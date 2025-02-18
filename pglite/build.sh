#!/bin/bash
echo "pglite/build: begin"

WORKSPACE=$(pwd)
PGROOT=/tmp/pglite

PGSRC=${WORKSPACE}
PGBUILD=${WORKSPACE}/build/postgres

LIBPGCORE=${PGBUILD}/libpgcore.a

WEBROOT=${PGBUILD}/web

PGINC="-I/tmp/pglite/include \
 -I${PGSRC}/src/include -I${PGSRC}/src/interfaces/libpq \
 -I${PGBUILD}/src/include"


if $WASI
then
    echo TODO


else
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


    if $DEBUG
    then
        LOPTS="-O2 -g3 --no-wasm-opt -sASSERTIONS=1"
        # FULL
        LINKER="-sMAIN_MODULE=1 -sEXPORTED_FUNCTIONS=_main,_use_wire,_ping"

    else
        LOPTS="-Oz -g0 --closure 1 -sASSERTIONS=0"
        # min
        # LINKER="-sMAIN_MODULE=2"

        # tailored
        LINKER="-sMAIN_MODULE=2 -sEXPORTED_FUNCTIONS=@exports"

    fi

    LOPTS="-O2 -g3"

    echo "

________________________________________________________

emscripten : $(which emcc ) $(cat ${SDKROOT}/VERSION)
python : $(which python3) $(python3 -V)
wasmtime : $(which wasmtime)

CC=${CC:-undefined}

Linking to libpgcore static from $LIBPGCORE

Folders :
    source : $PGSRC
     build : $PGBUILD
    target : $WEBROOT

    CPOPTS : $COPTS
    DEBUG  : $DEBUG
        LOPTS  : $LOPTS
    CMA_MB : $CMA_MB

 CC_PGLITE : $CC_PGLITE

$PGPRELOAD
________________________________________________________



"

    rm pglite.*

    mkdir -p $WEBROOT

#    ${CC} ${CC_PGLITE} -DPG_INITDB_MAIN \
#     ${PGINC} \
#     -o ${PGBUILD}/initdb.o -c ${PGSRC}/src/bin/initdb/initdb.c

    ${CC} ${CC_PGLITE} ${PGINC} -o ${PGBUILD}/pglite.o -c ${WORKSPACE}/pglite/pg_main.c


    COPTS="$LOPTS" ${CC} ${CC_PGLITE} -sGLOBAL_BASE=8388608 -o pglite.html -ferror-limit=1 --shell-file ${WORKSPACE}/pglite/repl.html \
     $PGPRELOAD \
     -sNO_EXIT_RUNTIME=1 -sFORCE_FILESYSTEM=1 -sENVIRONMENT=node,web \
     -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=Module \
         -sALLOW_TABLE_GROWTH -sALLOW_MEMORY_GROWTH -sERROR_ON_UNDEFINED_SYMBOLS \
         -sEXPORTED_RUNTIME_METHODS=FS,setValue,getValue,UTF8ToString,stringToNewUTF8,stringToUTF8OnStack \
     ${PGINC} ${PGBUILD}/pglite.o \
     $LINKER $LIBPGCORE -lnodefs.js -lidbfs.js -lxml2 -lz

fi

du -hs pglite.*

echo "pglite/build: end"
