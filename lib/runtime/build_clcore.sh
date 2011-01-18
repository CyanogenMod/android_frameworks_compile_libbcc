#!/bin/sh

# Generate rs_cl.bc
# =================
cp ../../../../base/libs/rs/scriptc/rs_cl.rsh rs_cl.h

sed -e 's/static/extern/' rs_cl.h > rs_cl_extern.h

rm rs_cl.h

clang -O3 rs_cl.c -emit-llvm -S -o rs_cl.ll

llvm-as rs_cl.ll

# Generate rs_core.bc
# ===================

cp ../../../../base/libs/rs/scriptc/rs_core.rsh rs_core.h

sed -e 's/extern/static/' rs_core.h > rs_core_extern.h

rm rs_core.h

clang -O3 rs_core.c -emit-llvm -S -o rs_core.ll

llvm-as rs_core.ll

# Link everything together
# ========================

llvm-link rs_cl.bc rs_core.bc -o libclcore.bc
