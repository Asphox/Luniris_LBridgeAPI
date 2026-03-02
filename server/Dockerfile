# =============================================================================
# Luniris Server Build Image (ARM64) - Full Static Build from Source
# =============================================================================
# Builds ALL dependencies from source targeting Cortex-A72 (Raspberry Pi 4 CM)
# =============================================================================
FROM alpine:3.19 AS builder

# Target: Raspberry Pi 4 Compute Module (Cortex-A72 / ARMv8.0-a)
# -march=armv8-a+crc : base ARMv8 + CRC (safe for Cortex-A72), avoids -mcpu micro-arch opts that crash QEMU
# -mno-outline-atomics : prevents runtime LSE detection (LSE not available on ARMv8.0)
ENV TARGET_CPU_FLAGS="-march=armv8-a+crc+nocrypto -mno-outline-atomics"
ENV CFLAGS="${TARGET_CPU_FLAGS}"
ENV CXXFLAGS="${TARGET_CPU_FLAGS}"

# Install build tools only - NO prebuilt static libraries
RUN apk add --no-cache \
    build-base \
    cmake \
    ninja \
    git \
    linux-headers \
    perl \
    # protoc and grpc_cpp_plugin are build-time tools only (not linked into final binary)
    protobuf-dev \
    grpc \
    grpc-dev

WORKDIR /deps

# =============================================================================
# Build zlib (static)
# =============================================================================
RUN git clone --depth 1 --branch v1.3.1 https://github.com/madler/zlib.git && \
    cd zlib && \
    CFLAGS="${CFLAGS}" ./configure --prefix=/usr/local --static && \
    while ! make -j$(nproc); do echo "Retry zlib..."; sleep 1; done && \
    make install

# =============================================================================
# Build c-ares (static)
# =============================================================================
RUN git clone --depth 1 --branch cares-1_19_1 https://github.com/c-ares/c-ares.git && \
    cd c-ares && \
    mkdir build && cd build && \
    cmake .. \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCARES_STATIC=ON \
        -DCARES_SHARED=OFF \
        -DCARES_BUILD_TESTS=OFF \
        -DCMAKE_C_FLAGS="${TARGET_CPU_FLAGS}" \
        -DCMAKE_INSTALL_PREFIX=/usr/local && \
    while ! ninja -j$(nproc); do echo "Retry c-ares..."; sleep 1; done && \
    ninja install

# =============================================================================
# Build abseil-cpp (static)
# =============================================================================
# Bump this to invalidate Docker cache from this layer onwards
ARG ABSEIL_CACHE_BUST=3
RUN git clone --depth 1 --branch 20230802.1 https://github.com/abseil/abseil-cpp.git && \
    cd abseil-cpp && \
    # Patch: disable AES dispatch (no crypto extensions on target SoC)
    # platform.h does #undef then #define ABSL_RANDOM_INTERNAL_AES_DISPATCH 1 unconditionally on aarch64
    # This forces runtime AES detection + AESE codegen even with +nocrypto, causing SIGILL
    sed -i 's/#define ABSL_RANDOM_INTERNAL_AES_DISPATCH 1/#define ABSL_RANDOM_INTERNAL_AES_DISPATCH 0/g' absl/random/internal/platform.h && \
    mkdir build && cd build && \
    cmake .. \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_CXX_STANDARD=17 \
        -DCMAKE_C_FLAGS="${TARGET_CPU_FLAGS}" \
        -DCMAKE_CXX_FLAGS="${TARGET_CPU_FLAGS}" \
        -DABSL_ENABLE_INSTALL=ON \
        -DCMAKE_INSTALL_PREFIX=/usr/local && \
    while ! ninja -j$(nproc); do echo "Retry..."; sleep 1; done && \
    ninja install

# =============================================================================
# Build protobuf (static)
# =============================================================================
RUN git clone --depth 1 --branch v24.4 https://github.com/protocolbuffers/protobuf.git && \
    cd protobuf && \
    git submodule update --init third_party/utf8_range && \
    mkdir build && cd build && \
    cmake .. \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -Dprotobuf_BUILD_TESTS=OFF \
        -Dprotobuf_BUILD_CONFORMANCE=OFF \
        -Dprotobuf_BUILD_EXAMPLES=OFF \
        -Dprotobuf_BUILD_LIBUPB=OFF \
        -Dprotobuf_BUILD_PROTOC_BINARIES=OFF \
        -Dprotobuf_ABSL_PROVIDER=package \
        -DCMAKE_CXX_STANDARD=17 \
        -DCMAKE_C_FLAGS="${TARGET_CPU_FLAGS}" \
        -DCMAKE_CXX_FLAGS="${TARGET_CPU_FLAGS}" \
        -DCMAKE_INSTALL_PREFIX=/usr/local && \
    while ! ninja -j$(nproc); do echo "Retry..."; sleep 1; done && \
    ninja install

# =============================================================================
# Build re2 (static)
# =============================================================================
RUN git clone --depth 1 --branch 2023-11-01 https://github.com/google/re2.git && \
    cd re2 && \
    mkdir build && cd build && \
    cmake .. \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_CXX_STANDARD=17 \
        -DCMAKE_C_FLAGS="${TARGET_CPU_FLAGS}" \
        -DCMAKE_CXX_FLAGS="${TARGET_CPU_FLAGS}" \
        -DCMAKE_INSTALL_PREFIX=/usr/local && \
    while ! ninja -j$(nproc); do echo "Retry re2..."; sleep 1; done && \
    ninja install

# =============================================================================
# Build gRPC (static)
# =============================================================================
RUN git clone --depth 1 --branch v1.59.3 https://github.com/grpc/grpc.git && \
    cd grpc && \
    git submodule update --init \
        third_party/boringssl-with-bazel \
        third_party/upb \
        third_party/envoy-api \
        third_party/googleapis \
        third_party/opencensus-proto \
        third_party/xds && \
    mkdir build && cd build && \
    cmake .. \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -DgRPC_INSTALL=ON \
        -DgRPC_BUILD_TESTS=OFF \
        -DgRPC_BUILD_CODEGEN=OFF \
        -DgRPC_BUILD_GRPC_CPP_PLUGIN=OFF \
        -DgRPC_BUILD_GRPC_CSHARP_PLUGIN=OFF \
        -DgRPC_BUILD_GRPC_NODE_PLUGIN=OFF \
        -DgRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN=OFF \
        -DgRPC_BUILD_GRPC_PHP_PLUGIN=OFF \
        -DgRPC_BUILD_GRPC_PYTHON_PLUGIN=OFF \
        -DgRPC_BUILD_GRPC_RUBY_PLUGIN=OFF \
        -DgRPC_ABSL_PROVIDER=package \
        -DgRPC_CARES_PROVIDER=package \
        -DgRPC_PROTOBUF_PROVIDER=package \
        -DgRPC_RE2_PROVIDER=package \
        -DgRPC_SSL_PROVIDER=module \
        -DgRPC_ZLIB_PROVIDER=package \
        -DCMAKE_CXX_STANDARD=17 \
        -DCMAKE_C_FLAGS="${TARGET_CPU_FLAGS}" \
        -DCMAKE_CXX_FLAGS="${TARGET_CPU_FLAGS}" \
        -DCMAKE_INSTALL_PREFIX=/usr/local && \
    while ! ninja -j$(nproc); do echo "Retry..."; sleep 1; done && \
    ninja install

WORKDIR /workspace

CMD ["sh", "-c", "\
    rm -rf server/build && \
    mkdir -p server/build && \
    cd server/build && \
    cmake .. -DLUNIRIS_STATIC_BUILD=ON \
        -DCMAKE_C_FLAGS=\"-march=armv8-a+crc -mno-outline-atomics\" \
        -DCMAKE_CXX_FLAGS=\"-march=armv8-a+crc -mno-outline-atomics\" \
        ${EXTRA_CMAKE_FLAGS} && \
    cmake --build . -j2 && \
    echo '' && \
    echo '=== Keeping unstripped copy for addr2line ===' && \
    cp bin/luniris_api_lbridge_server bin/luniris_api_lbridge_server.debug && \
    echo '=== Stripping debug info (keeping symbol table for crash handler) ===' && \
    ls -la bin/luniris_api_lbridge_server && \
    strip --strip-debug bin/luniris_api_lbridge_server && \
    echo '=== After strip ===' && \
    ls -la bin/luniris_api_lbridge_server && \
    echo '' && \
    echo '=== Build complete ===' \
"]
