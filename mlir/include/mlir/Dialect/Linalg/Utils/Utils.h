//===- Utils.h - Utilities to support the Linalg dialect --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_LINALG_UTILS_H_
#define MLIR_DIALECT_LINALG_UTILS_H_

#include "mlir/Dialect/Affine/EDSC/Intrinsics.h"
#include "mlir/Dialect/Linalg/EDSC/Builders.h"
#include "mlir/Dialect/Linalg/IR/LinalgOps.h"
#include "mlir/Dialect/SCF/SCF.h"
#include "mlir/Dialect/StandardOps/EDSC/Intrinsics.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"

#include "llvm/ADT/SetVector.h"

using mlir::edsc::intrinsics::AffineIndexedValue;
using mlir::edsc::intrinsics::StdIndexedValue;

namespace mlir {
class AffineExpr;
class AffineForOp;
class AffineMap;
class OperationFolder;
class PatternRewriter;

namespace linalg {
class LinalgDependenceGraph;

struct FusionInfo {
  LinalgOp originalProducer;
  LinalgOp fusedProducer;
};

/// A struct containing common matchers over linalg op's region.
struct RegionMatcher {
  enum class BinaryOpKind {
    IAdd,
  };

  /// Matches the given linalg op if its body is performing binary operation on
  /// int or float scalar values and returns the binary op kind.
  ///
  /// The linalg op's region is expected to be
  /// ```
  /// {
  ///   ^bb(%a: <scalar-type>, %b: <scalar-type>):
  ///     %0 = <binary-op> %a, %b: <scalar-type>
  ///     linalg.yield %0: <scalar-type>
  /// }
  /// ```
  static Optional<BinaryOpKind> matchAsScalarBinaryOp(GenericOp op);
};

/// Checks if an iterator_type attribute is parallel.
bool isParallelIteratorType(Attribute attr);

/// Checks if an iterator_type attribute is parallel.
bool isReductionIteratorType(Attribute attr);

/// Checks if an iterator_type attribute is parallel.
bool isWindowIteratorType(Attribute attr);

/// Checks whether the specific `producer` is the last write to exactly the
/// whole `consumedView`. This checks structural dominance, that the dependence
/// is a RAW without any interleaved write to any piece of `consumedView`.
bool isProducerLastWriteOfView(const LinalgDependenceGraph &graph,
                               LinalgOp consumer, Value consumedView,
                               LinalgOp producer);

/// Checks whether fusing the specific `producer` of the `consumedView` is
/// feasible. This checks `producer` is the last write of `consumedView` and
/// that no interleaved dependence would be violated (RAW, WAR or WAW).
bool isFusableInto(const LinalgDependenceGraph &graph, LinalgOp consumer,
                   Value consumedView, LinalgOp producer);

/// Fuses producer into consumer if the producer is structurally feasible and
/// the fusion would not violate dependencies.
/// When non-null, the optional pointer `folder` is used to call into the
/// `createAndFold` builder method. If `folder` is null, the regular `create`
/// method is called.
Optional<FusionInfo> fuseProducerOf(OpBuilder &b, LinalgOp consumer,
                                    unsigned consumerIdx,
                                    const LinalgDependenceGraph &graph,
                                    OperationFolder *folder = nullptr);

/// Fuse linalg operation on tensors, with the producer of the operand at
/// position `consumerIdx` of the consumer.
Operation *fuseTensorOps(PatternRewriter &rewriter, Operation *consumer,
                         unsigned consumerIdx,
                         OperationFolder *folder = nullptr);

/// Returns the linearized list of all view dimensions in a linalgOp. Applying
/// the inverse, concatenated loopToOperandRangeMaps to this list allows the
/// derivation of loop ranges for any linalgOp.
template <typename ConcreteOp>
SmallVector<Value, 8> getViewSizes(OpBuilder &builder, ConcreteOp linalgOp) {
  auto loc = linalgOp.getLoc();
  SmallVector<Value, 8> res;
  SmallVector<unsigned, 4> ranks;
  for (auto v : linalgOp.getInputsAndOutputBuffers()) {
    MemRefType t = v.getType().template cast<MemRefType>();
    ranks.push_back(t.getRank());
    for (unsigned i = 0; i < t.getRank(); ++i)
      res.push_back(builder.create<DimOp>(loc, v, i));
  }

  auto attr = linalgOp.template getAttrOfType<IntegerAttr>("symbol_source");
  if (attr) {
    // Find the correct position for inserting values for symbols.
    unsigned numSymb = ranks[attr.getInt()], symbolsPos = 0;
    for (unsigned idx = 0; idx < attr.getInt(); idx++)
      symbolsPos += ranks[idx];

    // Append the end of the value list that corresponds to the
    // values mapping to symbols. Since inside concatinated map symbols are
    // repeated we have to repeat the sizes as well.

    // Reserve is mandatory to avoid a potential undefined behavior with
    // pushing back to smallvector from itself.
    res.reserve(res.size() + ranks.size() * numSymb);
    for (unsigned idx = 0, s = ranks.size(); idx < s; ++idx)
      for (unsigned idx2 = 0; idx2 < numSymb; ++idx2)
        res.push_back(res[symbolsPos + idx2]);
  }
  return res;
}

/// Returns the values obtained by applying `map` to the list of values.
/// When non-null, the optional pointer `folder` is used to call into the
/// `createAndFold` builder method. If `folder` is null, the regular `create`
/// method is called.
SmallVector<Value, 4> applyMapToValues(OpBuilder &b, Location loc,
                                       AffineMap map, ValueRange values,
                                       OperationFolder *folder = nullptr);

/// Returns all the operands of `linalgOp` that are not views.
/// Asserts that these operands are value types to allow transformations like
/// tiling to just use the values when cloning `linalgOp`.
SmallVector<Value, 4> getAssumedNonViewOperands(LinalgOp linalgOp);

/// Apply the permutation defined by `permutation` to `inVec`.
/// Element `i` in `inVec` is mapped to location `j = permutation[i]`.
/// E.g.: for an input vector `inVec = ['a', 'b', 'c']` and a permutation vector
/// `permutation = [2, 0, 1]`, this function leaves `inVec = ['c', 'a', 'b']`.
template <typename T, unsigned N>
void applyPermutationToVector(SmallVector<T, N> &inVec,
                              ArrayRef<unsigned> permutation) {
  SmallVector<T, N> auxVec(inVec.size());
  for (unsigned i = 0; i < permutation.size(); ++i)
    auxVec[i] = inVec[permutation[i]];
  inVec = auxVec;
}

/// Scheme used to distribute loops to processors.
enum class DistributionMethod {
  /// Cyclic distribution where no assumption is made about the dynamic
  /// relationship between number of processors and number of iterations of the
  /// distributed loop. Distributes the following loop
  ///
  /// scf.parallel (%iv) = (%lb) to (%ub) step (%step)
  ///
  /// to
  ///
  /// scf.parallel(%iv)= (%lb + %procId * %step) to (%ub) step (%step * %nprocs)
  Cyclic = 0,

  /// Cyclic distribtuion where the number of processors can be assumed to be
  /// more than or equal to the number of iterations of the distributed loop. In
  /// such cases, a simple in-bounds check is enough (instead of materializing a
  /// loop). Distributes the following loop
  ///
  /// scf.parallel (%iv) = (%lb) to (%ub) step (%step)
  ///
  /// to
  ///
  /// %iv = %lb + %procId * %step
  /// %cond = cmpi "slt", %iv, %ub
  /// scf.if %cond {
  ///   ...
  /// }
  CyclicNumProcsGeNumIters = 1,

  /// Cyclic distribtuion where the number of processors can be assumed to be
  ///  equal to the number of iterations of the distributed loop. In such cases,
  ///  no bounds check is needed. Distributes the following loop
  ///
  /// scf.parallel (%iv) = (%lb) to (%ub) step (%step)
  ///
  /// to
  ///
  /// %iv = %lb + %procId * %step
  CyclicNumProcsEqNumIters = 2
};

/// Callback function type used to get processor ID, and number of processors
/// used for distribution.
struct ProcInfo {
  Value procId;
  Value nprocs;
};
using ProcInfoCallBackFn =
    std::function<ProcInfo(OpBuilder &b, Location loc, unsigned loopNum)>;

/// Options that allow distribution of loops generated in Linalg transforms to
/// processors while generating the loops.
struct LinalgLoopDistributionOptions {
  /// Callback function that returns the Value for processor ID, and number of
  /// processors used to execute a given loop.
  ProcInfoCallBackFn procInfo;
  /// Specification of how to distribute the `scf.parallel` loops that are
  /// generated. As the `scf.parallel` loop is generated, the elements of this
  /// vector is used (from left to right) and the specified distribution is
  /// applied. If the vector is less than the number of `scf.parallel` loops
  /// generated, then no distribution is applied.
  SmallVector<DistributionMethod, 0> distributionMethod = {};
};

/// Utility class used to generate nested loops with ranges described by
/// `loopRanges` and loop type described by the `iteratorTypes`. `bodyBuilderFn`
/// is used to generate the body of the innermost loop. It is passed a range
/// of loop induction variables.
template <typename LoopTy>
struct GenerateLoopNest {
  using IndexedValueTy =
      typename std::conditional<std::is_same<LoopTy, AffineForOp>::value,
                                AffineIndexedValue, StdIndexedValue>::type;

  static void doit(ArrayRef<SubViewOp::Range> loopRanges,
                   ArrayRef<Attribute> iteratorTypes,
                   function_ref<void(ValueRange)> bodyBuilderFn,
                   Optional<LinalgLoopDistributionOptions> = None);
};

} // namespace linalg
} // namespace mlir

#endif // MLIR_DIALECT_LINALG_UTILS_H_
