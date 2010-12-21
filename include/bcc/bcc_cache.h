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
#define OBCC_MAGIC       "bcc\n"

/* BCC Cache File Version, encoded in 4 bytes of ASCII */
#define OBCC_MAGIC_VERS  "001\0"

/* BCC Cache Header Structure */
struct oBCCHeader {
  uint8_t magic[4];             /* includes version number */
  uint8_t magicVersion[4];

  long sourceWhen;
  long sourceCRC32;

  uint32_t rslibWhen;
  uint32_t libRSWhen;
  uint32_t libbccWhen;

  unsigned char sourceSHA1[20];

  uint32_t cachedCodeDataAddr;
  uint32_t rootAddr;
  uint32_t initAddr;

  uint32_t libRSThreadable;     /* TODO: This is an hack. Should be fixed
                                   in the long term. */

  uint32_t relocOffset;         /* offset of reloc table. */
  uint32_t relocCount;
  uint32_t exportVarsOffset;    /* offset of export var table */
  uint32_t exportVarsCount;
  uint32_t exportFuncsOffset;   /* offset of export func table */
  uint32_t exportFuncsCount;
  uint32_t exportPragmasOffset; /* offset of export pragma table */
  uint32_t exportPragmasCount;
  uint32_t exportPragmasSize;   /* size of export pragma table (in bytes) */

  uint32_t codeOffset;          /* offset of code: 64-bit alignment */
  uint32_t codeSize;
  uint32_t dataOffset;          /* offset of data section */
  uint32_t dataSize;

  /* uint32_t flags; */         /* some info flags */
  uint32_t checksum;            /* adler32 checksum covering deps/opt */
};


/* BCC Cache Relocation Entry */
struct oBCCRelocEntry {
  uint32_t relocType;           /* target instruction relocation type */
  uint32_t relocOffset;         /* offset of hole (holeAddr - codeAddr) */
  uint32_t cachedResultAddr;    /* address resolved at compile time */

  oBCCRelocEntry(uint32_t ty, uintptr_t off, void *addr)
    : relocType(ty),
      relocOffset(static_cast<uint32_t>(off)),
      cachedResultAddr(reinterpret_cast<uint32_t>(addr)) {
  }
};


/* BCC Cache Pragma Entry */
struct oBCCPragmaEntry {
  uint32_t pragmaNameOffset;
  uint32_t pragmaNameSize;
  uint32_t pragmaValueOffset;
  uint32_t pragmaValueSize;
};


/* BCC Cache Header Offset Table */
/* TODO(logan): Deprecated.  Will remove this. */
#define k_magic                 offsetof(oBCCHeader, magic)
#define k_magicVersion          offsetof(oBCCHeader, magicVersion)
#define k_sourceWhen            offsetof(oBCCHeader, sourceWhen)
#define k_rslibWhen             offsetof(oBCCHeader, rslibWhen)
#define k_libRSWhen             offsetof(oBCCHeader, libRSWhen)
#define k_libbccWhen            offsetof(oBCCHeader, libbccWhen)
#define k_cachedCodeDataAddr    offsetof(oBCCHeader, cachedCodeDataAddr)
#define k_rootAddr              offsetof(oBCCHeader, rootAddr)
#define k_initAddr              offsetof(oBCCHeader, initAddr)
#define k_relocOffset           offsetof(oBCCHeader, relocOffset)
#define k_relocCount            offsetof(oBCCHeader, relocCount)
#define k_exportVarsOffset      offsetof(oBCCHeader, exportVarsOffset)
#define k_exportVarsCount       offsetof(oBCCHeader, exportVarsCount)
#define k_exportFuncsOffset     offsetof(oBCCHeader, exportFuncsOffset)
#define k_exportFuncsCount      offsetof(oBCCHeader, exportFuncsCount)
#define k_exportPragmasOffset   offsetof(oBCCHeader, exportPragmasOffset)
#define k_exportPragmasCount    offsetof(oBCCHeader, exportPragmasCount)
#define k_codeOffset            offsetof(oBCCHeader, codeOffset)
#define k_codeSize              offsetof(oBCCHeader, codeSize)
#define k_dataOffset            offsetof(oBCCHeader, dataOffset)
#define k_dataSize              offsetof(oBCCHeader, dataSize)
#define k_checksum              offsetof(oBCCHeader, checksum)


#endif /* BCC_CACHE_H */
