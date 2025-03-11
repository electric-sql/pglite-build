#!/bin/bash

export WORKSPACE=${GITHUB_WORKSPACE:-$(pwd)}

# we are using a custom emsdk to build pglite wasm
# this is available as a docker image under electricsql/pglite-builder
IMG_NAME="electricsql/pglite-builder"
IMG_TAG="latest"

[ -f ./pglite/.buildconfig ] && cp ./pglite/.buildconfig .buildconfig

source .buildconfig

if [[ -z "$SDKROOT" || -z "$PG_VERSION" ]]; then
  echo "Missing SDKROOT and PG_VERSION env vars."
  echo "Source them from .buildconfig"
  exit 1
fi



docker run \
  --rm \
  -e SDKROOT=$SDKROOT \
  -e PG_VERSION=${PG_VERSION} \
  -e PG_BRANCH=${PG_BRANCH} \
  -v .:${WORKSPACE} \
  $IMG_NAME:$IMG_TAG \
  bash /workspace/wasm-build.sh ${WHAT:-"contrib extra"}



