#!/bin/bash

WEBROOT=${WEBROOT:-/tmp/sdk}
mkdir -p $WEBROOT


# client lib ( eg psycopg ) for websocketed pg server
emcc $CDEBUG -shared -o ${WEBROOT}/libpgc.so \
     ./src/interfaces/libpq/libpq.a \
     ./src/port/libpgport.a \
     ./src/common/libpgcommon.a

file ${WEBROOT}/libpgc.so

pushd src

    PATCH="-DPATCH_MAIN=$GITHUB_WORKSPACE/pg_main.c $PATCH"
    PATCH="-DPATCH_PLUGIN=$GITHUB_WORKSPACE/pg_plugin.h  $PATCH"

    emcc -sFORCE_FILESYSTEM -DPG_INITDB_MAIN=1 -DPREFIX=${PGROOT} -Iinclude -Iinterfaces/libpq -c -o ../pg_initdb.o ./bin/initdb/initdb.c

    rm pg_initdb.o backend/main/main.o ./backend/tcop/postgres.o ./backend/utils/init/postinit.o

    #
    emcc -DPG_LINK_MAIN=1 -DPREFIX=${PGROOT} $PATCH \
     -Iinclude -Iinterfaces/libpq -c -o ./backend/tcop/postgres.o ./backend/tcop/postgres.c

    EMCC_CFLAGS="-DPREFIX=${PGROOT} -DPG_INITDB_MAIN=1 -DPATCH_PLUGIN=$GITHUB_WORKSPACE/pg_plugin.h -DPATCH_MAIN=$GITHUB_WORKSPACE/pg_main.c" emmake make backend/main/main.o backend/utils/init/postinit.o
popd


echo "========================================================"
echo -DPREFIX=${PGROOT} $PATCH
file pg_initdb.o src/backend/main/main.o src/backend/tcop/postgres.o src/backend/utils/init/postinit.o
echo "========================================================"


pushd src/backend

# https://github.com/emscripten-core/emscripten/issues/12167
# --localize-hidden
# https://github.com/llvm/llvm-project/issues/50623


cp -vf ../../src/interfaces/ecpg/ecpglib/libecpg.so ${WEBROOT}/


echo " ---------- building web test PREFIX=$PGROOT ------------"
du -hs ${WEBROOT}/libpg?.*

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
# /tmp/pglite/lib/postgresql/libduckdb.so"


# ? -sLZ4=1  -sENVIRONMENT=web
# -sSINGLE_FILE  => Uncaught SyntaxError: Cannot use 'import.meta' outside a module (at postgres.html:1:6033)
# -sENVIRONMENT=web => XHR
EMCC_WEB="-sNO_EXIT_RUNTIME=1 -sFORCE_FILESYSTEM=1"

# classic
MODULE="-sINVOKE_RUN=0 --shell-file /data/git/pglite-build/repl-nomod.html"

MODULE="-sMODULARIZE=0 -sEXPORT_ES6=0 --shell-file /data/git/pglite-build/repl-nomod.html"

# es6
MODULE="-sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=Module --shell-file ${GITHUB_WORKSPACE}/repl.html"

# closure -sSIMPLE_OPTIMIZATION

emcc $EMCC_WEB -fPIC $CDEBUG -sMAIN_MODULE=1 \
 -D__PYDK__=1 -DPREFIX=${PGROOT} \
 -sTOTAL_MEMORY=1GB -sSTACK_SIZE=4MB -sALLOW_TABLE_GROWTH -sALLOW_MEMORY_GROWTH -sGLOBAL_BASE=100MB \
  $MODULE -sERROR_ON_UNDEFINED_SYMBOLS \
 -sEXPORTED_RUNTIME_METHODS=FS,setValue,getValue,stringToNewUTF8,stringToUTF8OnStack,ccall,cwrap \
 -sEXPORTED_FUNCTIONS=_main,_getenv,_setenv,_interactive_one,_interactive_write,_interactive_read \
 --preload-file ${PGROOT}/share/postgresql@${PGROOT}/share/postgresql \
 --preload-file ${PGROOT}/lib@${PGROOT}/lib \
 --preload-file ${PGROOT}/password@${PGROOT}/password \
 --preload-file ${PGROOT}/bin/postgres@${PGROOT}/bin/postgres \
 --preload-file ${PGROOT}/bin/initdb@${PGROOT}/bin/initdb \
 -o postgres.html $PG_O $PG_L

mv -v postgres.* ${WEBROOT}/



mv postgres.* index.html ${WEBROOT}/


#mv postgres.html index.html
echo "<html></html>" > index.html

cp $GITHUB_WORKSPACE/vtx.js ${WEBROOT}/
du -hs ${WEBROOT}/postgres.*

popd
