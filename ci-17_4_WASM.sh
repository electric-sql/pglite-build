#!/bin/bash
export WORKSPACE=$(pwd)
export PG_VERSION=${PG_VERSION:-17.4}
export PG_BRANCH=${PG_BRANCH:-REL_17_4_WASM}
export CONTAINER_PATH=${CONTAINER_PATH:-/tmp/fs}
export DEBUG=${DEBUG:-false}
export USE_ICU=${USE_ICU:-false}



PG_DIST_EXT="${WORKSPACE}/postgresql/dist/extensions-emsdk"
PG_DIST_PGLITE="${WORKSPACE}/postgresql/dist/pglite-sandbox"

# for local testing
if [ -d export PG_DIST_WEB="/srv/www/html/pglite-web" ]
then
    export PG_DIST_WEB="/srv/www/html/pglite-web"
    export LOCAL=true
else
    export PG_DIST_WEB="${WORKSPACE}/postgresql/dist/web"
    export LOCAL=false
fi


#
#[ -f postgresql-${PG_BRANCH}/configure ] \
# || git clone --no-tags --depth 1 --single-branch --branch ${PG_BRANCH} https://github.com/pygame-web/postgres postgresql-${PG_BRANCH}
#

[ -f postgresql-${PG_BRANCH}/configure ] \
 || git clone --no-tags --depth 1 --single-branch --branch ${PG_BRANCH} https://github.com/electric-sql/postgres-pglite postgresql-${PG_BRANCH}

chmod +x portable/*.sh wasm-build/*.sh
cp -R wasm-build* extra patches-${PG_BRANCH} postgresql-${PG_BRANCH}/

if [ -d postgresql-${PG_BRANCH}/pglite-wasm ]
then
    echo "using local pglite files"
else
    mkdir -p postgresql-${PG_BRANCH}/pglite-wasm
    cp -Rv pglite-${PG_BRANCH}/* postgresql-${PG_BRANCH}/pglite-wasm/
fi

pushd postgresql-${PG_BRANCH}

    cat > $CONTAINER_PATH/portable.opts <<END
export DEBUG=${DEBUG}
export USE_ICU=${USE_ICU}
END

    ${WORKSPACE}/portable/portable.sh

    if [ -f build/postgres/libpgcore.a ]
    then
        for archive in ${PG_DIST_EXT}/*.tar
        do
            echo "    packing $archive"
            gzip -f -9 $archive
        done

        echo "

    *   preparing TS build assets

"

        if [ -d ${WORKSPACE}/pglite/packages/pglite ]
        then
            pushd ${WORKSPACE}/pglite
                # clean
                [ -d packages/pglite/release ] && rm packages/pglite/release/* packages/pglite/dist/* packages/pglite/dist/*/*

                # be safe
                mkdir -p packages/pglite/release packages/pglite/dist

                rmdir packages/pglite/dist/*

                #update
                mv -vf ${WORKSPACE}/postgresql-${PG_BRANCH}/pglite.* packages/pglite/release/
                mv -vf ${PG_DIST_EXT}/*.tar.gz packages/pglite/release/
            popd
        else
            git clone --no-tags --depth 1 --single-branch --branch pmp-p/pglite-build17 https://github.com/electric-sql/pglite pglite
        fi

        read

        # when outside CI use emsdk node
        if [ -d /srv/www/html/pglite-web ]
        then
            . /opt/python-wasm-sdk/wasm32-bi-emscripten-shell.sh
        fi

        echo "
    *   compiling TS

"
        if $CI
        then
            pushd build/postgres
                echo "# packing dev files to /tmp/sdk/libpglite-emsdk.tar.gz"
                tar -cpRz libpgcore.a pglite.* > /tmp/sdk/libpglite-emsdk.tar.gz
            popd

            pushd pglite
                npm install -g pnpm vitest
                pnpm install
            popd
        fi


        pushd pglite
            if pnpm run ts:build
            then
                pushd packages/pglite
                    if $CI
                    then
                        pnpm vitest tests/basic.test.js || exit 99
                    fi
                popd
            fi
        popd


        mkdir -p ${PG_DIST_WEB}
        if [ -d ${PG_DIST_WEB} ]
        then
            git restore src/test/Makefile src/test/isolation/Makefile

            echo "# use released files for web test"
            mkdir -p ${PG_DIST_WEB}/examples
            cp -r ${WORKSPACE}/pglite/packages/pglite/dist ${PG_DIST_WEB}/
            cp ${WORKSPACE}/pglite/packages/pglite/examples/{styles.css,utils.js} ${PG_DIST_WEB}/examples/
            cp -f ${WORKSPACE}/pglite/packages/pglite/release/* ${PG_DIST_WEB}/
            du -hs ${PG_DIST_WEB}/pglite.*

            if $LOCAL
            then
                echo "# backup pglite workfiles"
                [ -d pglite-wasm ] && cp -R pglite-wasm/* ${WORKSPACE}/pglite-${PG_BRANCH}/
            else
                mv ${PG_DIST_WEB} /tmp/web
            fi
        fi

        ./runtests.sh


    else
        echo failed to build libpgcore static
        exit 125
    fi
popd

