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

#include "bcc/Renderscript/RSMetadata.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Module.h"

// Name of metadata node where pragma info resides (should be synced with
// slang.cpp)
const llvm::StringRef pragma_metadata_name("#pragma");

/*
 * The following names should be synced with the one appeared in
 * slang_rs_metadata.h.
 */

// Name of metadata node where exported variable names reside
static const llvm::StringRef
export_var_metadata_name("#rs_export_var");

// Name of metadata node where exported function names reside
static const llvm::StringRef
export_func_metadata_name("#rs_export_func");

// Name of metadata node where exported ForEach name information resides
static const llvm::StringRef
export_foreach_name_metadata_name("#rs_export_foreach_name");

// Name of metadata node where exported ForEach signature information resides
static const llvm::StringRef
export_foreach_metadata_name("#rs_export_foreach");

// Name of metadata node where RS object slot info resides (should be
static const llvm::StringRef
object_slot_metadata_name("#rs_object_slots");

bcc::RSMetadata::RSMetadata(llvm::Module &Module) : Module(Module) {}

void bcc::RSMetadata::deleteAll() {
   std::vector<llvm::StringRef> MDNames;
   MDNames.push_back(pragma_metadata_name);
   MDNames.push_back(export_var_metadata_name);
   MDNames.push_back(export_func_metadata_name);
   MDNames.push_back(export_foreach_name_metadata_name);
   MDNames.push_back(export_foreach_metadata_name);
   MDNames.push_back(object_slot_metadata_name);

   for (std::vector<llvm::StringRef>::iterator MI = MDNames.begin(),
                                               ME = MDNames.end();
        MI != ME; ++MI) {
     llvm::NamedMDNode *MDNode = Module.getNamedMetadata(*MI);
     if (MDNode) {
       MDNode->eraseFromParent();
     }
   }
}

void bcc::RSMetadata::markForEachFunction(llvm::Function &Function,
  uint32_t Signature) {
  llvm::NamedMDNode *ExportForEachNameMD;
  llvm::NamedMDNode *ExportForEachMD;

  llvm::MDString *MDString;
  llvm::MDNode *MDNode;

  ExportForEachNameMD =
    Module.getOrInsertNamedMetadata(export_foreach_name_metadata_name);
  MDString = llvm::MDString::get(Module.getContext(), Function.getName());
  MDNode = llvm::MDNode::get(Module.getContext(), MDString);
  ExportForEachNameMD->addOperand(MDNode);

  ExportForEachMD =
    Module.getOrInsertNamedMetadata(export_foreach_metadata_name);
  MDString = llvm::MDString::get(Module.getContext(),
                                 llvm::utostr_32(Signature));
  MDNode = llvm::MDNode::get(Module.getContext(), MDString);
  ExportForEachMD->addOperand(MDNode);
}
