#!/bin/bash
# =============================================================================
# Luniris Server Build Script (ARM64 via Docker/Alpine)
# Full static build from source
# =============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

IMAGE_NAME="luniris-builder-arm64-static"

# Parse arguments
EXTRA_CMAKE_FLAGS=""
for arg in "$@"; do
    case "$arg" in
        --log-all)
            EXTRA_CMAKE_FLAGS="$EXTRA_CMAKE_FLAGS -DLUNIRIS_ENABLE_LBRIDGE_LOG=ON"
            echo "[opt] LBridge logging enabled (all levels)"
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Usage: $0 [--log-all]"
            exit 1
            ;;
    esac
done

echo "=== Luniris Server ARM64 Full Static Build ==="
echo "This will build gRPC, protobuf, and abseil from source."
echo "First build takes a while, subsequent builds are cached."
echo ""

# Enable ARM64 emulation (only needed once per boot)
echo "[1/5] Setting up ARM64 emulation..."
docker run --rm --privileged multiarch/qemu-user-static --reset -p yes 2>/dev/null || true

# Build the Docker image (includes building deps from source)
echo "[2/5] Building Docker image with dependencies..."
docker build --platform linux/arm64 -t "$IMAGE_NAME" -f server/Dockerfile .

# Run the build
echo "[3/5] Compiling server..."
docker run --rm --platform linux/arm64 -e "EXTRA_CMAKE_FLAGS=$EXTRA_CMAKE_FLAGS" -v "$SCRIPT_DIR":/workspace "$IMAGE_NAME"

# Verify the binary
echo "[4/5] Verifying binary..."
docker run --rm --platform linux/arm64 -v "$SCRIPT_DIR":/workspace "$IMAGE_NAME" sh -c "
    echo '=== Binary info ===' && \
    file /workspace/server/build/bin/luniris_api_lbridge_server && \
    echo '' && \
    echo '=== Link type ===' && \
    ldd /workspace/server/build/bin/luniris_api_lbridge_server 2>&1 || echo '(statically linked - no dynamic dependencies)' && \
    echo '' && \
    echo '=== Binary size ===' && \
    ls -lh /workspace/server/build/bin/luniris_api_lbridge_server
"

# Create distribution zip
echo "[5/5] Creating distribution zip..."
ZIP_NAME="server/build/luniris_api_lbridge_server.zip"
rm -f "$ZIP_NAME"
zip -j "$ZIP_NAME" server/build/bin/luniris_api_lbridge_server server/luniris_feature/*

echo ""
echo "=== Build complete ==="
echo "Output: $SCRIPT_DIR/$ZIP_NAME"
echo "  - luniris_api_lbridge_server (static binary)"
echo "  - luniris_feature files (manifests)"
echo ""
echo "This is a fully static binary - just copy it to your Raspberry Pi and run!"
