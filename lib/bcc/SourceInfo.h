/*
 * Copyright 2010, The Android Open Source Project
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

#ifndef BCC_SOURCEINFO_H
#define BCC_SOURCEINFO_H

#include "Config.h"

#include <llvm/ADT/OwningPtr.h>
#include <llvm/Module.h>

#include <stddef.h>

namespace bcc {
  class ScriptCompiled;

  namespace SourceKind {
    enum SourceType {
      File,
      Buffer,
      Module,
    };
  }

  class SourceInfo {
  private:
    SourceKind::SourceType type;

    llvm::OwningPtr<llvm::Module> module;
    // Note: module should not be a part of union.  Since, we are going to
    // use module to store the pointer to parsed bitcode.

    union {
      struct {
        char const *resName;
        char const *bitcode;
        size_t bitcodeSize;
      } buffer;

      struct {
        char const *path;
      } file;
    };

    unsigned long flags;

#if USE_CACHE
    unsigned char sha1[20];
#endif

  private:
    SourceInfo() { }

  public:
    static SourceInfo *createFromBuffer(char const *resName,
                                        char const *bitcode,
                                        size_t bitcodeSize,
                                        unsigned long flags);

    static SourceInfo *createFromFile(char const *path,
                                      unsigned long flags);

    static SourceInfo *createFromModule(llvm::Module *module,
                                        unsigned long flags);

    llvm::Module *takeModule() {
      return module.take();
    }

    llvm::Module *getModule() const {
      return module.get();
    }

    int prepareModule(ScriptCompiled *);

#if USE_CACHE
    template <typename T> void introDependency(T &checker);
#endif
  };


} // namespace bcc

#endif // BCC_SOURCEINFO_H
