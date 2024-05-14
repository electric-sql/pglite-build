#!/bin/bash

export PREFIX=$PGROOT

WEB=/tmp/sdk
mkdir -p $WEB


# client lib ( eg psycopg ) for websocketed pg server
emcc $CDEBUG -shared -o ${WEB}/libpgc.so \
     ./src/interfaces/libpq/libpq.a \
     ./src/port/libpgport.a \
     ./src/common/libpgcommon.a

file ${WEB}/libpgc.so

pushd src

    PATCH="-DPATCH_MAIN=$GITHUB_WORKSPACE/pg_main.c $PATCH"
    PATCH="-DPATCH_PLUGIN=$GITHUB_WORKSPACE/pg_plugin.h  $PATCH"

    emcc -sFORCE_FILESYSTEM -DPG_INITDB_MAIN=1 -DPREFIX=${PREFIX} -Iinclude -Iinterfaces/libpq -c -o ../pg_initdb.o ./bin/initdb/initdb.c

    rm pg_initdb.o backend/main/main.o ./backend/tcop/postgres.o ./backend/utils/init/postinit.o

    #
    emcc -DPG_LINK_MAIN=1 -DPREFIX=${PREFIX} $PATCH \
     -Iinclude -Iinterfaces/libpq -c -o ./backend/tcop/postgres.o ./backend/tcop/postgres.c

    EMCC_CFLAGS="-DPREFIX=${PREFIX} -DPG_INITDB_MAIN=1 -DPATCH_PLUGIN=$GITHUB_WORKSPACE/pg_plugin.h -DPATCH_MAIN=$GITHUB_WORKSPACE/pg_main.c" emmake make backend/main/main.o backend/utils/init/postinit.o
popd


echo "========================================================"
echo -DPREFIX=${PREFIX} $PATCH
file pg_initdb.o src/backend/main/main.o src/backend/tcop/postgres.o src/backend/utils/init/postinit.o
echo "========================================================"


pushd src/backend

# https://github.com/emscripten-core/emscripten/issues/12167
# --localize-hidden
# https://github.com/llvm/llvm-project/issues/50623


cp -vf ../../src/interfaces/ecpg/ecpglib/libecpg.so ${WEB}/


echo " ---------- building web test PREFIX=$PREFIX ------------"
du -hs ${WEB}/libpg?.*

PG_O="../../src/fe_utils/string_utils.o ../../src/common/logging.o \
 $(find . -type f -name "*.o" \
    | grep -v ./utils/mb/conversion_procs \
    | grep -v ./replication/pgoutput \
    | grep -v  src/bin/ \
    | grep -v ./snowball/dict_snowball.o ) \
 ../../src/timezone/localtime.o \
 ../../src/timezone/pgtz.o \
 ../../src/timezone/strftime.o \
 ../../pg_initdb.o"

PG_L="-L../../src/port -L../../src/common \
 ../../src/common/libpgcommon_srv.a ../../src/port/libpgport_srv.a"

PG_L="$PG_L -L../../src/interfaces/ecpg/ecpglib ../../src/interfaces/ecpg/ecpglib/libecpg.so"


# ? -sLZ4=1  -sENVIRONMENT=web
EMCC_WEB="-sNO_EXIT_RUNTIME=1 -sFORCE_FILESYSTEM=1  --shell-file $GITHUB_WORKSPACE/repl.html"

/opt/python-wasm-sdk/emsdk/upstream/emscripten/emcc  $EMCC_WEB -sFORCE_FILESYSTEM -fPIC $CDEBUG -sMAIN_MODULE=1 \
 -D__PYDK__=1 -DPREFIX=${PREFIX} \
 -sTOTAL_MEMORY=1GB -sSTACK_SIZE=4MB -sALLOW_TABLE_GROWTH -sALLOW_MEMORY_GROWTH -sGLOBAL_BASE=100MB \
 -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=Module -sEXPORTED_RUNTIME_METHODS=FS \
 -sEXPORTED_FUNCTIONS=_main \
 --use-preload-plugins \
 --preload-file ${PREFIX}/share/postgresql@${PREFIX}/share/postgresql \
 --preload-file ${PREFIX}/lib@${PREFIX}/lib \
 --preload-file ${PREFIX}/password@${PREFIX}/password \
 --preload-file ${PREFIX}/bin/postgres@${PREFIX}/bin/postgres \
 --preload-file ${PREFIX}/bin/initdb@${PREFIX}/bin/initdb \
 -o postgres.html $PG_O $PG_L

mv postgres.html index.html
mv postgres.* index.html $GITHUB_WORKSPACE/vtx.js ${WEB}/

du -hs ${WEB}/postgres.*

popd
