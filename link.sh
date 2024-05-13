#!/bin/bash

WEB=/tmp/sdk
mkdir -p $WEB


# client lib ( eg psycopg ) for websocketed pg server
emcc $CDEBUG -shared -o ${WEB}/libpgc.so \
     ./src/interfaces/libpq/libpq.a \
     ./src/port/libpgport.a \
     ./src/common/libpgcommon.a

file ${WEB}/libpgc.so

pushd src

    if $CI
    then
        PATCH="-DPATCH_MAIN=/home/runner/work/pglite-build/pglite-build/pg_main.c $PATCH"
        PATCH="-DPATCH_PLUGIN=/home/runner/work/pglite-build/pglite-build/pg_plugin.h  $PATCH"
    else
        PATCH="-DPATCH_MAIN=/data/git/pg/pg_main.c $PATCH"
        PATCH="-DPATCH_PLUGIN=/data/git/pg/pg_plugin.h  $PATCH"
    fi

    emcc -sFORCE_FILESYSTEM -DPG_INITDB_MAIN=1 -DPREFIX=${PREFIX} -Iinclude -Iinterfaces/libpq -c -o ../pg_initdb.o ./bin/initdb/initdb.c

    rm pg_initdb.o backend/main/main.o ./backend/tcop/postgres.o ./backend/utils/init/postinit.o

    #
    emcc -DPG_LINK_MAIN=1 -DPREFIX=${PREFIX} $PATCH \
     -Iinclude -Iinterfaces/libpq -c -o ./backend/tcop/postgres.o ./backend/tcop/postgres.c

    EMCC_CFLAGS="-DPREFIX=${PREFIX} -DPG_INITDB_MAIN=1 -DPATCH_PLUGIN=/data/git/pg/pg_plugin.h -DPATCH_MAIN=/data/git/pg/pg_main.c" emmake make backend/main/main.o backend/utils/init/postinit.o
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
 /data/git/pg/postgresql-16.2-wasm/pg_initdb.o"

PG_L="-L../../src/port -L../../src/common \
 ../../src/common/libpgcommon_srv.a ../../src/port/libpgport_srv.a"

if false
then
    echo "

        TODO !!
    missing : pg_strdup / pg_malloc / pg_realloc / fsync_pgdata


"
    PG_L="$PG_L ../../src/interfaces/ecpg/ecpglib/libecpg.a -sERROR_ON_UNDEFINED_SYMBOLS=0"
else
    PG_L="$PG_L -L../../src/interfaces/ecpg/ecpglib ../../src/interfaces/ecpg/ecpglib/libecpg.so"
fi



/opt/python-wasm-sdk/emsdk/upstream/emscripten/emcc -sLZ4=1 -sFORCE_FILESYSTEM -fPIC $CDEBUG -sMAIN_MODULE=1 \
 -D__PYDK__=1 -DPREFIX=${PREFIX} \
 --shell-file $GITHUB_WORKSPACE/repl.html \
 -sTOTAL_MEMORY=1GB -sSTACK_SIZE=4MB -sALLOW_TABLE_GROWTH -sALLOW_MEMORY_GROWTH -sGLOBAL_BASE=100MB \
-sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORTED_RUNTIME_METHODS=FS \
 --use-preload-plugins \
 --preload-file ${PREFIX}/share/postgresql@${PREFIX}/share/postgresql \
 --preload-file ${PREFIX}/lib@${PREFIX}/lib \
 --preload-file ${PREFIX}/password@${PREFIX}/password \
 --preload-file ${PREFIX}/bin/postgres@${PREFIX}/bin/postgres \
 --preload-file ${PREFIX}/bin/initdb@${PREFIX}/bin/initdb \
 -o index.html $PG_O $PG_L

mv -v index.* ${WEB}/

du -hs ${WEB}/index.*

popd
