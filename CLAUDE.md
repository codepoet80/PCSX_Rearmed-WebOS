# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

PCSX-ReARMed is a PlayStation 1 emulator fork based on PCSX-Reloaded. Originally ARM-oriented with MIPS→ARM dynamic recompilation, it now targets multiple architectures. This WebOS-specific fork (`com.starkka.pcsxrearmed`) packages the emulator for LG WebOS TV platforms.

## Build Commands

### Standard Build (Linux/macOS)
```bash
git submodule init && git submodule update  # Required first time
./configure
make -j$(nproc)
```

### LibRetro Core Build
```bash
make -j$(nproc) -f Makefile.libretro
```

### Cross-compilation Examples
```bash
# Windows (mingw)
make -f Makefile.libretro platform=win32 CC=x86_64-w64-mingw32-gcc

# Android
${ANDROID_NDK_HOME}/ndk-build -C jni/
```

### Configure Options
```bash
./configure --help                    # Show all options
./configure --platform=generic        # Target platform (generic, pandora, maemo, caanoo, miyoo)
./configure --gpu=neon                # GPU plugin (neon, peops, unai)
./configure --dynarec=lightrec        # Dynarec (ari64, lightrec, none)
./configure --sound-drivers="alsa sdl"
```

### Build Flags
```bash
make DEBUG=1           # Debug build
make DEBUG_ASAN=1      # AddressSanitizer
make DEBUG_UBSAN=1     # UndefinedBehaviorSanitizer
```

### Dependencies (Ubuntu/Debian)
```bash
sudo apt-get install libsdl1.2-dev libasound2-dev libpng-dev libz-dev
```

## Architecture

### Core Emulation (`libpcsxcore/`)
- `r3000a.c` - R3000A CPU core
- `psxinterpreter.c` - CPU interpreter
- `new_dynarec/` - Ari64's ARM/ARM64 dynamic recompiler
  - `new_dynarec.c` - Main recompiler (~300KB)
  - `linkage_arm.S`, `linkage_arm64.S` - Assembly stubs
- `lightrec/` - Alternative JIT for non-ARM architectures
- `gte.c`, `gte_arm.S`, `gte_neon.S` - Graphics Transform Engine (3D math)
- `cdrom.c`, `cdrom-async.c` - CD-ROM emulation with async support
- `psxbios.c` - BIOS HLE (High Level Emulation)
- `psxmem.c` - Memory management (4MB RAM, 512KB VRAM)

### Frontend (`frontend/`)
- `main.c` - Entry point and game loop
- `menu.c` - Configuration menu system
- `libretro.c` - LibRetro core implementation
- `plugin_lib.c` - Plugin event handling
- `plat_sdl.c` - SDL platform layer (primary)
- `cspace.c` - Color space conversions (NEON/SSE optimized)
- Platform-specific: `plat_pandora.c`, `plat_omap.c`, etc.

### Plugins (`plugins/`)
- `gpu_neon/` - ARM NEON GPU renderer (best for ARM/x86 with SSE2+)
- `gpu_unai/` - PCSX4ALL GPU (general-purpose software renderer)
- `gpulib/` - Common GPU library interface
- `dfsound/` - P.E.Op.S. SPU plugin (audio)
- `dfxvideo/` - Legacy GPU plugin

### External Dependencies (`deps/`)
Git submodules - run `git submodule update --init` if missing:
- `libchdr/` - CHD disk image format
- `lightrec/` - Lightrec JIT compiler
- `lightning/` - GNU Lightning JIT library
- `libretro-common/` - LibRetro utilities

### WebOS Port (`webos/`)
- `appinfo.json` - WebOS app manifest (version, app ID, icon paths)
- `psx.png`, `psx-256.png` - App icons (64x64 and 256x256)
- PDK-based native application packaging

## WebOS Packaging

### Build and Package for WebOS
```bash
# Full build and package (requires ARM cross-compiler)
export CROSS_COMPILE=arm-linux-gnueabi-
./webos-package.sh

# Package only (use pre-built binaries)
./webos-package.sh --skip-build

# Clean build artifacts
./webos-package.sh --clean
```

### Requirements
- PalmSDK installed at `/opt/PalmSDK` (provides `palm-package`)
- ARM cross-compiler (e.g., `arm-linux-gnueabi-gcc`) for building
- Git submodules initialized

### Package Structure
The script creates a webOS .ipk package containing:
```
webos/
├── appinfo.json    # App manifest
├── pcsx            # Main ARM binary
├── psx.png         # 64x64 icon
├── psx-256.png     # 256x256 splash icon
├── plugins/        # GPU/audio plugins (.so)
└── skin/           # UI assets
```

### Installation
```bash
palm-install com.starkka.pcsxrearmed_*.ipk
```

## Key Patterns

### Dynamic Recompiler Selection
- ARM/ARM64: Uses `ari64` dynarec by default (`new_dynarec/`)
- x86_64 and others: Uses `lightrec` JIT
- Set via `./configure --dynarec=` or `DYNAREC=` make variable

### GPU Plugin Selection
- `BUILTIN_GPU=neon` - ARM NEON / x86 SSE2+ (best performance)
- `BUILTIN_GPU=unai` - Software renderer (compatibility)
- `BUILTIN_GPU=peops` - P.E.Op.S. plugin

### Conditional Compilation
Heavy use of `#ifdef` for platform/architecture-specific code:
- `HAVE_NEON_ASM` - ARM NEON assembly
- `DYNAREC=ari64` vs `DYNAREC=lightrec`
- Platform defines: `PANDORA`, `MAEMO`, `MIYOO`

### Threading
Enabled via `./configure --enable-threads`:
- `USE_ASYNC_CDROM` - Async CD-ROM operations
- `USE_ASYNC_GPU` - Async GPU rendering
- `NDRC_THREAD` - Threaded dynarec
