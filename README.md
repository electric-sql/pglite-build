# pglite-build
build 16.2 wasm


PREFIX is /tmp/pglite

PGDATA is hardcoded to /tmp/pglite/base


for now able to run via Node

$PREFIX/initdb.sh 

which will create  "template0" "template1" "postgres" databases in /tmp/pglite/base with TZ=UTC and lang C.UTF-8


$PREFIX/postgres is the node REPL


