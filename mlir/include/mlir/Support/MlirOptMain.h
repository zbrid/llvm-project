//===- MlirOptMain.h - MLIR Optimizer Driver main ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Main entry function for mlir-opt for when built as standalone binary.
//
//===----------------------------------------------------------------------===//

#include <memory>
#include <vector>

namespace llvm {
class raw_ostream;
class MemoryBuffer;
} // end namespace llvm

namespace mlir {
struct LogicalResult;
class PassPipelineCLParser;

/// Run an passPipeline on the provided memory buffer loaded as an MLIRModule.
/// The preloadDialectsInContext option will trigger an option upfront loading
/// of all dialects from the global registry in the MLIRContext. This option is
/// deprecated and will be removed soon.
LogicalResult MlirOptMain(llvm::raw_ostream &os,
                          std::unique_ptr<llvm::MemoryBuffer> buffer,
                          const PassPipelineCLParser &passPipeline,
                          bool splitInputFile, bool verifyDiagnostics,
                          bool verifyPasses, bool allowUnregisteredDialects,
                          bool preloadDialectsInContext = false);

} // end namespace mlir
