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
    if $CI
    then
        cp /tmp/sdk/postgres.{js,data,wasm} pglite/packages/pglite/release/
        cp /tmp/sdk/libecpg.so pglite/packages/pglite/release/postgres.so
    else
        cp ${WEBROOT}/postgres.{js,data,wasm} pglite/packages/pglite/release/
        cp ${WEBROOT}/libecpg.so pglite/packages/pglite/release/postgres.so
    fi
    mv pglite/packages/pglite/release/postgres.js pglite/packages/pglite/release/pgbuild.js

    cat > pglite/packages/pglite/release/share.js <<END

    function loadPgShare(module, require) {
        console.warn("share.js: loadPgShare");
    }

    export default loadPgShare;
END

    cat pgbuild.js > pglite/packages/pglite/release/postgres.js

