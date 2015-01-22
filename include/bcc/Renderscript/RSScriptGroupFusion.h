/*
 * Copyright 2013, The Android Open Source Project
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

#ifndef BCC_RS_SCRIPT_GROUP_FUSION_H
#define BCC_RS_SCRIPT_GROUP_FUSION_H

#include <vector>

namespace llvm {
class Module;
}

namespace bcc {

class Source;
class RSScript;
class BCCContext;

/// @brief Fuse kernels
///
/// @param Sources The Sources containing the kernels.
/// @param Slots The slots where the kernels are located.
/// @return A script that containing the fused kernels.
// TODO(yangni): Check FP precision. (http://b/19098612)
llvm::Module* fuseKernels(BCCContext& Context,
                          const std::vector<const Source *>& sources,
                          const std::vector<int>& slots);
}

#endif /* BCC_RS_SCRIPT_GROUP_FUSION_H */
