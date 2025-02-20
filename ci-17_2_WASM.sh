#!/bin/bash
export WORKSPACE=$(pwd)
export PG_VERSION=${PG_VERSION:-REL_17_2_WASM}
export CONTAINER_PATH=${CONTAINER_PATH:-/tmp/fs}
#
#[ -f postgresql-${PG_VERSION}/configure ] \
# || git clone --no-tags --depth 1 --single-branch --branch ${PG_VERSION} https://github.com/pygame-web/postgres postgresql-${PG_VERSION}
#

[ -f postgresql-${PG_VERSION}/configure ] \
 || git clone --no-tags --depth 1 --single-branch --branch ${PG_VERSION} https://github.com/electric-sql/postgres-pglite postgresql-${PG_VERSION}

chmod +x portable/*.sh wasm-build/*.sh
cp -R wasm-build* extra patches-${PG_VERSION} postgresql-${PG_VERSION}/

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
        for archive in ${CONTAINER_PATH}/tmp/pglite/sdk/*.tar
        do
            echo "    packing $archive"
            gzip -f -9 $archive
        done


        if [ -d ${WORKSPACE}/pglite/packages/pglite ]
        then
            pushd ${WORKSPACE}/pglite
                #update
                mkdir -p packages/pglite/release
                mv -vf ${WORKSPACE}/postgresql-${PG_VERSION}/pglite.* ${CONTAINER_PATH}/tmp/pglite/sdk/*.tar.gz packages/pglite/release/
            popd
        else
            git clone https://github.com/electric-sql/pglite
        fi

        if $CI
        then
            pushd build/postgres
                tar -cpvRz libpgcore.a pglite.* > /tmp/sdk/libpglite-emsdk.tar.gz
            popd

            pushd pglite
                npm install -g pnpm vitest
            popd
        fi

        # when outside CI use emsdk node
        if [ -d /srv/www/html/pglite-web ]
        then
            . /opt/python-wasm-sdk/wasm32-bi-emscripten-shell.sh
        fi

        pushd pglite
            if pnpm run ts:build
            then
                pushd packages/pglite
                    pnpm vitest tests/basic.test.js
                popd
            fi
        popd


        if [ -d /srv/www/html/pglite-web ]
        then
            git restore src/test/Makefile src/test/isolation/Makefile

            # backup pglite workfiles
            [ -d pglite ] && cp -Rv pglite/* ${WORKSPACE}/pglite-${PG_VERSION}/

            # use released files for test
            cp -vf ${WORKSPACE}/pglite/packages/pglite/release/* /srv/www/html/pglite-web/
        fi

    else
        echo failed to build libpgcore static
        exit 19
    fi
popd

