#!/bin/bash
#
# build_menu_variants.sh - Build 5 different menu button implementations
#
# This script builds 5 variants of PCSX-ReARMed for WebOS, each with a different
# approach to handling menu button input. Each variant gets a unique app ID
# so they can all be installed simultaneously for comparison testing.
#
# Variants:
#   1. MinHold   - Buttons held for minimum duration after release
#   2. Debounce  - Cooldown period after each button press
#   3. Queue     - Button events queued and returned one at a time
#   4. SyncPoll  - State synchronized with polling
#   5. KeyInject - SDL keyboard events injected from touch
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VARIANTS_DIR="${SCRIPT_DIR}/menu_test_variants"
WEBOS_DIR="${SCRIPT_DIR}/webos"
OUTPUT_DIR="${SCRIPT_DIR}/packages"
TOUCH_FILE="${SCRIPT_DIR}/frontend/in_webos_touch.c"
APPINFO_FILE="${WEBOS_DIR}/appinfo.json"

# Cross-compiler
export CROSS_COMPILE="${CROSS_COMPILE:-arm-linux-gnueabi-}"

# PalmSDK paths
PALM_PACKAGE="${PALM_PACKAGE:-/opt/PalmSDK/Current/bin/palm-package}"
PALM_INSTALL="${PALM_INSTALL:-palm-install}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_step() { echo -e "${BLUE}[STEP]${NC} $1"; }

# Variant definitions: name, suffix, app_id_suffix, title_suffix
declare -a VARIANTS=(
    "minhold:mh:minhold:MinHold"
    "debounce:db:debounce:Debounce"
    "queue:qu:queue:Queue"
    "syncpoll:sp:syncpoll:SyncPoll"
    "keyinject:ki:keyinject:KeyInject"
)

# Check prerequisites
check_prereqs() {
    log_info "Checking prerequisites..."

    if [ ! -x "$PALM_PACKAGE" ]; then
        log_error "palm-package not found at: $PALM_PACKAGE"
        exit 1
    fi

    if ! command -v "${CROSS_COMPILE}gcc" &>/dev/null; then
        log_error "Cross-compiler not found: ${CROSS_COMPILE}gcc"
        exit 1
    fi

    if [ ! -d "$VARIANTS_DIR" ]; then
        log_error "Variants directory not found: $VARIANTS_DIR"
        exit 1
    fi

    for variant_def in "${VARIANTS[@]}"; do
        IFS=':' read -r name suffix app_suffix title_suffix <<< "$variant_def"
        variant_file="${VARIANTS_DIR}/in_webos_touch_${name}.c"
        if [ ! -f "$variant_file" ]; then
            log_error "Variant source not found: $variant_file"
            exit 1
        fi
    done

    log_info "All prerequisites satisfied"
}

# Back up original files
backup_originals() {
    log_info "Backing up original files..."
    cp "$TOUCH_FILE" "${TOUCH_FILE}.orig"
    cp "$APPINFO_FILE" "${APPINFO_FILE}.orig"
}

# Restore original files
restore_originals() {
    log_info "Restoring original files..."
    if [ -f "${TOUCH_FILE}.orig" ]; then
        mv "${TOUCH_FILE}.orig" "$TOUCH_FILE"
    fi
    if [ -f "${APPINFO_FILE}.orig" ]; then
        mv "${APPINFO_FILE}.orig" "$APPINFO_FILE"
    fi
}

# Clean build artifacts
clean_build() {
    log_info "Cleaning build artifacts..."
    make clean 2>/dev/null || true
    rm -f "${WEBOS_DIR}/pcsx"
    rm -rf "${WEBOS_DIR}/plugins"
    # Don't remove skin - it contains the custom PlayStation background
    # rm -rf "${WEBOS_DIR}/skin"
}

# Build the project
do_build() {
    log_info "Configuring build..."

    CFLAGS="-march=armv7-a -mfpu=neon -mfloat-abi=softfp" \
    CROSS_COMPILE="$CROSS_COMPILE" \
    ./configure \
        --platform=webos \
        --gpu=neon \
        --sound-drivers=sdl \
        --enable-neon

    log_info "Building..."
    make -j$(nproc 2>/dev/null || echo 4)
}

# Package for WebOS
do_package() {
    local variant_name="$1"
    local app_suffix="$2"
    local title_suffix="$3"

    log_info "Packaging variant: $variant_name"

    # Copy binary
    if [ -f "${SCRIPT_DIR}/pcsx" ]; then
        cp "${SCRIPT_DIR}/pcsx" "${WEBOS_DIR}/pcsx"
        chmod +x "${WEBOS_DIR}/pcsx"
    else
        log_error "Binary not found!"
        return 1
    fi

    # Create plugins directory
    mkdir -p "${WEBOS_DIR}/plugins"

    # Create skin directory if needed (but don't overwrite existing PlayStation background)
    if [ ! -f "${WEBOS_DIR}/skin/background.png" ]; then
        mkdir -p "${WEBOS_DIR}/skin"
        if [ -d "${SCRIPT_DIR}/frontend/pandora/skin" ]; then
            cp "${SCRIPT_DIR}/frontend/pandora/skin/background.png" "${WEBOS_DIR}/skin/" 2>/dev/null || true
        fi
    fi

    # Update appinfo.json with unique ID and title
    cat > "$APPINFO_FILE" << EOF
{
	"title": "PCSX $title_suffix",
	"id": "com.starkka.pcsxrearmed.${app_suffix}",
	"version": "1.0.7",
	"vendor": "starkka",
	"type": "pdk",
	"main": "pcsx",
	"icon": "psx.png",
	"splashicon": "psx-256.png",
	"uiRevision": 2
}
EOF

    # Remove .DS_Store files
    find "${WEBOS_DIR}" -name ".DS_Store" -delete 2>/dev/null || true

    # Run palm-package
    cd "${SCRIPT_DIR}"
    "$PALM_PACKAGE" "${WEBOS_DIR}"

    # Move package to output directory
    local ipk_file=$(ls -t "${SCRIPT_DIR}"/*.ipk 2>/dev/null | head -1)
    if [ -n "$ipk_file" ] && [ -f "$ipk_file" ]; then
        mv "$ipk_file" "${OUTPUT_DIR}/"
        log_info "Created: ${OUTPUT_DIR}/$(basename "$ipk_file")"
    else
        log_error "Package creation failed for $variant_name"
        return 1
    fi
}

# Build one variant
build_variant() {
    local variant_def="$1"
    IFS=':' read -r name suffix app_suffix title_suffix <<< "$variant_def"

    log_step "=========================================="
    log_step "Building variant: $name ($title_suffix)"
    log_step "=========================================="

    # Copy variant source
    local variant_file="${VARIANTS_DIR}/in_webos_touch_${name}.c"
    log_info "Using source: $variant_file"
    cp "$variant_file" "$TOUCH_FILE"

    # Clean and build
    clean_build
    do_build

    # Package
    do_package "$name" "$app_suffix" "$title_suffix"
}

# Install all packages
install_packages() {
    log_step "=========================================="
    log_step "Installing packages on device"
    log_step "=========================================="

    for ipk in "${OUTPUT_DIR}"/*.ipk; do
        if [ -f "$ipk" ]; then
            log_info "Installing: $(basename "$ipk")"
            "$PALM_INSTALL" "$ipk" || log_warn "Failed to install: $ipk"
        fi
    done
}

# Main
main() {
    log_info "Starting multi-variant build"
    log_info "Cross-compiler: ${CROSS_COMPILE}gcc"

    # Create output directory
    mkdir -p "$OUTPUT_DIR"

    # Check prerequisites
    check_prereqs

    # Initialize submodules if needed
    if [ ! -f "${SCRIPT_DIR}/frontend/libpicofe/README" ]; then
        log_info "Initializing git submodules..."
        git submodule init && git submodule update
    fi

    # Back up original files
    backup_originals

    # Trap to restore on error or exit
    trap restore_originals EXIT

    # Build each variant
    for variant_def in "${VARIANTS[@]}"; do
        build_variant "$variant_def"
    done

    # Restore originals
    restore_originals
    trap - EXIT

    log_step "=========================================="
    log_step "Build complete! Packages in: $OUTPUT_DIR"
    log_step "=========================================="
    ls -la "$OUTPUT_DIR"/*.ipk 2>/dev/null || true

    # Install if requested
    if [ "$AUTO_INSTALL" -eq 1 ]; then
        install_packages
    else
        echo ""
        read -p "Install all packages on connected device? [y/N] " -n 1 -r
        echo ""
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            install_packages
        else
            log_info "Skipping installation. You can install manually with:"
            log_info "  palm-install ${OUTPUT_DIR}/<package>.ipk"
        fi
    fi

    log_info "Done!"
}

# Parse arguments
AUTO_INSTALL=0
for arg in "$@"; do
    case $arg in
        --install)
            AUTO_INSTALL=1
            ;;
    esac
done

# Run if not sourced
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi
