#!/bin/bash
export WORKSPACE=$(pwd)
export PG_VERSION=${PG_VERSION:-REL_17_2_WASM}

#
#[ -f postgresql-${PG_VERSION}/configure ] \
# || git clone --no-tags --depth 1 --single-branch --branch ${PG_VERSION} https://github.com/pygame-web/postgres postgresql-${PG_VERSION}
#

[ -f postgresql-${PG_VERSION}/configure ] \
 || git clone --no-tags --depth 1 --single-branch --branch ${PG_VERSION} https://github.com/electric-sql/postgres-pglite postgresql-${PG_VERSION}

chmod +x portable/*.sh wasm-build/*.sh
cp -R wasm-build* patches-${PG_VERSION} postgresql-${PG_VERSION}/

if [ -d postgresql-${PG_VERSION}/pglite ]
then
    echo "using local pglite files"
else
    mkdir -p postgresql-${PG_VERSION}/pglite
    cp -Rv pglite-${PG_VERSION}/* postgresql-${PG_VERSION}/pglite/
fi

pushd postgresql-${PG_VERSION}
    ${WORKSPACE}/portable/portable.sh
    if [ -f build/postgres/libpgcore.a ]
    then
        if $CI
        then
            pushd build/postgres
            tar -cpvRz libpgcore.a > /tmp/sdk/libpglite-emsdk.tar.gz
            popd
        fi


        # git restore src/test/Makefile src/test/isolation/Makefile

        # backup
        [ -d pglite ] && cp -Rv pglite ${WORKSPACE}/

    else
        echo failed to build libpgcore static
        exit 19
    fi
popd

