#!/bin/bash

export WORKSPACE=${GITHUB_WORKSPACE:-$(pwd)}

source .buildconfig

if [[ -z "$SDKROOT" || -z "$PG_VERSION" ]]; then
  echo "Missing SDKROOT and PG_VERSION env vars."
  echo "Source them from .buildconfig"
  exit 1
fi

ALL="contrib extra"


# we are using a custom emsdk to build pglite wasm
# this is available as a docker image under electricsql/pglite-builder
IMG_NAME="electricsql/pglite-builder"
IMG_TAG="${PG_VERSION}_${SDK_VERSION}"

[ -f ./pglite/.buildconfig ] && cp ./pglite/.buildconfig ./.buildconfig

docker run \
  --rm \
  -e SDKROOT=$SDKROOT \
  -e PG_VERSION=${PG_VERSION} \
  -e PG_BRANCH=${PG_BRANCH} \
  -v .:${WORKSPACE} \
  $IMG_NAME:$IMG_TAG \
  bash ${WORKSPACE}/wasm-build.sh ${WHAT:-$ALL}



