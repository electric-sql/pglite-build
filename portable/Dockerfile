
ENV PYTHON_VERSION 3.13.1
ENV SDKROOT /opt/python-wasm-sdk
ENV PIP_ROOT_USER_ACTION ignore
ENV PIP_NO_CACHE_DIR 1

# TODO : bootstrap python from clang directly and use it as SYS_PYTHON
RUN apk add --no-cache --virtual .build-deps \
	    tar file lz4 \
	    git patch bison flex \
        findutils binutils coreutils \
	    libffi curl perl nodejs \
        make autoconf automake libtool \
    ;

WORKDIR /workspace

RUN ./wasm-build.sh
