#!/bin/bash
WORKSPACE=$(pwd)

PGSRC=${WORKSPACE}
PGROOT=${WORKSPACE}/build/postgres

LIBPGLITE=${PGROOT}/libpglite.a

WEBROOT=${PGROOT}/web

. ${SDKROOT:-/opt/python-wasm-sdk}/wasm32-bi-emscripten-shell.sh

export CC=$(which emcc)
echo "

________________________________________________________

emscripten : $(which emcc ) $(cat ${SDKROOT}/VERSION)
python : $(which python3) $(python3 -V)
wasmtime : $(which wasmtime)

CC=${CC:-undefined}

Linking to libpglite static from $LIBPGLITE

Folders :
    source : $PGSRC
     build : $PGROOT
    target : $WEBROOT

________________________________________________________



"

# /workspace/src/include/postgres_fe.h
# /workspace/build/postgres/src/include/pg_config_ext.h
# /workspace/src/interfaces/libpq/pqexpbuffer.h

mkdir -p $WEBROOT
${CC} \
 -I/tmp/pglite/include \
 -I${PGSRC}/src/include -I${PGSRC}/src/interfaces/libpq \
 -I${PGROOT}/src/include \
 -o ${PGROOT}/initdb.o -c ${PGSRC}/src/bin/initdb/initdb.c




