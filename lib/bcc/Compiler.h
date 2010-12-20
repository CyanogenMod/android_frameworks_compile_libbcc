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

#ifndef BCC_COMPILER_H
#define BCC_COMPILER_H

#include <bcc/bcc.h>

#include "CodeEmitter.h"
#include "CodeMemoryManager.h"

#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Target/TargetMachine.h"

#include <stddef.h>

#include <list>
#include <string>
#include <vector>
#include <utility>


namespace llvm {
  class LLVMContext;
  class Module;
}


namespace bcc {

  class Compiler {
  private:
    typedef std::list< std::pair<std::string, std::string> > PragmaList;
    typedef std::list<void*> ExportVarList;
    typedef std::list<void*> ExportFuncList;


  private:
    // This part is designed to be orthogonal to those exported bcc*() functions
    // implementation and internal struct BCCscript.

    //////////////////////////////////////////////////////////////////////////
    // The variable section below (e.g., Triple, CodeGenOptLevel)
    // is initialized in GlobalInitialization()
    //
    static bool GlobalInitialized;
    static const char *resNames[64];
    static int resNamesMmaped[64];

    // If given, this will be the name of the target triple to compile for.
    // If not given, the initial values defined in this file will be used.
    static std::string Triple;

    static llvm::CodeGenOpt::Level CodeGenOptLevel;

    // End of section of GlobalInitializing variables
    /////////////////////////////////////////////////////////////////////////
    // If given, the name of the target CPU to generate code for.
    static std::string CPU;

    // The list of target specific features to enable or disable -- this should
    // be a list of strings starting with '+' (enable) or '-' (disable).
    static std::vector<std::string> Features;

    static void GlobalInitialization();

    static void LLVMErrorHandler(void *UserData, const std::string &Message);

    static const llvm::StringRef PragmaMetadataName;
    static const llvm::StringRef ExportVarMetadataName;
    static const llvm::StringRef ExportFuncMetadataName;

    friend class CodeEmitter;
    friend class CodeMemoryManager;


  private:
    std::string mError;

    int mResId;             // Set by readBC()
    bool mUseCache;         // Set by readBC()
    bool mCacheNew;         // Set by readBC()
    int mCacheFd;           // Set by readBC()
    long mSourceModTime;    // Set by readBC()
    long mSourceCRC32;      // Set by readBC();
    char *mCacheMapAddr;    // Set by loadCacheFile() if mCacheNew is false
    oBCCHeader *mCacheHdr;  // Set by loadCacheFile()
    size_t mCacheSize;      // Set by loadCacheFile()
    ptrdiff_t mCacheDiff;   // Set by loadCacheFile()
    char *mCodeDataAddr;    // Set by CodeMemoryManager if mCacheNew is true.
                            // Used by genCacheFile() for dumping

    PragmaList mPragmas;

    ExportVarList mExportVars;

    ExportFuncList mExportFuncs;

    // The memory manager for code emitter
    llvm::OwningPtr<CodeMemoryManager> mCodeMemMgr;

    // The CodeEmitter
    llvm::OwningPtr<CodeEmitter> mCodeEmitter;

    BCCSymbolLookupFn mpSymbolLookupFn;
    void *mpSymbolLookupContext;

    llvm::LLVMContext *mContext;
    llvm::Module *mModule;

    bool mHasLinked;

  public:
    Compiler();

    // interface for BCCscript::registerSymbolCallback()
    void registerSymbolCallback(BCCSymbolLookupFn pFn, BCCvoid *pContext) {
      mpSymbolLookupFn = pFn;
      mpSymbolLookupContext = pContext;
    }

    CodeMemoryManager *createCodeMemoryManager();

    CodeEmitter *createCodeEmitter();

    int readModule(llvm::Module *module) {
      GlobalInitialization();
      mModule = module;
      return hasError();
    }

    int readBC(const char *bitcode,
               size_t bitcodeSize,
               long bitcodeFileModTime,
               long bitcodeFileCRC32,
               const BCCchar *resName,
               const BCCchar *cacheDir);

    int linkBC(const char *bitcode, size_t bitcodeSize);

    // interface for bccLoadBinary()
    int loadCacheFile();

    // interace for bccCompileBC()
    int compile();

    // interface for bccGetScriptInfoLog()
    char *getErrorMessage() {
      return const_cast<char*>(mError.c_str());
    }

    // interface for bccGetScriptLabel()
    void *lookup(const char *name);

    // Interface for bccGetExportVars()
    void getExportVars(BCCsizei *actualVarCount,
                       BCCsizei maxVarCount,
                       BCCvoid **vars);

    // Interface for bccGetExportFuncs()
    void getExportFuncs(BCCsizei *actualFuncCount,
                        BCCsizei maxFuncCount,
                        BCCvoid **funcs);

    // Interface for bccGetPragmas()
    void getPragmas(BCCsizei *actualStringCount,
                    BCCsizei maxStringCount,
                    BCCchar **strings);

    // Interface for bccGetFunctions()
    void getFunctions(BCCsizei *actualFunctionCount,
                      BCCsizei maxFunctionCount,
                      BCCchar **functions);

    // Interface for bccGetFunctionBinary()
    void getFunctionBinary(BCCchar *function,
                           BCCvoid **base,
                           BCCsizei *length);

    const llvm::Module *getModule() const {
      return mModule;
    }

    ~Compiler();

  private:
    // Note: loadCacheFile() and genCacheFile() go hand in hand
    void genCacheFile();

    // OpenCacheFile() returns fd of the cache file.
    // Input:
    //   BCCchar *resName: Used to genCacheFileName()
    //   bool createIfMissing: If false, turn off caching
    // Output:
    //   returns fd: If -1: Failed
    //   mCacheNew: If true, the returned fd is new. Otherwise, the fd is the
    //              cache file's file descriptor
    //              Note: openCacheFile() will check the cache file's validity,
    //              such as Magic number, sourceWhen... dependencies.
    int openCacheFile(const BCCchar *resName,
                      const BCCchar *cacheDir,
                      bool createIfMissing);

    char *genCacheFileName(const char *cacheDir,
                           const char *fileName,
                           const char *subFileName);

    /*
     * Read the oBCC header, verify it, then read the dependent section
     * and verify that data as well.
     *
     * On successful return, the file will be seeked immediately past the
     * oBCC header.
     */
    bool checkHeaderAndDependencies(int fd,
                                    long sourceWhen,
                                    uint32_t rslibWhen,
                                    uint32_t libRSWhen,
                                    uint32_t libbccWhen);


    struct {
      bool mNoCache;
    } props;


  private:

    bool hasError() const {
      return !mError.empty();
    }

    void setError(const char *Error) {
      mError.assign(Error);  // Copying
    }

    void setError(const std::string &Error) {
      mError = Error;
    }

  };  // End of class Compiler

} // namespace bcc

#endif // BCC_COMPILER_H
