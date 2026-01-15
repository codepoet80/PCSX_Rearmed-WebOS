PCSX-ReARMed for WebOS
======================

A PlayStation 1 emulator for HP TouchPad and other WebOS devices.

Based on [PCSX-ReARMed](https://github.com/notaz/pcsx_rearmed) by notaz.

## Installation

1. Download the latest `.ipk` from Releases
2. Install via WebOS Quick Install or command line:
   ```bash
   palm-install com.starkka.pcsxrearmed_*.ipk
   ```

## File Paths

All emulator files are stored in `/media/internal/.pcsx/`:

| Content | Path |
|---------|------|
| **BIOS files** | `/media/internal/.pcsx/bios/` |
| **Game ISOs/BINs** | Anywhere, browse from menu (e.g., `/media/internal/games/`) |
| **Memory cards** | `/media/internal/.pcsx/memcards/` |
| **Save states** | `/media/internal/.pcsx/sstates/` |
| **Screenshots** | `/media/internal/.pcsx/screenshots/` |
| **Config** | `/media/internal/.pcsx/pcsx.cfg` |

### BIOS Setup

For best compatibility, copy a PlayStation BIOS file to `/media/internal/.pcsx/bios/`. Common BIOS files:
- `SCPH1001.BIN` (North America)
- `SCPH5501.BIN` (North America)
- `SCPH5500.BIN` (Japan)
- `SCPH5502.BIN` (Europe)

The emulator will run without a BIOS using HLE (High Level Emulation), but some games require a real BIOS.

## Controls

### Touchscreen (Game Mode)

On-screen controls are displayed as button outlines:
- **Left side**: D-pad (directional controls)
- **Right side**: Action buttons (Triangle, Circle, Cross, Square)
- **Top corners**: L1/L2 and R1/R2 shoulder buttons
- **Bottom center**: Select and Start
- **Top center**: Menu button

Multitouch is supported - press multiple buttons simultaneously.

### Touchscreen (Menu Mode)

Simplified controls at the bottom of the screen:
- **Bottom left**: UP / DOWN navigation
- **Bottom right**: BACK / OK actions

### Bluetooth Keyboard

Bluetooth keyboards work as standard input devices:

| Key | Action |
|-----|--------|
| Arrow Keys | D-pad |
| Z | Cross (X) |
| X | Circle |
| S | Square |
| D | Triangle |
| V | Start |
| C | Select |
| W | L1 |
| R | R1 |
| E | L2 |
| T | R2 |
| Escape | Open Menu |

## Video Settings

For best performance, select **OpenGL** video output in Options > Video. This uses hardware-accelerated display scaling.

---

## Building from Source

See [CLAUDE.md](CLAUDE.md) for detailed build instructions.

### Quick Start

```bash
git submodule init && git submodule update
./configure
make -j$(nproc)
./webos-package.sh
```

### Package Only (Skip Build)

If you have pre-built ARM binaries:

```bash
./webos-package.sh --skip-build
```

Place binaries in `webos/` first:
```
webos/
├── pcsx              # main binary
├── plugins/
│   └── *.so          # GPU/SPU plugins
└── skin/
    └── *.png         # UI assets
```

### Requirements

- [PalmSDK](https://github.com/pcolby/webOS-Open-SDK) at `/opt/PalmSDK`
- [PalmPDK](https://github.com/pcolby/webOS-Open-SDK) at `/opt/PalmPDK`
- ARM cross-compiler (e.g., `arm-linux-gnueabi-gcc`)

---

## Features

* ARM dynamic recompiler (Ari64)
* NEON GPU renderer (Exophase)
* P.E.Op.S. SPU (audio)
* BIOS HLE emulation
* Multitouch on-screen controls
* Bluetooth keyboard support

## Credits

- PCSX-ReARMed by notaz and contributors
- Original PCSX by PCSX Team
- WebOS port by Starkka15, codepoet80 and Claude

## License

GNU General Public License v2.0
