/*
 * Copyright 2011, The Android Open Source Project
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

#ifndef __ANDROID_BCINFO_BCINFO_H__
#define __ANDROID_BCINFO_BCINFO_H__

#include <stddef.h>
#include <stdint.h>

/**
 * Extracted metadata for a bitcode source file.
 * Note that this struct is created by bcinfoGetScriptMetadata() and can only
 * be released/destroyed via bcinfoReleaseScriptMetadata().
 */
struct BCScriptMetadata {
  /**
   * Number of exported global variables (slots) in this script/module.
   */
  size_t exportVarCount;
  /**
   * Number of exported global functions (slots) in this script/module.
   */
  size_t exportFuncCount;

  /**
   * Number of pragmas contained in pragmaKeyList and pragmaValueList.
   */
  size_t pragmaCount;
  /**
   * Pragma keys (the name for the pragma).
   */
  const char **pragmaKeyList;
  /**
   * Pragma values (the contents corresponding to a particular pragma key).
   */
  const char **pragmaValueList;

  /**
   * Number of object slots contained in objectSlotList.
   */
  size_t objectSlotCount;
  /**
   * Array of object slot numbers that must be cleaned up on script teardown.
   */
  uint32_t *objectSlotList;
};

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Construct a BCScriptMetadata object containing the appropriate metadata
 * information for a given \p bitcode file. Note that
 * bcinfoReleaseScriptMetadata() must be called to free resources associated
 * with calls to this function.
 *
 * \param bitcode - string containing bitcode to parse/process.
 * \param bitcodeSize - size of the bitcode string (in bytes).
 * \param flags - must be zero for current version.
 *
 * \return pointer to BCScriptMetadata struct containing appropriate metadata.
 */
struct BCScriptMetadata *bcinfoGetScriptMetadata(const char *bitcode,
    size_t bitcodeSize, unsigned int flags);

/**
 * Releases all resources within a BCScriptMetadata struct.
 * Note that this function must be called to free resources associated with
 * calls to bcinfoGetScriptMetadata().
 *
 * \param md - BCScriptMetadata to release/free.
 */
extern "C" void bcinfoReleaseScriptMetadata(struct BCScriptMetadata **md);

#ifdef __cplusplus
}
#endif

#endif  // __ANDROID_BCINFO_BCINFO_H__
