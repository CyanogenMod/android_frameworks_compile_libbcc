/*
 * Copyright 2015, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef BCC_RS_METADATA_H
#define BCC_RS_METADATA_H

#include "stdint.h"

namespace llvm {
  class Module;
  class Function;
}

namespace bcc {

/// @brief Class to manage RenderScript metadata.
class RSMetadata{
  llvm::Module &Module;

public:

  /// @brief Create a metadata manager for a specific LLVM module.
  ///
  /// @param Module The module to work on.
  RSMetadata(llvm::Module &Module);

  /// @brief Delete all metadata.
  void deleteAll();

  /// @brief Add foreach function.
  ///
  /// Add metadata to describe a new foreach function.
  ///
  /// @param Function The function to mark.
  /// @param Properties The properties of the function.
  void markForEachFunction(llvm::Function &Function, uint32_t Properties);
};

} // end namespace bcc

#endif /* BCC_RS_METADATA_H */
