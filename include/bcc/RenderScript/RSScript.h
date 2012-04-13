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

#ifndef BCC_RS_SCRIPT_H
#define BCC_RS_SCRIPT_H

#include <string>

#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/CodeGen.h>

#include "bcc/Script.h"
#include "bcc/Support/Sha1Util.h"

namespace bcc {

class RSInfo;
class Source;

class RSScript : public Script {
public:
  class SourceDependency {
  private:
    std::string mSourceName;
    uint8_t mSHA1[SHA1_DIGEST_LENGTH];

  public:
    SourceDependency(const std::string &pSourceName,
                     const uint8_t *pSHA1);

    inline const std::string &getSourceName() const
    { return mSourceName; }

    inline const uint8_t *getSHA1Checksum() const
    { return mSHA1; }
  };
  typedef llvm::SmallVectorImpl<SourceDependency *> SourceDependencyListTy;

  // This is one-one mapping with the llvm::CodeGenOpt::Level in
  // llvm/Support/CodeGen.h. Therefore, value of this type can safely cast
  // to llvm::CodeGenOpt::Level. This makes RSScript LLVM-free.
  enum OptimizationLevel {
    kOptLvl0, // -O0
    kOptLvl1, // -O1
    kOptLvl2, // -O2, -Os
    kOptLvl3  // -O3
  };

private:
  llvm::SmallVector<SourceDependency *, 4> mSourceDependencies;

  const RSInfo *mInfo;

  unsigned mCompilerVersion;

  OptimizationLevel mOptimizationLevel;

private:
  // This will be invoked when the containing source has been reset.
  virtual bool doReset();

public:
  RSScript(Source &pSource);

  // Add dependency information for this script given the source named
  // pSourceName. pSHA1 is the SHA-1 checksum of the given source. Return
  // false on error.
  bool addSourceDependency(const std::string &pSourceName,
                           const uint8_t *pSHA1);

  const SourceDependencyListTy &getSourceDependencies() const
  { return mSourceDependencies; }

  // Set the associated RSInfo of the script.
  void setInfo(const RSInfo *pInfo)
  { mInfo = pInfo; }

  const RSInfo *getInfo() const
  { return mInfo; }

  void setCompilerVersion(unsigned pCompilerVersion)
  {  mCompilerVersion = pCompilerVersion; }

  unsigned getCompilerVersion() const
  {  return mCompilerVersion; }

  void setOptimizationLevel(OptimizationLevel pOptimizationLevel)
  {  mOptimizationLevel = pOptimizationLevel; }

  OptimizationLevel getOptimizationLevel() const
  {  return mOptimizationLevel; }

  ~RSScript();
};

} // end namespace bcc

#endif // BCC_RS_SCRIPT_H
