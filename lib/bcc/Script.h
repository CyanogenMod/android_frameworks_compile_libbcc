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

#ifndef BCC_SCRIPT_H
#define BCC_SCRIPT_H

#include "Compiler.h"

#include <bcc/bcc.h>

namespace bcc {

  class Script {
  public:
    //////////////////////////////////////////////////////////////////////////
    // Part I. Compiler
    //////////////////////////////////////////////////////////////////////////
    Compiler compiler;

    void registerSymbolCallback(BCCSymbolLookupFn pFn, BCCvoid *pContext) {
      compiler.registerSymbolCallback(pFn, pContext);
    }

    //////////////////////////////////////////////////////////////////////////
    // Part II. Logistics & Error handling
    //////////////////////////////////////////////////////////////////////////
    Script() {
      bccError = BCC_NO_ERROR;
    }

    ~Script() {
    }

    void setError(BCCenum error) {
      if (bccError == BCC_NO_ERROR && error != BCC_NO_ERROR) {
        bccError = error;
      }
    }

    BCCenum getError() {
      BCCenum result = bccError;
      bccError = BCC_NO_ERROR;
      return result;
    }

    BCCenum bccError;
  };

} // namespace bcc

#endif // BCC_SCRIPT_H
