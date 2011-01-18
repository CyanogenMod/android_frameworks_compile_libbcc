#!/bin/sh

# Generate rs_cl.bc
# =================
cp ../../../../base/libs/rs/scriptc/rs_cl.rsh ./rs_cl_extern.h

s/extern/static/ in rs_cl_extern.h

clang -O3 rs_cl.c -emit-llvm -S -o rs_cl.ll 

llvm-as rs_cl.ll

# Generate rs_core.bc
# ===================

cp ../../../../base/libs/rs/scriptc/rs_core.rsh ./rs_core_extern.h

s/extern/static/ in rs_core_extern.h

clang -O3 rs_core.c -emit-llvm -S -o rs_core.ll 

llvm-as rs_core.ll

# Link everything together
# ========================

llvm-rs-link rs_cl.bc rs_core.bc -o libruntime.so
