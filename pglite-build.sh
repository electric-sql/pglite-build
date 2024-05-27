#!/bin/bash

if [ -d pglite ]
then
    echo using local
else
    git clone --no-tags --depth 1 --single-branch --branch pglite-build https://github.com/electric-sql/pglite pglite
fi

. /opt/python-wasm-sdk/wasm32-bi-emscripten-shell.sh

cd pglite/packages/pglite
npm install
npm run build
