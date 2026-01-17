#!/bin/bash
#
# webos-package.sh - Build PCSX-ReARMed and package for webOS
#
# This script builds the emulator for ARM and creates a webOS .ipk package
# using the palm-package tool from the PalmSDK.
#
# Usage:
#   ./webos-package.sh              # Build and package
#   ./webos-package.sh --skip-build # Package only (use existing binaries)
#   ./webos-package.sh --clean      # Clean build artifacts
#
# Environment variables:
#   CROSS_COMPILE - ARM cross-compiler prefix (e.g., arm-linux-gnueabi-)
#   PALM_PACKAGE  - Path to palm-package (default: /opt/PalmSDK/Current/bin/palm-package)
#

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WEBOS_DIR="${SCRIPT_DIR}/webos"
PALM_PACKAGE="${PALM_PACKAGE:-/opt/PalmSDK/Current/bin/palm-package}"
OUTPUT_DIR="${SCRIPT_DIR}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Parse arguments
SKIP_BUILD=0
CLEAN_ONLY=0

for arg in "$@"; do
    case $arg in
        --skip-build)
            SKIP_BUILD=1
            ;;
        --clean)
            CLEAN_ONLY=1
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --skip-build    Skip compilation, package existing binaries"
            echo "  --clean         Clean build artifacts and exit"
            echo "  --help, -h      Show this help message"
            echo ""
            echo "Environment variables:"
            echo "  CROSS_COMPILE   ARM cross-compiler prefix (e.g., arm-linux-gnueabi-)"
            echo "  PALM_PACKAGE    Path to palm-package tool"
            exit 0
            ;;
    esac
done

# Clean target
if [ "$CLEAN_ONLY" -eq 1 ]; then
    log_info "Cleaning build artifacts..."
    make clean 2>/dev/null || true
    rm -f "${WEBOS_DIR}/pcsx"
    rm -rf "${WEBOS_DIR}/plugins"
    # Don't remove skin - it contains the custom PlayStation background
    # rm -rf "${WEBOS_DIR}/skin"
    rm -f "${OUTPUT_DIR}"/*.ipk
    log_info "Clean complete."
    exit 0
fi

# Check for palm-package
if [ ! -x "$PALM_PACKAGE" ]; then
    log_error "palm-package not found at: $PALM_PACKAGE"
    log_error "Please install PalmSDK or set PALM_PACKAGE environment variable"
    exit 1
fi

# Check for required webOS files
if [ ! -f "${WEBOS_DIR}/appinfo.json" ]; then
    log_error "appinfo.json not found in ${WEBOS_DIR}"
    exit 1
fi

if [ ! -f "${WEBOS_DIR}/psx.png" ] || [ ! -f "${WEBOS_DIR}/psx-256.png" ]; then
    log_error "Icon files (psx.png, psx-256.png) not found in ${WEBOS_DIR}"
    exit 1
fi

# Build phase
if [ "$SKIP_BUILD" -eq 0 ]; then
    log_info "Starting build process..."

    # Check for cross-compiler
    if [ -z "$CROSS_COMPILE" ]; then
        # Try to find a cross-compiler
        for cc in arm-linux-gnueabi- arm-none-linux-gnueabi- arm-linux-gnueabihf-; do
            if command -v "${cc}gcc" &> /dev/null; then
                CROSS_COMPILE="$cc"
                break
            fi
        done
    fi

    if [ -z "$CROSS_COMPILE" ]; then
        log_error "No ARM cross-compiler found!"
        log_error "Please install an ARM cross-compiler and set CROSS_COMPILE, e.g.:"
        log_error "  export CROSS_COMPILE=arm-linux-gnueabi-"
        log_error "  $0"
        log_error ""
        log_error "Or use --skip-build to package existing binaries."
        exit 1
    fi

    log_info "Using cross-compiler: ${CROSS_COMPILE}gcc"

    # Initialize submodules if needed
    if [ ! -f "frontend/libpicofe/README" ]; then
        log_info "Initializing git submodules..."
        git submodule init && git submodule update
    fi

    # Clean previous build
    log_info "Cleaning previous build..."
    make clean 2>/dev/null || true

    # Configure for ARM/webOS
    # webOS devices are ARMv7 with NEON support and OpenGL ES GPU
    log_info "Configuring build for ARM/webOS with GPU acceleration..."

    CFLAGS="-march=armv7-a -mfpu=neon -mfloat-abi=softfp" \
    CROSS_COMPILE="$CROSS_COMPILE" \
    ./configure \
        --platform=webos \
        --gpu=neon \
        --sound-drivers=sdl \
        --enable-neon

    # Build main binary
    log_info "Building PCSX emulator..."
    make -j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

    # Build plugins if they exist as separate targets
    log_info "Building plugins..."
    if [ -f "plugins/gpu_unai/Makefile" ]; then
        make -C plugins/gpu_unai/ 2>/dev/null || true
    fi
    if [ -f "plugins/dfxvideo/Makefile" ]; then
        make -C plugins/dfxvideo/ 2>/dev/null || true
    fi
    if [ -f "plugins/spunull/Makefile" ]; then
        make -C plugins/spunull/ 2>/dev/null || true
    fi

    log_info "Build complete."
fi

# Packaging phase
log_info "Preparing webOS package..."

# Check for main binary - first in webos/, then in project root
if [ -f "${WEBOS_DIR}/pcsx" ]; then
    log_info "Using existing binary in webos/"
    chmod +x "${WEBOS_DIR}/pcsx"
elif [ -f "${SCRIPT_DIR}/pcsx" ]; then
    log_info "Copying main binary from project root..."
    cp "${SCRIPT_DIR}/pcsx" "${WEBOS_DIR}/pcsx"
    chmod +x "${WEBOS_DIR}/pcsx"
else
    log_error "Main binary 'pcsx' not found!"
    log_error "Place it in webos/ or project root, or run without --skip-build to compile."
    exit 1
fi

# Create plugins directory
mkdir -p "${WEBOS_DIR}/plugins"

# Copy plugins if not already in webos/plugins/
PLUGINS_IN_WEBOS=$(ls "${WEBOS_DIR}/plugins/"*.so 2>/dev/null | wc -l)
if [ "$PLUGINS_IN_WEBOS" -gt 0 ]; then
    log_info "Using existing plugins in webos/plugins/"
else
    log_info "Copying plugins..."
    PLUGINS_FOUND=0

    # Look for built plugins in various locations
    for plugin in gpu_unai.so gpu_peops.so gpu_neon.so spunull.so; do
        for path in \
            "${SCRIPT_DIR}/plugins/${plugin%.so}/${plugin}" \
            "${SCRIPT_DIR}/plugins/dfxvideo/${plugin}" \
            "${SCRIPT_DIR}/${plugin}" \
            ; do
            if [ -f "$path" ]; then
                cp "$path" "${WEBOS_DIR}/plugins/"
                log_info "  Copied: $plugin"
                PLUGINS_FOUND=$((PLUGINS_FOUND + 1))
                break
            fi
        done
    done

    if [ "$PLUGINS_FOUND" -eq 0 ]; then
        log_warn "No plugins found. Package may not work correctly without GPU plugins."
    fi
fi

# Copy skin assets if not already present
if [ ! -f "${WEBOS_DIR}/skin/background.png" ]; then
    log_info "Copying skin assets..."
    mkdir -p "${WEBOS_DIR}/skin"
    # Use pandora skin as base if available
    if [ -d "${SCRIPT_DIR}/frontend/pandora/skin" ]; then
        cp "${SCRIPT_DIR}/frontend/pandora/skin/background.png" "${WEBOS_DIR}/skin/" 2>/dev/null || true
    fi
else
    log_info "Using existing skin assets in webos/skin/"
fi

# Remove any .DS_Store files
find "${WEBOS_DIR}" -name ".DS_Store" -delete 2>/dev/null || true

# Get version from appinfo.json
VERSION=$(grep '"version"' "${WEBOS_DIR}/appinfo.json" | sed 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/')
APP_ID=$(grep '"id"' "${WEBOS_DIR}/appinfo.json" | sed 's/.*"id"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/')

log_info "Packaging: ${APP_ID} v${VERSION}"

# Run palm-package
log_info "Running palm-package..."
cd "${SCRIPT_DIR}"
"$PALM_PACKAGE" "${WEBOS_DIR}"

# Find the generated .ipk
IPK_FILE=$(ls -t "${SCRIPT_DIR}"/*.ipk 2>/dev/null | head -1)

if [ -n "$IPK_FILE" ] && [ -f "$IPK_FILE" ]; then
    log_info "Package created successfully!"
    log_info "Output: ${IPK_FILE}"
    ls -lh "$IPK_FILE"
else
    log_error "Package creation failed!"
    exit 1
fi

echo ""
log_info "Done! Install on webOS device with:"
log_info "  palm-install ${IPK_FILE}"
