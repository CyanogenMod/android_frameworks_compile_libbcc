============================================
libbcc: A Versatile Bitcode Execution Engine
============================================


Introduction
------------

libbcc is an LLVM bitcode execution engine that compiles the bitcode
to an in-memory executable.

libbcc provides:

* a *just-in-time (JIT) bitcode compiler*, which translates the bitcode into
  machine code

* a *caching mechanism*, which can:

  * after the compilation, serialize the in-memory executable into a cache file.
    Note that the compilation is triggered by a cache miss.
  * load from the cache file upon cache-hit.

Here are some highlights of libbcc:

* libbcc supports bitcode from various language frontends, such as
  RenderScript, GLSL.

* libbcc strives to balance between library size, launch time and
  steady-state performance:

  * The size of libbcc is aggressively reduced for mobile devices.
    We customize and we don't use Execution Engine.

  * To reduce launch time, we support caching of binaries.

  * For steady-state performance, we enable VFP3 and aggressive
    optimizations.

* Currently we disable Lazy JITting.



API
---

**Basic:**

* **bccCreateScript** - Create new bcc script

* **bccRegisterSymbolCallback** - Register the callback function for external
  symbol lookup

* **bccReadBC** - Set the source bitcode for compilation

* **bccReadModule** - Set the llvm::Module for compilation

* **bccLinkBC** - Set the library bitcode for linking

* **bccPrepareExecutable** - Create the in-memory executable by either
  just-in-time compilation or cache loading

* **bccDeleteScript** - Destroy bcc script and release the resources

* **bccGetError** - Get the error code

* **bccGetScriptInfoLog** - *deprecated* - Don't use this


**Reflection:**

* **bccGetExportVars** - Get the addresses of exported variables

* **bccGetExportFuncs** - Get the addresses of exported functions

* **bccGetPragmas** - Get the pragmas


**Debug:**

* **bccGetFunctions** - Get the function name list

* **bccGetFunctionBinary** - Get the address and the size of a function binary



Cache File Format
-----------------

A cache file (denoted as \*.oBCC) for libbcc consists of several sections:
header, string pool, dependencies table, relocation table, exported
variable list, exported function list, pragma list, function information
table, and bcc context.  Every section should be aligned to a word size.
Here is the brief description of each sections:

* **Header** (OBCC_Header) - The header of a cache file. It contains the
  magic word, version, machine integer type information, and the size
  and the offset of other sections.  The header section is guaranteed
  to be at the beginning of the cache file.

* **String Pool** (OBCC_StringPool) - A collection of serialized variable
  length strings.  The strp_index in the other part of the cache file
  represents the index of such string in this string pool.

* **Dependencies Table** (OBCC_DependencyTable) - The dependencies table.
  This table stores the resource name (or file path), the resouece
  type (rather in APK or on the file system), and the SHA1 checksum.

* **Relocation Table** (OBCC_RelocationTable) - *not enabled*

* **Exported Variable List** (OBCC_ExportVarList),
  **Exported Function List** (OBCC_ExportFuncList) -
  The list of the addresses of exported variables and exported functions.

* **Pragma List** (OBCC_PragmaList) - The list of pragma key-value pair.

* **Function Information Table** (OBCC_FuncTable) - This is a table of
  function information, such as function name, function entry address,
  and function binary size.  Besides, the table should be ordered by
  function name.

* **Context** - The context of the in-memory executable, including
  the code and the data.  The offset of context should aligned to
  a page size, so that we can mmap the context directly into the memory.

For furthur information, you may read `bcc_cache.h <include/bcc/bcc_cache.h>`_,
`CacheReader.cpp <lib/bcc/CacheReader.cpp>`_, and
`CacheWriter.cpp <lib/bcc/CacheWriter.cpp>`_ for details.



JIT'ed Code Calling Conventions
-------------------------------

1. Calls from Execution Environment or from/to within script:

   On ARM, the first 4 arguments will go into r0, r1, r2, and r3, in that order.
   The remaining (if any) will go through stack.

   For ext_vec_types such as float2, a set of registers will be used. In the case
   of float2, a register pair will be used. Specifically, if float2 is the first
   argument in the function prototype, float2.x will go into r0, and float2.y,
   r1.

   Note: stack will be aligned to the coarsest-grained argument. In the case of
   float2 above as an argument, parameter stack will be aligned to an 8-byte
   boundary (if the sizes of other arguments are no greater than 8.)

2. Calls from/to a separate compilation unit: (E.g., calls to Execution
   Environment if those runtime library callees are not compiled using LLVM.)

   On ARM, we use hardfp.  Note that double will be placed in a register pair.
