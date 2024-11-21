reset
export SDKROOT=${SDKROOT:-/opt/python-wasm-sdk}

export CI=true
export WASI=true
export contrib=false
export extra=false
export PGVERSION=16.4
export DEBUG=true
export OBJDUMP=false

rm -rf /tmp/pglite/pg.installed /tmp/pglite/bin/*.wasi build/postgres /tmp/pglite/base /tmp/sdk/pg*
echo cleaned !

export PG_VERSION=${PG_VERSION:-16.4}
export DEBUG=${DEBUG:-false}
export PGES6=${PGES6:-true}
export WASI=${WASI:-false}
export OBJDUMP=${OBJDUMP:-true}
export EXTRA_EXT=${EXTRA_EXT:-"vector postgis"}


. /opt/python-wasm-sdk/wasisdk/wasisdk_env.sh

if ./cibuild.sh ${contrib:-contrib} ${extra:-extra} node linkweb pglite-prep pglite
then
    echo NO WEB RUNTIME
else
    echo FAILED
fi


rm /tmp/pglite/base/.s.PGS*

WASMTIME_BACKTRACE_DETAILS=1 wasi-run /tmp/pglite/bin/postgres.wasi --single postgres || \
 WASMTIME_BACKTRACE_DETAILS=1 wasi-run /tmp/pglite/bin/postgres.wasi --single postgres
