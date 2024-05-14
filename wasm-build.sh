#!/bin/bash
reset
export PREFIX=${PREFIX:-/tmp/pgdata}

# save prefix because wasm sdk may change it to sdk install prefix
export PGROOT=${PREFIX}
PGDATA=${PGROOT}/base

PGUSER=postgres

# warnings for indexes
PGFIXES="-d 1 -P -B 16 -S 512 -f siobtnmh"

PGFIXES="-d 1 -B 16 -S 512 -f siobtnmh"


# --data-checksums needs a value
CKSUM_B=""
CKSUM_S=""


CRED="-U $PGUSER --pwfile=${PREFIX}/password"

# exit on error
EOE=true

EOE=false

# -c wal_segment_size=16MB

mkdir -p ${PREFIX}

cat > ${PREFIX}/pgopts.sh <<END
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

. ${PREFIX}/pgopts.sh

# default
# 15.6 : 20d4b68722f06f9a93ee663f230efe2a
# 16.2 : f68ec46a43a15b0744c8928dbd52107e
#    The database cluster will be initialized with this locale configuration:
#      provider:    libc
#      LC_COLLATE:  C
#      LC_CTYPE:    C.UTF-8
#      LC_MESSAGES: C
#      LC_MONETARY: C
#      LC_NUMERIC:  C
#      LC_TIME:     C
#    The default text search configuration will be set to "english".
LANG="-E UTF8 --locale=C.UTF-8 --locale-provider=libc"


echo "

        PREFIX=$PREFIX
        PGDATA=$PGDATA

TODO:
    handle symlinks for initdb --waldir=${PREFIX}/wal to $PGDATA/pg_wal


"

if echo "$@"|grep -q clean
then
	make clean
	rm $(find |grep \\.js$) $(find |grep \\.wasm$)
	if echo "$@"|grep distclean
	then
        make distclean
	fi
    exit 0
fi


# for upstreaming CI, on hold for now. 16.2 is priority
if echo "$@"|grep -q patchwork
then
    echo "


        applying patchwork from https://github.com/pmp-p/postgres-patchwork/issues?q=is%3Aissue+is%3Aopen+label%3Apatch


"
    wget -O- https://patch-diff.githubusercontent.com/raw/pmp-p/postgres-patchwork/pull/2.diff | patch -p1
    wget -O- https://patch-diff.githubusercontent.com/raw/pmp-p/postgres-patchwork/pull/5.diff | patch -p1
    wget -O- https://patch-diff.githubusercontent.com/raw/pmp-p/postgres-patchwork/pull/7.diff | patch -p1
    sudo mkdir ${PREFIX}
    sudo chmod 777 ${PREFIX}
    exit 0
fi


mkdir -p ${PREFIX}

# make sure no non-mvp feature gets in.

cat > ${PREFIX}/config.site <<END
pgac_cv_sse42_crc32_intrinsics_=no
pgac_cv_sse42_crc32_intrinsics__msse4_2=no
pgac_sse42_crc32_intrinsics=no
pgac_armv8_crc32c_intrinsics=no
ac_cv_search_sem_open=no
ac_cv_file__dev_urandom=no
END


# workaround no "locale -a" for Node.

cat > ${PREFIX}/locale <<END
C
C.UTF-8
POSIX
UTF-8
END


if ${DEBUG:-true}
then
    DEBUG="--enable-debug"
    CDEBUG="-g3 -O0"
else
    DEBUG=""
    CDEBUG="-g0 -Os"
fi

export CDEBUG

#  --with-wal-blocksize=1 --with-segsize=1 --with-segsize-blocks=0 --with-blocksize=4 \

CNF="./configure --prefix=${PREFIX} \
 --disable-spinlocks --disable-atomics \
 --without-zlib --disable-largefile --without-llvm \
 --without-pam --disable-largefile --without-zlib --with-openssl=no \
 --without-readline --without-icu \
 ${DEBUG}"



if echo "$@" |grep native
then

	make distclean
	echo "    ======== building host native , Debug = ${DEBUG} ===========   "
	if CONFIG_SITE=${PREFIX}/config.site CC="clang" CXX="clang++" CFLAGS="-m32" $CNF --with-template=linux
    then
        sed -i 's|-sMAIN_MODULE=1||g' src/backend/Makefile
        make -j $(nproc) && make install
    fi

	if pushd ${PREFIX}
	then
		cp bin/postgres bin/postgres.native

		cat > ${PREFIX}/bin/postgres <<END
#!/bin/bash
unset LD_PRELOAD
TZ=UTC
PGDATA=${PGDATA}
echo "# \$@" >> ${PREFIX}/journal.sql
tee -a ${PREFIX}/journal.sql | ${PREFIX}/bin/postgres.native \$@
END

		chmod +x ${PREFIX}/bin/postgres

		# 32bits build run initdb
		unset LD_PRELOAD

		rm -rf base/
		> ${PREFIX}/journal.sql

		${PREFIX}/bin/initdb -g -N $LANG $CRED --pgdata=${PGDATA}

		mkdir -p base-native/ base-wasm/
		cp -rf base/* base-native/
		popd
	fi
	sync
	echo "initdb-native done"
	# make distclean 2>&1 >/dev/null
	exit 0
fi

# on hold.

if echo "$@" |grep wasi
then
	. /opt/python-wasm-sdk/wasisdk/wasisdk_env.sh
    export PREFIX=$PGROOT

	echo "      ========== building wasi Debug=${DEBUG} ===========   "
	#make distclean

	CONFIG_SITE=${PREFIX}/config.site CC="wasi-c" CXX="wasi-c++" $CNF --cache-file=${PREFIX}/config.cache.emsdk && make
	# && make install

	exit 0
fi



# ================= build wasm (node) ===========================

if echo "$@" |grep -q emsdk
then
    shift

    . /opt/python-wasm-sdk/wasm32-bi-emscripten-shell.sh

    # was erased, default pfx is sdk dir
    export PREFIX=$PGROOT

    echo "  ==== building wasm MVP:$MVP Debug=${DEBUG} with opts : $@  == "


    if [ -f ${PREFIX}/password ]
    then
        echo "not changing db password"
    else
        echo password > ${PREFIX}/password
    fi

    if [ -f ${PREFIX}/config.cache.emsdk ]
    then
        echo "re-using config cache file from ${PREFIX}/config.cache.emsdk"
    else
        if [ -f ../config.cache.emsdk ]
        then
            cp ../config.cache.emsdk ${PREFIX}/
        else
            cp config.cache.emsdk ${PREFIX}/
        fi
    fi

    # -lwebsocket.js -sPROXY_POSIX_SOCKETS -pthread -sPROXY_TO_PTHREAD
    # CONFIG_SITE=$(pwd)/config.site EMCC_CFLAGS="--oformat=html" \

    # crash clang CFLAGS=-Wno-error=implicit-function-declaration

    if CONFIG_SITE==${PGDATA}/config.site emconfigure $CNF --with-template=emscripten --cache-file=${PREFIX}/config.cache.emsdk
    then
        echo configure ok
    else
        echo configure failed
        exit 161
    fi

    sed -i 's|ZIC= ./zic|ZIC= zic|g' ./src/timezone/Makefile


    if grep -q MAIN_MODULE src/backend/Makefile
    then
        echo "dyld server patch ok"
    else
        echo "missing server dyld patch"
        exit 219
    fi
    mkdir -p bin

    cat > bin/zic <<END
#!/bin/bash
#. /opt/python-wasm-sdk/wasm32-bi-emscripten-shell.sh
TZ=UTC PGTZ=UTC node $(pwd)/src/timezone/zic \$@
END

    # --disable-shared not supported so be able to use a fake linker

    > /tmp/disable-shared.log

    cat > bin/wasm-shared <<END
#!/bin/bash
echo "[\$(pwd)] $0 \$@" >> /tmp/disable-shared.log
# shared build
emcc -DPREFIX=${PREFIX} -shared -sSIDE_MODULE=1 \$@ -Wno-unused-function
exit 0
# fake linker
for arg do
    shift
    if [ "\$arg" = "-o" ]
    then
        continue
    fi
    if echo "\$arg" | grep -q ^-
    then
        continue
    fi
    if echo "\$arg" | grep -q \\\\.o$
    then
        continue
    fi
    set -- "\$@" "\$arg"
done
touch \$@
END

    # FIXME: workaround for /conversion_procs/ make
    # cp bin/wasm-shared bin/o
    if which zic
    then
        cp $(which zic) zic.native bin/zic
    fi
    chmod +x bin/zic bin/wasm-shared

    # for zic and wasm-shared
    export PATH=$(pwd)/bin:$PATH


    EMCC_WEB="-sNO_EXIT_RUNTIME=1 -sENVIRONMENT=web"
    EMCC_NODE="-sEXIT_RUNTIME=1 -DEXIT_RUNTIME -sNODERAWFS -sENVIRONMENT=node"
    EMCC_ENV=${EMCC_NODE}

    # export EMCC_CFLAGS="-lwebsocket.js -sPROXY_POSIX_SOCKETS -pthread -sPROXY_TO_PTHREAD $EMCC_CFLAGS"
    #  -sWASMFS

    # only required for static initdb
    EMCC_CFLAGS="-sERROR_ON_UNDEFINED_SYMBOLS=0"

    EMCC_CFLAGS="-sTOTAL_MEMORY=1GB -sSTACK_SIZE=5MB -sALLOW_TABLE_GROWTH -sALLOW_MEMORY_GROWTH -sGLOBAL_BASE=4MB ${EMCC_CFLAGS}"
    EMCC_CFLAGS="-DPREFIX=${PREFIX} ${EMCC_CFLAGS}"

    if $CI
    then
        EMCC_CFLAGS="-DPATCH_MAIN=/home/runner/work/pglite-build/pglite-build/pg_main.c ${EMCC_CFLAGS}"
        EMCC_CFLAGS="-DPATCH_PLUGIN=/home/runner/work/pglite-build/pglite-build/pg_plugin.h ${EMCC_CFLAGS}"
    else
        EMCC_CFLAGS="-DPATCH_MAIN=/data/git/pg/pg_main.c ${EMCC_CFLAGS}"
        EMCC_CFLAGS="-DPATCH_PLUGIN=/data/git/pg/pg_plugin.h ${EMCC_CFLAGS}"
    fi

    export EMCC_CFLAGS="-Wno-macro-redefined -Wno-unused-function ${EMCC_CFLAGS}"


	if EMCC_CFLAGS="${EMCC_ENV} ${EMCC_CFLAGS}" emmake make -j $(nproc) 2>&1 > /tmp/build.log
	then
        echo build ok
        # for 32bits zic
        unset LD_PRELOAD
        if EMCC_CFLAGS="${EMCC_ENV} ${EMCC_CFLAGS}" emmake make install 2>&1 > /tmp/install.log
        then
            echo install ok
        else
            cat /tmp/install.log
            echo "install failed"
            exit 236
        fi
    else
        cat /tmp/build.log
        echo "build failed"
        exit 241
	fi

	mv -vf ./src/bin/initdb/initdb.wasm ./src/backend/postgres.wasm ./src/backend/postgres.map ${PREFIX}/bin/
    mv -vf ./src/bin/pg_resetwal/pg_resetwal.wasm  ./src/bin/pg_dump/pg_restore.wasm ./src/bin/pg_dump/pg_dump.wasm ./src/bin/pg_dump/pg_dumpall.wasm ${PREFIX}/bin/
	mv -vf ./src/bin/initdb/initdb ${PREFIX}/bin/initdb.js
	mv -vf ./src/bin/pg_resetwal/pg_resetwal ${PREFIX}/bin/pg_resetwal.js
	mv -vf ./src/backend/postgres ${PREFIX}/bin/postgres.js


    cat  > ${PREFIX}/postgres <<END
#!/bin/bash
. /opt/python-wasm-sdk/wasm32-bi-emscripten-shell.sh
TZ=UTC PGTZ=UTC PGDATA=${PGDATA} node ${PREFIX}/bin/postgres.js \$@
END

# remove the abort but stall prompt
#  2>&1 | grep --line-buffered -v ^var\\ Module

    # force node wasm version
    cp -vf ${PREFIX}/postgres ${PREFIX}/bin/postgres

	cat  > ${PREFIX}/initdb <<END
#!/bin/bash
. /opt/python-wasm-sdk/wasm32-bi-emscripten-shell.sh
TZ=UTC PGTZ=UTC node ${PREFIX}/bin/initdb.js \$@
END

    chmod +x ${PREFIX}/postgres ${PREFIX}/bin/postgres
	chmod +x ${PREFIX}/initdb ${PREFIX}/bin/initdb

	echo "initdb for PGDATA=${PGDATA} "


    # create empty db hack

	cat >$PREFIX/initdb.sh <<END
#!/bin/bash
rm -rf ${PGDATA} /tmp/initdb-* ${PREFIX}/wal/*
export TZ=UTC
export PGTZ=UTC
SQL=/tmp/initdb-\$\$
# TODO: --waldir=${PREFIX}/wal
> /tmp/initdb.txt

${PREFIX}/initdb --no-clean --wal-segsize=1 -g $LANG $CRED --pgdata=${PGDATA}

# 2> /tmp/initdb-\$\$.log

mv /tmp/initdb.boot.txt \${SQL}.boot.sql
mv /tmp/initdb.single.txt \${SQL}.single.sql


#grep -v dynamic_shared_memory_type ${PGDATA}/postgresql.conf > /tmp/pg-\$\$.conf
#mv /tmp/pg-\$\$.conf ${PGDATA}/postgresql.conf


#grep -v ^invalid\\ binary /tmp/initdb-\$\$.log \\
# | csplit - -s -n 1 -f \${SQL}-split /^build\\ indices\$/1
#
#grep -v ^invalid\\ binary /tmp/initdb.txt \\
# | csplit - -s -n 1 -f \${SQL}-split /^build\\ indices\$/1
#
#grep -v ^# \${SQL}-split0 > \${SQL}.boot.sql
#rm \${SQL}-split0
#grep -v ^# \${SQL}-split1 | grep -v ^warning \\
#  | grep -v '^/\\*'  | grep -v '^ \\*' | grep -v '^ \\*/'  >> \${SQL}.single.sql
#


if \${CI:-false}
then
    cp -vf \$SQL ${PREFIX}/\$(md5sum \$SQL|cut -c1-32).sql
fi

# --wal-segsize=1  -> -X 1048576

# CKSUM_B -k --data-checksums
# 2024-04-24 05:53:28.121 GMT [42] WARNING:  page verification failed, calculated checksum 5487 but expected 0
# 2024-04-24 05:53:28.121 GMT [42] FATAL:  invalid page in block 0 of relation base/1/1259

CMD="${PREFIX}/postgres --boot $CKSUM_B -D ${PGDATA} -d 3 $PGOPTS -X 1048576"
echo "\$CMD < \$SQL.boot.sql"
\$CMD < \$SQL.boot.sql 2>&1 \\
 | grep -v --line-buffered 'bootstrap> boot' \\
 | grep -v --line-buffered 'index'

echo "

\$(md5sum /tmp/initdb-\$\$.*.sql)

    boot done
"
if echo $PREFIX|grep -q -v pglite
then
    read
else
    sync
fi


CMD="${PREFIX}/postgres --single $PGFIXES $CKSUM_S -D ${PGDATA} -F -O -j $PGOPTS template1"
echo "\$CMD < \$SQL.single.sql"
\$CMD < \$SQL.single.sql \\
 | grep -v --line-buffered '^pg> \$' \\
 | grep -v --line-buffered ^\$

rm $PGDATA/postmaster.pid

if echo $PREFIX|grep -q -v pglite
then
    echo ready
    read
    echo cleaning up sql journal
    read
fi

# rm  /tmp/initdb-\$\$.log \${SQL}-split1

rm /tmp/initdb-\$\$.*.sql
END

    # force node wasm version
    cp -vf ${PREFIX}/initdb.sh ${PREFIX}/bin/initdb

    chmod +x $PREFIX/*sh
fi


# ================= run test (node) ===========================

if echo "$@" |grep test
then
    shift

    echo "      ======= testing with opts: $@ =========   "


	$PREFIX/initdb.sh   2>&1 | grep --line-buffered -v ^var\ Module
	echo "initdb.sh done, now init sql default database"

	if [ -f ${PGDATA}/postmaster.pid ]
	then
		chmod +x $PREFIX/*.sh


        # TODO run some sql tests
		# $PREFIX/initsql.sh

		rm $PGDATA/postmaster.pid
	fi

    mkdir -p ${PREFIX}/lib
    rm ${PREFIX}/lib/lib*.so.* ${PREFIX}/lib/libpq.so

    . /opt/python-wasm-sdk/wasm32-bi-emscripten-shell.sh


    # build single lib static/shared
    if [ -f /data/git/pg/local.sh ]
    then
        . /data/git/pg/local.sh
    else
        . $GITHUB_WORKSPACE/link.sh
    fi

    echo "========== $CI : $GITHUB_WORKSPACE ======"

    file ${PREFIX}/lib/lib*.so

    echo "========================================================="

    if ${CI:-false}
    then
        mkdir -p /tmp/sdk
        tar -cpRz ${PREFIX} > /tmp/sdk/pg.tar.gz
    fi

fi
