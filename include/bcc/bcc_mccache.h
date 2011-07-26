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

#ifndef BCC_MCCACHE_H
#define BCC_MCCACHE_H

#include <stdint.h>
#include <sys/types.h>

#include "bcc_cache.h"

/* BCC Cache File Magic Word */
#define MCO_MAGIC "\0bcc"

/* BCC Cache File Version, encoded in 4 bytes of ASCII */
#define MCO_VERSION "001\0"

/* BCC Cache Header Structure */
struct MCO_Header {
  /* magic and version */
  uint8_t magic[4];
  uint8_t version[4];

  /* machine-dependent integer type size */
  uint8_t endianness;
  uint8_t sizeof_off_t;
  uint8_t sizeof_size_t;
  uint8_t sizeof_ptr_t;

  /* string pool section */
  off_t str_pool_offset;
  size_t str_pool_size;

  /* dependancy table */
  off_t depend_tab_offset;
  size_t depend_tab_size;

  /* relocation table section */
  off_t reloc_tab_offset;
  size_t reloc_tab_size;

  /* pragma list section */
  off_t pragma_list_offset;
  size_t pragma_list_size;

  /* function table */
  off_t func_table_offset;
  size_t func_table_size;

  /* function table */
  off_t object_slot_list_offset;
  size_t object_slot_list_size;

  /* export variable name list section */
  off_t export_var_name_list_offset;
  size_t export_var_name_list_size;

  /* export function name list section */
  off_t export_func_name_list_offset;
  size_t export_func_name_list_size;

  /* dirty hack for libRS */
  /* TODO: This should be removed in the future */
  uint32_t libRS_threadable;
};


#endif /* BCC_MCCACHE_H */
