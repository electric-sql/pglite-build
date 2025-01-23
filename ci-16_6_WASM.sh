#!/bin/bash
export WORKSPACE=$(pwd)
export PG_VERSION=${PG_VERSION:-REL_16_6_WASM}

[ -f postgresql-${PG_VERSION}/configure ] \
 || git clone --no-tags --depth 1 --single-branch --branch ${PG_VERSION} https://github.com/pygame-web/postgres postgresql-${PG_VERSION}

chmod +x portable/*.sh wasm-build/*.sh
cp -R wasm-build* patches-${PG_VERSION} postgresql-${PG_VERSION}/

pushd postgresql-${PG_VERSION}
    ${WORKSPACE}/portable/portable.sh
popd

