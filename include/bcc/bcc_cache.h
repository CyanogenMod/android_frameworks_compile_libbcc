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

#ifndef BCC_CACHE_H
#define BCC_CACHE_H


/* BCC Cache File Magic Word */
#define OBCC_MAGIC "\0bcc"

/* BCC Cache File Version, encoded in 4 bytes of ASCII */
#define OBCC_VERSION "004\0"

/* BCC Cache Header Structure */
struct OBCC_Header {
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

  /* export variable list section */
  off_t export_var_list_offset;
  size_t export_var_list_size;

  /* export function list section */
  off_t export_func_list_offset;
  size_t export_func_list_size;

  /* pragma list section */
  off_t pragma_list_offset;
  size_t pragma_list_size;

  /* function table */
  off_t func_table_offset;
  size_t func_table_size;

  /* context section */
  off_t context_offset;
  char *context_cached_addr;
  uint32_t context_parity_checksum;

  /* dirty hacks */
  /* TODO: This is an hack. Should be removed. */
  uint32_t libRS_threadable;
};

struct OBCC_String {
  size_t length; /* String length, without ending '\0' */
  off_t offset; /* Note: Offset related to string_pool_offset. */
};

struct OBCC_StringPool {
  size_t count;
  struct OBCC_String list[];
};

struct OBCC_Dependancy {
  size_t resource_strp_index;
  char sha1[20];
};

struct OBCC_DependancyTable {
  size_t count;
  struct OBCC_Dependancy table[];
};

struct OBCC_RelocationTable {
/* TODO: Implement relocation table. */
};

struct OBCC_ExportVarList {
  size_t count;
  void *cached_addr_list[];
};

struct OBCC_ExportFuncList {
  size_t count;
  void *cached_addr_list[];
};

struct OBCC_Pragma {
  size_t key_strp_index;
  size_t value_strp_index;
};

struct OBCC_PragmaList {
  size_t count;
  struct OBCC_Pragma list[];
};

struct OBCC_FuncInfo {
  size_t name_strp_index;
  size_t size;
  void *cached_addr;
};

struct OBCC_FuncTable {
  size_t count;
  struct OBCC_FuncInfo table[];
};


#endif /* BCC_CACHE_H */
