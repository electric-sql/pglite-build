#!/bin/bash

export CI=${CI:-false}
export GITHUB_WORKSPACE=${GITHUB_WORKSPACE:-$(pwd)}
export PGROOT=${PGROOT:-/tmp/pglite}
export WEBROOT=${WEBROOT:-${GITHUB_WORKSPACE}/pglite/postgres}
export DEBUG=${DEBUG:-false}

PGDATA=${PGROOT}/base

PGUSER=postgres

# warnings for indexes
PGFIXES="-d 1 -P -B 16 -S 512 -f siobtnmh"

PGFIXES="-d 1 -B 16 -S 512 -f siobtnmh"


# --data-checksums needs a value
CKSUM_B=""
CKSUM_S=""


CRED="-U $PGUSER --pwfile=${PGROOT}/password"

# exit on error
EOE=false

# -c wal_segment_size=16MB

mkdir -p ${PGROOT}

cat > ${PGROOT}/pgopts.sh <<END
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

. ${PGROOT}/pgopts.sh



# make sure no non-mvp feature gets in.

cat > ${PGROOT}/config.site <<END
pgac_cv_sse42_crc32_intrinsics_=no
pgac_cv_sse42_crc32_intrinsics__msse4_2=no
pgac_sse42_crc32_intrinsics=no
pgac_armv8_crc32c_intrinsics=no
ac_cv_search_sem_open=no
END



# workaround no "locale -a" for Node.

cat > ${PGROOT}/locale <<END
C
C.UTF-8
POSIX
UTF-8
END



# default to web/release
if $DEBUG
then
    echo "debug not support on web build"
    exit 77
else
    export PGDEBUG=""
    export CDEBUG="-g0 -Os"
fi

# setup compiler+node
. /opt/python-wasm-sdk/wasm32-bi-emscripten-shell.sh


if [ -f ${WEBROOT}/postgres.js ]
then
    echo using current from ${WEBROOT}
else
    . cibuild-pg162.sh
fi

if echo "$@"|grep pglite
then
    if [ -d pglite ]
    then
        echo using local
    else
        git clone --no-tags --depth 1 --single-branch --branch pglite-build https://github.com/electric-sql/pglite pglite
    fi

    pushd pglite/packages/pglite
    npm install
    npm run build
    popd

    #> pglite/packages/pglite/release/postgres.js
    cp ${WEBROOT}/postgres.{js,data,wasm} pglite/packages/pglite/release/
    cp ${WEBROOT}/libecpg.so pglite/packages/pglite/release/postgres.so
    mv pglite/packages/pglite/release/postgres.js pglite/packages/pglite/release/pgbuild.js

    cat > pglite/packages/pglite/release/share.js <<END

    function loadPgShare(module, require) {
        console.warn("share.js: loadPgShare");
    }

    export default loadPgShare;
END

    cat pgbuild.js > pglite/packages/pglite/release/postgres.js

fi
