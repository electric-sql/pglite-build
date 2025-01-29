#!/bin/bash
WORKSPACE=$(pwd)
PGROOT=/tmp/pglite

PGSRC=${WORKSPACE}
PGBUILD=${WORKSPACE}/build/postgres

LIBPGLITE=${PGBUILD}/libpglite.a

WEBROOT=${PGBUILD}/web

. ${SDKROOT:-/opt/python-wasm-sdk}/wasm32-bi-emscripten-shell.sh

touch placeholder

export PGPRELOAD="\
--preload-file ${PGROOT}/share/postgresql@${PGROOT}/share/postgresql \
--preload-file ${PGROOT}/lib/postgresql@${PGROOT}/lib/postgresql \
--preload-file ${PGROOT}/password@${PGROOT}/password \
--preload-file ${PGROOT}/PGPASSFILE@/home/web_user/.pgpass \
--preload-file placeholder@${PGROOT}/bin/postgres \
--preload-file placeholder@${PGROOT}/bin/initdb\
"

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
     build : $PGBUILD
    target : $WEBROOT

    DEBUG  : $DEBUG

$PGPRELOAD
________________________________________________________



"
rm pglite.*

# /workspace/src/include/postgres_fe.h
# /workspace/build/postgres/src/include/pg_config_ext.h
# /workspace/src/interfaces/libpq/pqexpbuffer.h

mkdir -p $WEBROOT




INC="-I/tmp/pglite/include \
 -I${PGSRC}/src/include -I${PGSRC}/src/interfaces/libpq \
 -I${PGBUILD}/src/include \
"

# min
# LINKER="-sMAIN_MODULE=2"

# tailored
# LINKER="-sMAIN_MODULE=2 -sEXPORTED_FUNCTIONS=@exports"

# FULL
LINKER="-sMAIN_MODULE=1 -sEXPORTED_FUNCTIONS=_main"


${CC} -DPG_INITDB_MAIN \
 $INC \
 -o ${PGBUILD}/initdb.o -c ${PGSRC}/src/bin/initdb/initdb.c

# /workspace/src/include/postgres.h
# ${CC} \
# $INC \
# -o ${PGBUILD}/pgcore.o -c ${PGSRC}/src/backend/tcop/postgres.c

${CC} -DCMA_MB=32  \
 $INC \
 -o ${PGBUILD}/pglite.o -c ${WORKSPACE}/pglite/pg_main.c


if $DEBUG
then
    LOPTS="-O2 -g3 --no-wasm-opt -sASSERTIONS=1"
else
    LOPTS="-Oz -g0 --closure 1 -sASSERTIONS=0"
fi

COPTS="$LOPTS" ${CC} -o pglite.html -ferror-limit=1 --shell-file ${WORKSPACE}/pglite/repl.html \
 -DCMA_MB=32 \
 $PGPRELOAD \
 -sNO_EXIT_RUNTIME=1 -sFORCE_FILESYSTEM=1 -sENVIRONMENT=node,web \
 -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=Module \
     -sALLOW_TABLE_GROWTH -sALLOW_MEMORY_GROWTH -sERROR_ON_UNDEFINED_SYMBOLS \
     -sEXPORTED_RUNTIME_METHODS=FS,setValue,getValue,UTF8ToString,stringToNewUTF8,stringToUTF8OnStack \
 $INC ${PGBUILD}/pglite.o \
 $LINKER $LIBPGLITE -lnodefs.js -lidbfs.js -lxml2 -lz

du -hs pglite.*
