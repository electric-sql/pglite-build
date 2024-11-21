# pglite-build

build 16.4+ with wasi sdk 24 (patched for pthreads/shm stubs and linked to custom unix socket implementation)


PREFIX is /tmp/pglite

PGDATA is hardcoded to /tmp/pglite/base


for now able to run via wasmtime and wasmtime-py

known other embedding :


go wasi :
https://github.com/sgosiaco/pglite-go


php wasm:
https://github.com/seanmorris/pdo-pglite

