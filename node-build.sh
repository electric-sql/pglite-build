#!/bin/bash

# TODO: add wasm sdk download when not in git CI

ARCHIVE=postgresql-16.2.tar.bz2


[ -f ${ARCHIVE} ] || wget -q -c https://ftp.postgresql.org/pub/source/v16.2/${ARCHIVE}

tar xfj ${ARCHIVE}

cd postgresql-16.2
> ./src/template/emscripten
> ./src/include/port/emscripten.h
> ./src/makefiles/Makefile.emscripten

cat ../postgresql-16.2-wasm.patchset/*diff | patch -p1

DEBUG=false PREFIX=/tmp/pglite ../wasm-build.sh $@


