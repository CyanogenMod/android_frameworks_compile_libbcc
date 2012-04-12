/*
 * copyright 2010-2012, the android open source project
 *
 * licensed under the apache license, version 2.0 (the "license");
 * you may not use this file except in compliance with the license.
 * you may obtain a copy of the license at
 *
 *     http://www.apache.org/licenses/license-2.0
 *
 * unless required by applicable law or agreed to in writing, software
 * distributed under the license is distributed on an "as is" basis,
 * without warranties or conditions of any kind, either express or implied.
 * see the license for the specific language governing permissions and
 * limitations under the license.
 */

#include "bcc/Support/Sha1Helper.h"

#include <cstring>

#include <utils/StopWatch.h>

#include "bcc/Config/Config.h"
#include "bcc/Support/Log.h"
#include "bcc/Support/InputFile.h"

#include "sha1.h"

namespace bcc {

unsigned char sha1LibBCC_SHA1[20];
char const *pathLibBCC_SHA1 = "/system/lib/libbcc.so.sha1";

unsigned char sha1LibRS[20];
char const *pathLibRS = "/system/lib/libRS.so";

void calcSHA1(unsigned char *result, char const *data, size_t size) {
  SHA1_CTX hashContext;

  SHA1Init(&hashContext);
  SHA1Update(&hashContext,
             reinterpret_cast<const unsigned char *>(data),
             static_cast<unsigned long>(size));

  SHA1Final(result, &hashContext);
}


void calcFileSHA1(unsigned char *result, char const *filename) {
  android::StopWatch calcFileSHA1Timer("calcFileSHA1 time");

  InputFile file(filename);

  if (file.hasError()) {
    ALOGE("Unable to open the file %s before SHA-1 checksum "
          "calculation! (%s)", filename, file.getErrorMessage().c_str());
    memset(result, '\0', 20);
    return;
  }

  SHA1_CTX hashContext;
  SHA1Init(&hashContext);

  char buf[256];
  while (true) {
    ssize_t nread = file.read(buf, sizeof(buf));

    if (nread < 0) {
      break;
    }

    SHA1Update(&hashContext,
               reinterpret_cast<unsigned char *>(buf),
               static_cast<unsigned long>(nread));

    if ((size_t)nread < sizeof(buf)) {
      // finished.
      break;
    }
  }

  SHA1Final(result, &hashContext);
}

void readSHA1(unsigned char *result, int result_size, char const *filename) {
  InputFile file(filename);
  if (file.hasError()) {
    ALOGE("Unable to open the binary sha1 file %s! (%s)", filename,
          file.getErrorMessage().c_str());
    memset(result, '\0', result_size);
    return;
  }
  file.read((char *)result, result_size);
}

} // namespace bcc
