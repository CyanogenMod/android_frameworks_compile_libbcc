/*
 * copyright 2010, the android open source project
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

#ifndef BCC_CONTEXTMANAGER_H
#define BCC_CONTEXTMANAGER_H

#include <stddef.h>

#include <unistd.h>


#define BCC_CONTEXT_FIXED_ADDR (reinterpret_cast<char *>(0x7e000000))
#define BCC_CONTEXT_SLOT_COUNT 8 

#define BCC_CONTEXT_CODE_SIZE (128 * 1024)
#define BCC_CONTEXT_DATA_SIZE (128 * 1024)
#define BCC_CONTEXT_SIZE (BCC_CONTEXT_CODE_SIZE + BCC_CONTEXT_DATA_SIZE)


namespace bcc {

  extern char *allocateContext();

  extern char *allocateContext(char *addr, int imageFd, off_t imageOffset);

  extern void deallocateContext(char *addr);

} // namespace bcc


#endif // BCC_CONTEXTMANAGER_H
