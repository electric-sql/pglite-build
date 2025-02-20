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
        for archive in /tmp/fs/tmp/pglite/sdk/*.tar
        do
            echo "    packing $archive"
            gzip -9 $archive
        done

        if $CI
        then
            pushd build/postgres
            tar -cpvRz libpgcore.a pglite.* > /tmp/sdk/libpglite-emsdk.tar.gz
            popd
        fi
        if [ -d /srv/www/html/pglite-web ]
        then
            git restore src/test/Makefile src/test/isolation/Makefile
            # backup pglite workfiles
            [ -d pglite ] && cp -Rv pglite/* ${WORKSPACE}/pglite-${PG_VERSION}/
            mv -vf /data/git/postgres-pglite/pgl-${PG_VERSION}/pglite.*  /srv/www/html/pglite-web/
            mv -vf /tmp/fs/tmp/pglite/sdk/*.tar.gz /srv/www/html/pglite-web/
        fi

    else
        echo failed to build libpgcore static
        exit 19
    fi
popd

