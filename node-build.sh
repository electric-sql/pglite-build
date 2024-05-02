#!/bin/bash

# TODO: add wasm sdk download when not in git CI

ARCHIVE=postgresql-16.2.tar.bz2


[ -f ${ARCHIVE} ] || wget -c https://ftp.postgresql.org/pub/source/v16.2/${ARCHIVE}

tar xvfj ${ARCHIVE}

cd postgresql-16.2
> ./src/template/emscripten
> ./src/include/port/emscripten.h
> ./src/makefiles/Makefile.emscripten

cat ../postgresql-16.2-wasm.patchset/*diff | patch -p1

DEBUG=true PREFIX=/tmp/pglite ../wasm-build.sh emsdk test


