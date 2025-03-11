#!/bin/bash

export PG_VERSION=${PG_VERSION:-17.4}

#set -x;
#set -e;
export LC_ALL=C

export CI=${CI:-false}
export PORTABLE=${PORTABLE:-$(pwd)/wasm-build}
export SDKROOT=${SDKROOT:-/tmp/sdk}


# data transfer zone this is == (wire query size + result size ) + 2
# expressed in EMSDK MB, max is 13MB on emsdk 3.1.74+
export CMA_MB=${CMA_MB:-12}
export TOTAL_MEMORY=${TOTAL_MEMORY:-180MB}
export WASI=${WASI:-false}

export WORKSPACE=${GITHUB_WORKSPACE:-$(pwd)}
export PGROOT=${PGROOT:-/tmp/pglite}
export WEBROOT=${WEBROOT:-/tmp/web}
export DIST=${DIST:-/tmp/sdk}
export DEBUG=${DEBUG:-true}

export USE_ICU=${USE_ICU:-false}
export PGUSER=${PGUSER:-postgres}

[ -f /portable.opts ] && . /portable.opts


if $DEBUG
then
    export COPTS=${COPTS:-"-O2 -g3"}
    export LOPTS=${LOPTS:-"-O2 -g3 --no-wasm-opt -sASSERTIONS=1"}
else
    export COPTS=${COPTS:-"-Oz -g0"}
    export LOPTS=${LOPTS:-'-Oz -g0 --closure=0 --closure-args=--externs=/tmp/externs.js -sASSERTIONS=0'}
fi

export PGDATA=${PGROOT}/base
export PGPATCH=${WORKSPACE}/patches

chmod +x ${PORTABLE}/*.sh ${PORTABLE}/extra/*.sh

# exit on error
EOE=false

if ${PORTABLE}/sdk.sh
then
    echo "$PORTABLE : sdk check passed (emscripten+wasi)"
else
    echo sdk failed
    exit 40
fi


# default to a user writeable path.
if mkdir -p ${DIST} ${PGROOT}
then
    echo "checking for valid prefix ${PGROOT}"
else
    sudo mkdir -p ${PGROOT} ${PGROOT}/bin ${DIST}
    sudo chown $(whoami) -R ${PGROOT} ${DIST}
fi

# TODO: also handle PGPASSFILE hostname:port:database:username:password
# https://www.postgresql.org/docs/devel/libpq-pgpass.html
export CRED="-U $PGUSER --pwfile=${PGROOT}/password"

if [ -f ${PGROOT}/password ]
then
    echo "not changing db password"
    PGPASS=$(cat ${PGROOT}/password)
else
    PGPASS=${PGPASS:-password}
    echo ${PGPASS:-password} > ${PGROOT}/password
fi

export PGPASS


export PG_DEBUG_HEADER="${PGROOT}/include/pg_debug.h"


echo "
System node/pnpm ( may interfer) :

        node : $(which node) $(which node && $(which node) -v)
        PNPM : $(which pnpm)


"



# setup compiler+node. emsdk provides node 20, recent enough for bun.
# TODO: but may need to adjust $PATH with stock emsdk.

if ${WASI}
then
    echo "Wasi build (experimental)"
    export WASI_SDK=25.0
    export WASI_SDK_PREFIX=${SDKROOT}/wasisdk/wasi-sdk-${WASI_SDK}-x86_64-linux
    export WASI_SYSROOT=${WASI_SDK_PREFIX}/share/wasi-sysroot

    if [ -f ${WASI_SYSROOT}/extra ]
    then
        echo -n
    else
        pushd ${WASI_SYSROOT}
            VMLABS="https://github.com/vmware-labs/webassembly-language-runtimes/releases/download"
            wget -q "${VMLABS}/libs%2Flibpng%2F1.6.39%2B20230629-ccb4cb0/libpng-1.6.39-wasi-sdk-20.0.tar.gz" -O-| tar xfz -
            wget -q "${VMLABS}/libs%2Fzlib%2F1.2.13%2B20230623-2993864/libz-1.2.13-wasi-sdk-20.0.tar.gz"  -O-| tar xfz -
            wget -q "${VMLABS}/libs%2Fsqlite%2F3.42.0%2B20230623-2993864/libsqlite-3.42.0-wasi-sdk-20.0.tar.gz" -O-| tar xfz -
            wget -q "${VMLABS}/libs%2Flibxml2%2F2.11.4%2B20230623-2993864/libxml2-2.11.4-wasi-sdk-20.0.tar.gz" -O-| tar xfz -
            wget -q "${VMLABS}/libs%2Fbzip2%2F1.0.8%2B20230623-2993864/libbzip2-1.0.8-wasi-sdk-20.0.tar.gz"  -O-| tar xfz -
            wget -q "${VMLABS}/libs%2Flibuuid%2F1.0.3%2B20230623-2993864/libuuid-1.0.3-wasi-sdk-20.0.tar.gz" -O-| tar xfz -
        popd
        touch ${WASI_SYSROOT}/extra
    fi


    if false
    then
        . ${SDKROOT}/wasisdk/wasisdk_env.sh
        env|grep WASI
        export CC=${WASI_SDK_DIR}/bin/clang
        export CPP=${WASI_SDK_DIR}/bin/clang-cpp
        export CXX=${WASI_SDK_DIR}/bin/clang++
        export CFLAGS="-D_WASI_EMULATED_SIGNAL"
        export LDFLAGS="-lwasi-emulated-signal"
    else
        . ${SDKROOT}/wasm32-wasi-shell.sh
    fi

    # wasi does not use -sGLOBAL_BASE
    CC_PGLITE="-DCMA_MB=${CMA_MB}"

else
    if which emcc
    then
        echo "emcc found in PATH=$PATH"
    else
        . ${SDKROOT}/wasm32-bi-emscripten-shell.sh
    fi
    export PG_LINK=${PG_LINK:-$(which emcc)}

    echo "

    Using provided emsdk from $(which emcc)
    Using PG_LINK=$PG_LINK as linker

        node : $(which node) $($(which node) -v)
        PNPM : $(which pnpm)


"

    # custom code for node/web builds that modify pg main/tools behaviour
    # this used by both node/linkweb build stages

    # pass the "kernel" contiguous memory zone size to the C compiler.
    CC_PGLITE="-DCMA_MB=${CMA_MB}"

fi

# also used for non make (linking and pgl_main)
export CC_PGLITE="-DPYDK=1 -DPG_PREFIX=${PGROOT} -I${PGROOT}/include ${CC_PGLITE}"


export PGPRELOAD="\
--preload-file ${PGROOT}/share/postgresql@${PGROOT}/share/postgresql \
--preload-file ${PGROOT}/lib/postgresql@${PGROOT}/lib/postgresql \
--preload-file ${PGROOT}/password@${PGROOT}/password \
--preload-file ${PGROOT}/PGPASSFILE@/home/web_user/.pgpass \
--preload-file placeholder@${PGROOT}/bin/postgres \
--preload-file placeholder@${PGROOT}/bin/initdb\
"

# ========================= symbol extractor ============================

OBJDUMP=${OBJDUMP:-true}

if $OBJDUMP
then
    if [ -f $PGROOT/bin/wasm-objdump ]
    then
        echo "wasm-objdump found"
    else
        WRAPPER=$(which wasm-objdump)
        WASIFILE=$(realpath ${WRAPPER}.wasi)
        if $WRAPPER -h $WASIFILE | grep -q 'file format wasm 0x1'
        then
            mkdir -p $PGROOT/bin/
            if cp -f $WRAPPER $WASIFILE $PGROOT/bin/
            then
                echo "wasm-objdump found and working, and copied to $PGROOT/bin/"
            else
                OBJDUMP=false
            fi
        else
            echo "
        ERROR: $(which wasm-objdump) is not working properly ( is wasmtime ok ? )

    "
            OBJDUMP=false
        fi
    fi
else
    echo "

    WARNING: OBJDUMP disabled, some newer or complex extensions may not load properly


"
fi

if $OBJDUMP
then
    mkdir -p patches/imports patches/imports.pgcore
else
    mkdir -p patches/imports
    touch patches/imports/plpgsql
    echo "

    WARNING:    wasm-objdump not found or OBJDUMP disabled, some extensions may not load properly


"
fi

export OBJDUMP


# ========================= pg core configuration ============================

# testing postgres.js file instead of ${PGROOT}/pgopts.sh because build should not have failed.
if [ -f ${WEBROOT}/postgres.js ]
then
    echo using current from ${WEBROOT}

    . ${PGROOT}/pgopts.sh

else

    # default to web/release size optim.

    mkdir -p ${PGROOT}/include
    if $DEBUG
    then
        export PGDEBUG=""
        cat > ${PG_DEBUG_HEADER} << END
#ifndef I_PGDEBUG
#define I_PGDEBUG
#define WASM_USERNAME "$PGUSER"
#define PGDEBUG 1
#define PDEBUG(string) fputs(string, stdout)
#define JSDEBUG(string) {EM_ASM({ console.log(string); });}
#define ADEBUG(string) { PDEBUG(string); JSDEBUG(string) }
#endif
END

    else
        export PGDEBUG=""
        cat > ${PG_DEBUG_HEADER} << END
#ifndef I_PGDEBUG
#define I_PGDEBUG
#define WASM_USERNAME "$PGUSER"
#define PDEBUG(string)
#define JSDEBUG(string)
#define ADEBUG(string)
#define PGDEBUG 0
#endif
END
    fi

    mkdir -p ${PGROOT}/include/postgresql/server
    for dest in ${PGROOT}/include ${PGROOT}/include/postgresql ${PGROOT}/include/postgresql/server
    do
        [ -f $dest/pg_debug.h ] || cp ${PG_DEBUG_HEADER} $dest/
    done

    # store all pg options that have impact on cmd line initdb/boot
    cat > ${PGROOT}/pgopts.sh <<END
export COPTS="$COPTS"
export LOPTS="$LOPTS"
export PGDEBUG="$PGDEBUG"
export PG_DEBUG_HEADER=$PG_DEBUG_HEADER
export PGOPTS="\\
 -c log_checkpoints=false \\
 -c dynamic_shared_memory_type=posix \\
 -c search_path=pg_catalog \\
 -c exit_on_error=$EOE \\
 -c ignore_invalid_pages=on \\
 -c temp_buffers=8MB -c work_mem=4MB \\
 -c fsync=on -c synchronous_commit=on \\
 -c wal_buffers=4MB -c min_wal_size=80MB \\
 -c shared_buffers=128MB"
END

    export PGLITE=$(pwd)/packages/pglite

    echo "export PGSRC=${WORKSPACE}" >> ${PGROOT}/pgopts.sh
    echo "export PGLITE=${PGLITE}" >> ${PGROOT}/pgopts.sh


    . ${PGROOT}/pgopts.sh

    # make sure no non-mvp feature gets in.
    cat > ${PGROOT}/config.site <<END
pgac_cv_sse42_crc32_intrinsics_=no
pgac_cv_sse42_crc32_intrinsics__msse4_2=no
pgac_sse42_crc32_intrinsics=no
pgac_armv8_crc32c_intrinsics=no
ac_cv_search_sem_open=no

with_uuid=ossp
ac_cv_lib_ossp_uuid_uuid_export=yes
ac_cv_lib_uuid_uuid_generate=no
END


    # workaround no "locale -a" for Node.
    # this is simply the minimal result a popen call would give.
    mkdir -p ${PGROOT}/etc/postgresql
    cat > ${PGROOT}/etc/postgresql/locale <<END
C
C.UTF-8
POSIX
UTF-8
END

    . ${PORTABLE}/build-pgcore.sh
fi

# put local zic in the path from build dir
# put emsdk-shared and also pg_config from the install dir.
export PATH=${WORKSPACE}/build/postgres/bin:${PGROOT}/bin:$PATH


# At this stage, PG should be installed to PREFIX and ready for linking
# or building ext.



# ===========================================================================
# ===========================================================================
#                             EXTENSIONS
# ===========================================================================
# ===========================================================================

if echo " $*"|grep -q " contrib"
then

    if $WASI
    then
        echo " ========= TODO WASI openssl ============== "
        SKIP="\
 [\
 sslinfo bool_plperl hstore_plperl hstore_plpython jsonb_plperl jsonb_plpython\
 ltree_plpython sepgsql bool_plperl start-scripts\
 pgcrypto uuid-ossp xml2\
 ]"
    else
        # TEMP FIX for SDK
        SSL_INCDIR=$EMSDK/upstream/emscripten/cache/sysroot/include/openssl
        [ -f $SSL_INCDIR/evp.h ] || ln -s $PREFIX/include/openssl $SSL_INCDIR
        SKIP="\
 [\
 sslinfo bool_plperl hstore_plperl hstore_plpython jsonb_plperl jsonb_plpython\
 ltree_plpython sepgsql bool_plperl start-scripts\
 ]"
    fi

    for extdir in postgresql/contrib/*
    do
        if [ -f ${PGROOT}/dump.vector ]
        then
            echo "

    *   NOT rebuilding extensions

"
            break
        fi

        if [ -d "$extdir" ]
        then
            ext=$(echo -n $extdir|cut -d/ -f3)
            if echo -n $SKIP|grep -q "$ext "
            then
                echo skipping extension $ext
            else
                echo "

        Building contrib extension : $ext : begin
"
                pushd build/postgres/contrib/$ext
                if PATH=$PREFIX/bin:$PATH emmake make install 2>&1 >/dev/null
                then
                    echo "
        Building contrib extension : $ext : end
"
                else
                    echo "

        Extension $ext from $extdir failed to build

"
                    exit 216
                fi
                popd

                python3 ${PORTABLE}/pack_extension.py 2>&1 >/dev/null

            fi
        fi
    done


fi

 if echo " $*"|grep -q " extra"
then
    for extra_ext in  ${EXTRA_EXT:-"vector"}
    do
        if $CI
        then
            #if [ -d $PREFIX/include/X11 ]
            if true
            then
                echo -n
            else
                # install EXTRA sdk
                . /etc/lsb-release
                DISTRIB="${DISTRIB_ID}-${DISTRIB_RELEASE}"
                CIVER=${CIVER:-$DISTRIB}
                SDK_URL=https://github.com/pygame-web/python-wasm-sdk-extra/releases/download/$SDK_VERSION/python3.13-emsdk-sdk-extra-${CIVER}.tar.lz4
                echo "Installing $SDK_URL"
                curl -sL --retry 5 $SDK_URL | tar xvP --use-compress-program=lz4 | pv -p -l -s 15000 >/dev/null
                chmod +x ./extra/*.sh
            fi
        fi
        echo "======================= ${extra_ext} : $(pwd) ==================="

        ./extra/${extra_ext}.sh || exit 400

        python3 ${PORTABLE}/pack_extension.py
    done
fi

# ===========================================================================
# ===========================================================================
#                               PGLite
# ===========================================================================
# ===========================================================================


# run this last so all extensions files can be packaged
# those include  *.control *.sql and *.so
# TODO: check if some versionned *.sql files can be omitted
# TODO: for bigger extensions than pgvector make separate packaging.

if echo " $*"|grep " node"
then
    echo "====================== node : $(pwd) ========================"
    mkdir -p /tmp/sdk/

    # remove versionned symlinks
    rm ${PGROOT}/lib/lib*.so.? 2>/dev/null

    if $WASI
    then
        tar -cpRz ${PGROOT} > /tmp/sdk/postgres-${PG_VERSION}-wasisdk.tar.gz
    else
        tar -cpRz ${PGROOT} > /tmp/sdk/postgres-${PG_VERSION}-emsdk.tar.gz
    fi

fi

# run linkweb after node build because it may remove some wasm .so used by node from fs
# as they don't need to be in MEMFS since they are fetched.

# include current pglite source for easy local rebuild with just npm run build:js.

if echo " $*"|grep " linkweb"
then

    # build web version
    echo "========== linkweb : $(pwd) =================="
    pushd build/postgres
        . ${PORTABLE}/linkweb.sh
    popd
fi


# pglite* also use web build files, so order them last.


while test $# -gt 0
do
    case "$1" in

        pglite) echo "=================== pglite : $(pwd) ======================="
            # TODO: SAMs NOTE - Not using this in GitHub action as it doesnt resolve pnpm correctly
            # replaced with pglite-prep and pglite-bundle-sdk

            pushd ${PGLITE}
                pnpm install --frozen-lockfile

                mkdir -p $PGLITE/release
                rm $PGLITE/release/* 2>/dev/null


                # copy packed extensions for dist
                echo "

__________________________ enabled extensions (dlfcn)_____________________________
"
    cp -vf ${WEBROOT}/*.tar.gz ${PGLITE}/release/ | wc -l
echo "
__________________________________________________________________________________
"

                # copy wasm web prebuilt artifacts to release folder
                # TODO: get them from web for nosdk systems.

                cp -vf ${WEBROOT}/postgres.{js,data,wasm} ${PGLITE}/release/

                # debug CI does not use pnpm/npm for building pg, so call the typescript build
                # part from here
                #pnpm --filter "pglite^..." build || exit 450
                pnpm run build:js || exit 476
                pnpm pack || exit 31
                packed=$(echo -n electric-sql-pglite-*.tgz)

                mv $packed /tmp/sdk/pg${PG_VERSION}-${packed}

                echo "<html>
                <body>
                    <ul>
                        <li><a href=./pglite/examples/repl.html>PGlite REPL (in-memory)</a></li>
                        <li><a href=./pglite/examples/repl-idb.html>PGlite REPL (indexedDB)</a></li>
                        <li><a href=./pglite/examples/notify.html>list/notify test</a></li>
                        <li><a href=./pglite/examples/index.html>All PGlite Examples</a></li>
                        <li><a href=./pglite/benchmark/index.html>Benchmarks</a> / <a href=./pglite/benchmark/rtt.html>RTT Benchmarks</a></li>
                        <li><a href=./postgres.html>Postgres xterm REPL</a></li>
                    </ul>
                </body>
                </html>" > ${WEBROOT}/index.html

            popd

            mkdir -p ${PGROOT}/sdk/packages/ /tmp/web/pglite /tmp/web/repl/
            cp -r $PGLITE ${PGROOT}/sdk/packages/

            #mkdir /tmp/web/repl/dist-webcomponent -p
            #cp -r ${WORKSPACE}/packages/pglite-repl/dist-webcomponent /tmp/web/repl/

            if $CI
            then
                tar -cpRz ${PGROOT} > /tmp/sdk/pglite-pg${PG_VERSION}.tar.gz

                # build sdk (node)
                cp /tmp/sdk/postgres-${PG_VERSION}-*.tar.gz ${WEBROOT}/

                # pglite (web)
                cp /tmp/sdk/pglite-pg${PG_VERSION}.tar.gz ${WEBROOT}/

            fi

            du -hs ${WEBROOT}/*
        ;;

        pglite-test) echo "================== pglite-test ========================="
            export PATH=$PATH:$(pwd)/node_modules/.bin
            pushd ./packages/pglite
            #npm install -g concurrently playwright ava http-server pg-protocol serve tinytar buffer async-mutex 2>&1 > /dev/null
            pnpm install --prefix . 2>&1 >/dev/null
            pnpm run build 2>&1 >/dev/null
            if pnpm exec playwright install --with-deps 2>&1 >/dev/null
            then
                pnpm --filter "pglite^..." test || exit 534
                pnpm test:web || pnpm test:web || pnpm test:web || exit 535
            else
                echo "failed to install web-test env"
                pnpm --filter "pglite^..." test || exit 538
            fi
            pnpm pack
            popd
        ;;

        pglite-prep) echo "==================== pglite-prep  =========================="
            mkdir -p $PGLITE/release
            #rm $PGLITE/release/*

            # copy packed extensions
            cp -vf ${WEBROOT}/*.tar.gz ${PGLITE}/release/ | wc -l
            cp -vf ${WEBROOT}/postgres.{js,data,wasm} $PGLITE/release/
        ;;

        pglite-bundle-interim) echo "================== pglite-bundle-interim ======================"
            tar -cpRz ${PGLITE}/release > /tmp/sdk/pglite-interim-${PG_VERSION}.tar.gz
        ;;

        demo-site) echo "==================== demo-site =========================="
            ${PORTABLE}/demo-site.sh
        ;;
    esac
    shift
done

if [ -f  ${WORKSPACE}/pglite-wasm/build.sh ]
then
    ${WORKSPACE}/pglite-wasm/build.sh
fi
