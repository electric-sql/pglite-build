#!/bin/bash

export CI=${CI:-false}
export GITHUB_WORKSPACE=${GITHUB_WORKSPACE:-$(pwd)}
export PGROOT=${PGROOT:-/tmp/pglite}
export WEBROOT=${WEBROOT:-${GITHUB_WORKSPACE}/pglite/postgres}
export DEBUG=${DEBUG:-false}

PGDATA=${PGROOT}/base

PGUSER=postgres

export PGPASS=${PGPASS:-password}
export CRED="-U $PGUSER --pwfile=${PGROOT}/password"


# exit on error
EOE=false

# the default is a user writeable path.
if mkdir -p ${PGROOT}
then
    echo "checking for valid prefix ${PGROOT}"
else
    sudo mkdir -p ${PGROOT}
    sudo chown $(whoami) ${PGROOT}
fi

if [ -f ${PGROOT}/password ]
then
    echo "not changing db password"
else
    echo ${PGPASS:-password} > ${PGROOT}/password
fi


# default to web/release size optim.
if $DEBUG
then
    echo "debug not support on web build"
    exit 80
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

    # store all pg options that have impact on cmd line initdb/boot
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

    . cibuild-pg162.sh

    rm ${GITHUB_WORKSPACE}/postgresql 2>/dev/null
    # to get wasm-shared link tool in the path for extensions building.
    ln -s ${GITHUB_WORKSPACE}/postgresql-16.2 ${GITHUB_WORKSPACE}/postgresql
fi

export PATH=${GITHUB_WORKSPACE}/postgresql/bin:$PATH


if echo "$@"|grep pglite
then
    . cibuild-pglite.sh
fi



if echo "$@"|grep pgvector
then
    [ -d pgvector ] || git clone --no-tags --depth 1 --single-branch --branch master https://github.com/pgvector/pgvector
    pushd pgvector
    # path for wasm-shared already set to (pwd:pg source dir)/bin
    # OPTFLAGS="" turns off arch optim.
    PG_CONFIG=${PGROOT}/bin/pg_config emmake make OPTFLAGS="" install
    popd

fi


if echo "$@"|grep quack
then
    echo WIP
    PG_LINK=em++
fi


# run this last so all extensions files can be packaged

if echo "$@"|grep linkweb
then
    # build web version
    pushd postgresql
    . $GITHUB_WORKSPACE/link.sh

    # upload all to gh pages, including node archive
    if $CI
    then
        tar -cpRz ${PGROOT} > /tmp/sdk/pg.tar.gz
        mkdir -p /tmp/sdk/
        mv $WEBROOT/* /tmp/sdk/
    fi
    popd
fi






