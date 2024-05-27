#!/bin/bash

export CI=${CI:-false}
export GITHUB_WORKSPACE=${GITHUB_WORKSPACE:-$(pwd)}
export PGROOT=${PGROOT:-/tmp/pglite}

if ${CI:-false}
then
    export WEBROOT=${GITHUB_WORKSPACE}/pglite/postgres
else
    export WEBROOT=${WEBROOT:-/srv/www/html/pygbag/pglite-web}
fi
export DEBUG=false

ARCHIVE=postgresql-16.2.tar.bz2

if [ -f ${WEBROOT}/postgres.js ]
then
    echo using current from ${WEBROOT}
else
    [ -f ${ARCHIVE} ] || wget -q -c https://ftp.postgresql.org/pub/source/v16.2/${ARCHIVE}

    tar xfj ${ARCHIVE}

    pushd postgresql-16.2
    > ./src/template/emscripten
    > ./src/include/port/emscripten.h
    > ./src/makefiles/Makefile.emscripten

    cat ../postgresql-16.2-wasm.patchset/*diff | patch -p1

    ../wasm-build.sh $@

    popd
fi


#> pglite/packages/pglite/release/postgres.js
cp ${WEBROOT}/postgres.{js,data,wasm} pglite/packages/pglite/release/
cp ${WEBROOT}/libecpg.so pglite/packages/pglite/release/pgbuild.so
mv pglite/packages/pglite/release/postgres.js pglite/packages/pglite/release/pgbuild.js

cat > pglite/packages/pglite/release/share.js <<END

function loadPgShare(module, require) {
    console.warn("share.js: loadPgShare");
}

export default loadPgShare;
END

cat pgbuild.js > pglite/packages/pglite/release/postgres.js

