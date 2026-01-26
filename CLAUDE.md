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

### Clean Build
```bash
make clean                # Remove build artifacts
./webos-package.sh --clean  # Clean WebOS artifacts
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
- Target: ARMv7 with NEON (legacy webOS devices)

### WebOS Platform Files (`frontend/`)
- `plat_webos.c`, `plat_webos.h` - PDL (Palm Device Library) initialization
- PDL must be initialized before SDL for proper GPU access

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
- PalmPDK installed at `/opt/PalmPDK` (provides headers for SDL, GLES, PDL)
- ARM cross-compiler (e.g., `arm-linux-gnueabi-gcc`) for building
- Git submodules initialized

### PalmPDK Structure
```
/opt/PalmPDK/
├── include/           # Headers for cross-compilation
│   ├── SDL/           # SDL 1.2 headers
│   ├── GLES/          # OpenGL ES 1.1 headers
│   ├── GLES2/         # OpenGL ES 2.0 headers
│   └── PDL.h          # Palm Device Library header
└── device/lib/        # ARM device libraries (reference only)
    ├── libSDL-1.2.so
    ├── libGLES_CM.so  # OpenGL ES 1.1
    ├── libpdl.so      # Palm Device Library
    └── libpng12.so
```

### GPU Acceleration
WebOS builds use OpenGL ES 1.1 (libGLES_CM.so) for hardware-accelerated display output:
- PSX rendering uses NEON-optimized software renderer
- Final display uses OpenGL ES for fast scaling/composition
- Select "OpenGL" video output mode in the menu for best performance

### WebOS OpenGL Without Touch Flicker
**CRITICAL**: WebOS has a 3-layer display system. Using EGL directly causes screen flicker on touch events.

**Solution**: Use SDL's built-in OpenGL support instead of EGL:
```c
// CORRECT - No flicker
SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
SDL_SetVideoMode(w, h, 16, SDL_OPENGL | SDL_FULLSCREEN);
SDL_GL_SwapBuffers();

// WRONG - Causes flicker
eglGetDisplay(...);
eglCreateContext(...);
eglSwapBuffers(...);
```

**Implementation**: See `frontend/libpicofe/gl_webos.c` for the WebOS-specific GL code.

**Linking**: Link directly against `libGLES_CM.so`, NOT `libEGL.so`:
```makefile
LDLIBS += -lGLES_CM  # Correct
# Do NOT use: -lEGL
```

See `WEBOS_GL_NOTES.md` for detailed documentation.

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
- `HAVE_GLES` - OpenGL ES support
- `DYNAREC=ari64` vs `DYNAREC=lightrec`
- Platform defines: `PANDORA`, `MAEMO`, `MIYOO`, `WEBOS`

### Threading
Enabled via `./configure --enable-threads` (on by default):
- `USE_ASYNC_CDROM` - Async CD-ROM operations
- `USE_ASYNC_GPU` - Async GPU rendering
- `NDRC_THREAD` - Threaded dynarec

### Save States
The emulator supports save/load states via `freeze/unfreeze` functions in each component (CPU, GPU, SPU, etc.).

## WebOS Touchscreen Controls

### Architecture
Touch controls are implemented in `frontend/in_webos_touch.c` with two modes:
- **Game mode**: Full PlayStation controller layout (D-pad, face buttons, shoulders, start/select, menu)
- **Menu mode**: 6-button navigation (UP, DOWN, LEFT, RIGHT as D-pad on left; OK, BACK on right)

### Multitouch Support
WebOS SDL 1.2 has Palm-specific multitouch extensions:
```c
// SDL mouse events include 'which' field for finger index (0-4)
finger_id = event->button.which;  // Not always 0!
if (finger_id >= MAX_FINGERS)
    finger_id = 0;

// Can also poll directly:
SDL_GetMultiMouseState(int which, int *x, int *y);  // Up to SDL_MAXMOUSE (5) fingers
```

### Data Folder Path
The emulator stores all user data in `/media/internal/.pcsx/` (hidden folder):
- BIOS: `/media/internal/.pcsx/bios/`
- Memory cards: `/media/internal/.pcsx/memcards/`
- Save states: `/media/internal/.pcsx/sstates/`
- Config: `/media/internal/.pcsx/pcsx.cfg`

This is defined in `frontend/main.h` as `PCSX_DOT_DIR "/.pcsx/"`.

### Touch Zone Positioning (HP TouchPad 1024x768)
Controls are positioned for comfortable thumb reach in landscape mode:
- D-pad: Left side, y=525-765 (cardinal) with diagonal zones
- Action buttons: Right side, y=525-765
- Shoulder buttons: Top corners, y=338-458 (L1/L2 left, R1/R2 right)
- Start/Select: Bottom center at screen edge (y=708)
- Menu button: Top center (y=0, always accessible)

### Diagonal D-Pad Zones
Invisible touch zones between cardinal directions allow diagonal input:
```c
/* D-Pad diagonal zones (no outline drawn, empty label) */
{ 160, 525, 80, 80,  DKEY_UP_RIGHT,   "" },
{ 0,   525, 80, 80,  DKEY_UP_LEFT,    "" },
{ 160, 685, 80, 80,  DKEY_DOWN_RIGHT, "" },
{ 0,   685, 80, 80,  DKEY_DOWN_LEFT,  "" },
```
Special key values (-20 to -23) are mapped to combined button presses in `update_buttons()`.

### Button Overlay Rendering
- Draw button outlines only (not filled) to avoid obscuring game content
- Fill buttons only when pressed (visual feedback)
- Use `SDL_FillRect()` for filled areas, custom `draw_rect_outline_sdl()` for borders
- Draw overlay after game frame in `plat_video_menu_end()` for all render paths (YUV, GL, software)

### Resolution Changes and Ghost Touch Controls
**Problem**: When PS1 games change resolution (e.g., 640x480 → 320x240), touch control overlays from the previous frame appear duplicated or "ghosted" on screen.

**Root Cause**: The WebOS compositor doesn't clear areas outside a shrinking SDL window. When the SDL surface shrinks, old touch control pixels remain visible in the compositor's buffer beyond the new window boundaries.

**Solution** (in `frontend/plat_sdl.c` `change_mode()`): Before changing to a smaller resolution, briefly create a full-screen surface, clear it, double-flip for double buffering, then switch to the desired resolution:
```c
#ifdef WEBOS
    /* On WebOS, the compositor doesn't clear areas outside a shrinking window.
     * Briefly create a full-screen surface and clear it to remove ghost pixels. */
    if (plat_target.vout_method == 0 && fs_w && fs_h) {
      SDL_Surface *tmp = SDL_SetVideoMode(fs_w, fs_h, 16, flags | SDL_FULLSCREEN);
      if (tmp) {
        if (SDL_MUSTLOCK(tmp))
          SDL_LockSurface(tmp);
        memset(tmp->pixels, 0, tmp->pitch * tmp->h);
        if (SDL_MUSTLOCK(tmp))
          SDL_UnlockSurface(tmp);
        SDL_Flip(tmp);
        SDL_Flip(tmp); /* Double flip for double buffering */
      }
    }
#endif
    plat_sdl_screen = SDL_SetVideoMode(set_w, set_h, 16, flags);
```

This forces the compositor to update its entire buffer, eliminating ghost pixels. Only applies to software rendering mode (vout_method == 0).

### PNG Icon Loading
Menu and game buttons use PNG icons with alpha transparency:
```
webos/
├── menu-up.png, menu-down.png        # Menu UP/DOWN navigation
├── menu-forward.png, menu-backward.png  # Menu LEFT/RIGHT navigation
├── control-circle.png                # Menu OK button (PlayStation O)
├── control-cross.png                 # Menu BACK button (PlayStation X)
├── control-triangle.png, control-square.png  # Game action buttons
```

Icons are loaded at init via `load_icon_png()` using libpng, stored as RGBA pixel data, and blitted with alpha blending to RGB565 surface. The `blit_icon()` function handles scaling and per-pixel alpha blending.

### Control Opacity
In-game controls use 60% opacity for both borders and pressed highlights:
```c
#define BORDER_ALPHA 153    /* 60% of 255 */
#define PRESSED_ALPHA 153   /* 60% of 255 */
```
Colors are alpha-blended per-pixel in `draw_rect_with_alpha()` and `draw_rect_outline_alpha()`.

### Menu Integration
The menu system uses `PBTN_*` constants from `libpicofe/input.h`:
- `PBTN_UP`, `PBTN_DOWN` - Navigation
- `PBTN_MOK` - Confirm/OK
- `PBTN_MBACK` - Back/Cancel

**Important**: `frontend/libpicofe/input.c` requires local modification to call `webos_touch_get_menu_buttons()`. Add to Makefile:
```makefile
frontend/libpicofe/input.o: CFLAGS += -DWEBOS
```

### Loading Screen
Display "Loading..." during game load to provide user feedback:
```c
// In plat_sdl.c - plat_video_show_loading()
memset(plat_sdl_screen->pixels, 0, ...);  // Clear to black
basic_text_out16_nf(pixels, w, x, y, "Loading...");  // From libpicofe/fonts.h
SDL_Flip(plat_sdl_screen);
```
Call from `menu.c` in `run_cd_image()` before heavy loading operations.

### Menu Button Input Handling

The menu system in libpicofe uses `in_menu_wait()` which polls for input with timeouts and auto-repeat logic. Touch input requires special handling to work correctly with this model.

**Problem**: Touch events are instantaneous, but the menu expects sustained key states. Various approaches were tested:

| Approach | Description | Result |
|----------|-------------|--------|
| One-shot | Set pending buttons on press, clear after read | Unreliable - timing issues |
| MinHold | Hold buttons active for 150ms after release | Works but feels sluggish |
| Debounce | 250ms cooldown between presses | Prevents rapid navigation |
| Queue | Buffer events, return one at a time | Good but complex |
| KeyInject | Inject KEY_DOWN on press, KEY_UP on release | Timing issues, stuck keys |
| **TapKey** | Inject complete keystroke on tap | **Best - recommended** |

**Recommended Solution (TapKey)**:
Each tap immediately injects a complete keystroke (KEY_DOWN + KEY_UP). No state tracking needed - one tap = one menu action.

```c
static void inject_key_event(SDLKey key, int pressed)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.sym = key;
    SDL_PushEvent(&event);
}

// In webos_touch_event(), on SDL_MOUSEBUTTONDOWN in menu mode:
if (menu_mode && zone >= 0) {
    SDLKey sdlkey = menu_key_to_sdlkey(zones[zone].key);
    if (sdlkey != SDLK_UNKNOWN) {
        inject_key_event(sdlkey, 1);  /* KEY_DOWN */
        inject_key_event(sdlkey, 0);  /* KEY_UP */
    }
}
```

This approach is simpler and more reliable than tracking press/release separately:
- No state synchronization issues
- No stuck keys possible
- No timing-dependent behavior
- Each tap produces exactly one menu action

**Menu Key Mapping**:
```c
MENU_KEY_UP    -> SDLK_UP     -> PBTN_UP
MENU_KEY_DOWN  -> SDLK_DOWN   -> PBTN_DOWN
MENU_KEY_LEFT  -> SDLK_LEFT   -> PBTN_LEFT
MENU_KEY_RIGHT -> SDLK_RIGHT  -> PBTN_RIGHT
MENU_KEY_MOK   -> SDLK_RETURN -> PBTN_MOK
MENU_KEY_MBACK -> SDLK_ESCAPE -> PBTN_MBACK
```

**Mode Switching**:
When switching between game and menu modes, flush stale touch events:
```c
while (SDL_PeepEvents(&event, 1, SDL_GETEVENT,
       SDL_EVENTMASK(SDL_MOUSEBUTTONDOWN) |
       SDL_EVENTMASK(SDL_MOUSEBUTTONUP) |
       SDL_EVENTMASK(SDL_MOUSEMOTION)) > 0) {
    /* discard */
}
```

## Development Notes

### Quick Build & Install Cycle
```bash
# Remove old package, rebuild, and install
palm-install -r com.starkka.pcsxrearmed 2>/dev/null
rm -f webos/pcsx  # Force fresh binary copy
CROSS_COMPILE=arm-linux-gnueabi- ./webos-package.sh
palm-install com.starkka.pcsxrearmed_*.ipk
```

### Debugging on Device
View app logs via novaterm or SSH:
```bash
# On device
tail -f /var/log/messages | grep -i pcsx
```

### Test Variants
The `menu_test_variants/` directory contains alternative implementations of `in_webos_touch.c` for testing different menu button approaches (MinHold, Debounce, Queue, SyncPoll, KeyInject). Use `build_menu_variants.sh` to build all variants with unique app IDs for side-by-side comparison. Note: The current implementation uses TapKey which supersedes all these approaches.

### Default Browse Directory
The file browser defaults to `/media/internal/.pcsx` with fallbacks to `/media/internal` then `/media`. This is configured in `frontend/menu.c` in the `menu_do_last_cd_img()` function.
