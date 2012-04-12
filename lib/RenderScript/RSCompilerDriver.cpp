/*
 * Copyright 2012, The Android Open Source Project
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

#include "bcc/RenderScript/RSCompilerDriver.h"

#include "bcc/RenderScript/RSExecutable.h"
#include "bcc/Support/CompilerConfig.h"
#include "bcc/Support/TargetCompilerConfigs.h"
#include "bcc/Support/FileMutex.h"
#include "bcc/Support/Log.h"
#include "bcc/Support/InputFile.h"
#include "bcc/Support/Initialization.h"
#include "bcc/Support/OutputFile.h"

#include <cutils/properties.h>
#include <utils/String8.h>

using namespace bcc;

namespace {

bool is_force_recompile() {
  char buf[PROPERTY_VALUE_MAX];

  property_get("debug.rs.forcerecompile", buf, "0");
  if ((::strcmp(buf, "1") == 0) || (::strcmp(buf, "true") == 0)) {
    return true;
  } else {
    return false;
  }
}

} // end anonymous namespace

RSCompilerDriver::RSCompilerDriver() : mConfig(NULL), mCompiler() {
  init::Initialize();
  // Chain the symbol resolvers for BCC runtimes and RS runtimes.
  mResolver.chainResolver(mBCCRuntime);
  mResolver.chainResolver(mRSRuntime);
}

RSCompilerDriver::~RSCompilerDriver() {
  delete mConfig;
}

RSExecutable *RSCompilerDriver::loadScriptCache(const RSScript &pScript,
                                                const std::string &pOutputPath){
  RSExecutable *result = NULL;

  if (is_force_recompile())
    return NULL;

  //===--------------------------------------------------------------------===//
  // Acquire the read lock for reading output object file.
  //===--------------------------------------------------------------------===//
  FileMutex<FileBase::kReadLock> read_output_mutex(pOutputPath);

  if (read_output_mutex.hasError() || !read_output_mutex.lock()) {
    ALOGE("Unable to acquire the read lock for %s! (%s)", pOutputPath.c_str(),
          read_output_mutex.getErrorMessage().c_str());
    return NULL;
  }

  //===--------------------------------------------------------------------===//
  // Read the output object file.
  //===--------------------------------------------------------------------===//
  InputFile *output_file = new (std::nothrow) InputFile(pOutputPath);

  if ((output_file == NULL) || output_file->hasError()) {
    ALOGE("Unable to open the %s for read! (%s)", pOutputPath.c_str(),
          output_file->getErrorMessage().c_str());
    delete output_file;
    return NULL;
  }

  //===--------------------------------------------------------------------===//
  // Acquire the read lock on output_file for reading its RS info file.
  //===--------------------------------------------------------------------===//
  android::String8 info_path = RSInfo::GetPath(*output_file);

  if (!output_file->lock()) {
    ALOGE("Unable to acquire the read lock on %s for reading %s! (%s)",
          pOutputPath.c_str(), info_path.string(),
          output_file->getErrorMessage().c_str());
    delete output_file;
    return NULL;
  }

 //===---------------------------------------------------------------------===//
  // Open and load the RS info file.
  //===--------------------------------------------------------------------===//
  InputFile info_file(info_path.string());
  RSInfo *info = RSInfo::ReadFromFile(info_file,
                                      pScript.getSourceDependencies());

  // Release the lock on output_file.
  output_file->unlock();

  if (info == NULL) {
    delete output_file;
    return NULL;
  }

  //===--------------------------------------------------------------------===//
  // Create the RSExecutable.
  //===--------------------------------------------------------------------===//
  result = RSExecutable::Create(*info, *output_file, mResolver);
  if (result == NULL) {
    delete output_file;
    delete info;
    return NULL;
  }

  // TODO: Dirty hack for libRS. This can be removed once RSExecutable is public
  //       to libRS.
  if (!result->isThreadable()) {
    mRSRuntime.getAddress("__clearThreadable");
  }

  return result;
}

bool RSCompilerDriver::setupConfig(const RSScript &pScript) {
  bool changed = false;

  const llvm::CodeGenOpt::Level script_opt_level =
      static_cast<llvm::CodeGenOpt::Level>(pScript.getOptimizationLevel());

  if (mConfig != NULL) {
    // Renderscript bitcode may have their optimization flag configuration
    // different than the previous run of RS compilation.
    if (mConfig->getOptimizationLevel() != script_opt_level) {
      mConfig->setOptimizationLevel(script_opt_level);
      changed = true;
    }
  } else {
    // Haven't run the compiler ever.
    mConfig = new (std::nothrow) DefaultCompilerConfig();
    if (mConfig == NULL) {
      // Return false since mConfig remains NULL and out-of-memory.
      return false;
    }
    mConfig->setOptimizationLevel(script_opt_level);
    changed = true;
  }

#if defined(DEFAULT_ARM_CODEGEN)
  // NEON should be disable when full-precision floating point is required.
  assert((pScript.getInfo() != NULL) && "NULL RS info!");
  if (pScript.getInfo()->getFloatPrecisionRequirement() == RSInfo::Full) {
    // Must be ARMCompilerConfig.
    ARMCompilerConfig *arm_config = static_cast<ARMCompilerConfig *>(mConfig);
    changed |= arm_config->enableNEON(/* pEnable */false);
  }
#endif

  return changed;
}

RSExecutable *RSCompilerDriver::compileScript(RSScript &pScript,
                                              const std::string &pOutputPath) {
  RSExecutable *result = NULL;
  RSInfo *info = NULL;

  //===--------------------------------------------------------------------===//
  // Extract RS-specific information from source bitcode.
  //===--------------------------------------------------------------------===//
  // RS info may contains configuration (such as #optimization_level) to the
  // compiler therefore it should be extracted before compilation.
  info = RSInfo::ExtractFromSource(pScript.getSource(),
                                   pScript.getSourceDependencies());
  if (info == NULL) {
    return NULL;
  }

  //===--------------------------------------------------------------------===//
  // Associate script with its info
  //===--------------------------------------------------------------------===//
  // This is required since RS compiler may need information in the info file
  // to do some transformation (e.g., expand foreach-able function.)
  pScript.setInfo(info);

  //===--------------------------------------------------------------------===//
  // Acquire the write lock for writing output object file.
  //===--------------------------------------------------------------------===//
  FileMutex<FileBase::kWriteLock> write_output_mutex(pOutputPath);

  if (write_output_mutex.hasError() || !write_output_mutex.lock()) {
    ALOGE("Unable to acquire the lock for writing %s! (%s)",
          pOutputPath.c_str(), write_output_mutex.getErrorMessage().c_str());
    return NULL;
  }

  //===--------------------------------------------------------------------===//
  // Open the output file for write.
  //===--------------------------------------------------------------------===//
  OutputFile *output_file = new (std::nothrow) OutputFile(pOutputPath);

  if ((output_file == NULL) || output_file->hasError()) {
    ALOGE("Unable to open the %s for write! (%s)", pOutputPath.c_str(),
          output_file->getErrorMessage().c_str());
    delete info;
    delete output_file;
    return NULL;
  }

  //===--------------------------------------------------------------------===//
  // Setup the config to the compiler.
  //===--------------------------------------------------------------------===//
  bool compiler_need_reconfigure = setupConfig(pScript);

  if (mConfig == NULL) {
    ALOGE("Failed to setup config for RS compiler to compile %s!",
          pOutputPath.c_str());
    delete info;
    delete output_file;
    return NULL;
  }

  // Compiler need to re-config if it's haven't run the config() yet or the
  // configuration it referenced is changed.
  if (compiler_need_reconfigure) {
    Compiler::ErrorCode err = mCompiler.config(*mConfig);
    if (err != Compiler::kSuccess) {
      ALOGE("Failed to config the RS compiler for %s! (%s)",pOutputPath.c_str(),
            Compiler::GetErrorString(err));
      delete info;
      delete output_file;
      return NULL;
    }
  }

  //===--------------------------------------------------------------------===//
  // Run the compiler.
  //===--------------------------------------------------------------------===//
  Compiler::ErrorCode compile_result = mCompiler.compile(pScript, *output_file);
  if (compile_result != Compiler::kSuccess) {
    ALOGE("Unable to compile the source to file %s! (%s)", pOutputPath.c_str(),
          Compiler::GetErrorString(compile_result));
    delete info;
    delete output_file;
    return NULL;
  }

  //===--------------------------------------------------------------------===//
  // Create the RSExecutable.
  //===--------------------------------------------------------------------===//
  result = RSExecutable::Create(*info, *output_file, mResolver);
  if (result == NULL) {
    delete info;
    delete output_file;
    return NULL;
  }

  // TODO: Dirty hack for libRS. This can be removed once RSExecutable is public
  //       to libRS.
  result->setThreadable(mRSRuntime.getAddress("__isThreadable") != NULL);

  //===--------------------------------------------------------------------===//
  // Write out the RS info file.
  //===--------------------------------------------------------------------===//
  // Note that write failure only results in a warning since the source is
  // successfully compiled and loaded.
  if (!result->syncInfo(/* pForce */true)) {
    ALOGW("%s was successfully compiled and loaded but its RS info file failed "
          "to write out!", pOutputPath.c_str());
  }

  return result;
}

RSExecutable *RSCompilerDriver::build(RSScript &pScript,
                                      const std::string &pOutputPath) {
  RSExecutable *result = loadScriptCache(pScript, pOutputPath);

  if (result != NULL) {
    // Cache hit
    return result;
  }

  return compileScript(pScript, pOutputPath);
}
