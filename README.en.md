# Bomberman ULTRA — unofficial PC port

Native Windows port of **Bomberman ULTRA** (PlayStation Network, Hudson Soft, 2009), built by static recompilation: the PS3's PowerPC and SPU code is translated to C/C++ and compiled for x86-64, with the PS3 system replaced by an HLE layer ([ps3recomp](https://github.com/sp00nznet/ps3recomp)). It is not an emulator.

> **Not affiliated with, endorsed or sponsored by Konami, Hudson Soft or Sony Interactive Entertainment.** Bomberman is a trademark of Konami Digital Entertainment; PlayStation is a trademark of Sony Interactive Entertainment. **This repository contains no game files** — no code, art, audio, text or license. You need your own copy of the game.

[Português](README.md) · [Technical notes (PT)](docs/TECNICO.md) · [Changelog](CHANGELOG.md)

![Gameplay at 1080p](docs/gameplay.png)

## ⚠ Required license: European release

The port is built from **one specific release** and **only runs with its license**:

| | |
|---|---|
| Game | Bomberman ULTRA — **Europe** |
| Title ID | `NPEB00076` |
| Content ID | `EP0555-NPEB00076_00-BOMBERMANGAME001` |
| License | `EP0555-NPEB00076_00-BOMBERMANGAME001.rap` |

The `.rap` is the license of your PSN purchase, obtained from your own PS3/account (tools for that are not part of this project). The launcher searches for it automatically and has a **Localizar licença** (locate license) button. Without the right license the PLAY button stays disabled and `BombermanUltra.exe` refuses to start. Other regions (US `NPUB…`, Japan `NPJB…`) ship a different EBOOT and do not work with this port.

## Highlights

- Native 64-bit, Vulkan, **locked 60 FPS** like the PS3 (game logic is tied to the frame); can be unlocked.
- Render resolution from 720p to 4K, borderless fullscreen, FPS counter.
- **Brazilian Portuguese translation** (734 of 736 strings) plus all original languages.
- **Controllers**: DualShock 4, DualSense (USB/Bluetooth, rumble, player color on the light bar), Xbox and generic pads, hot-plug, up to 4 local players; keyboard for player 1.
- **Steam / Big Picture**: "Adicionar à Steam" button; hold **SELECT + START** to quit from the controller.
- Launcher (Portuguese UI) with file checks, license, language, audio and video options.
- Local saves. No RPCS3, firmware or `dev_flash` needed to play.

## Compatibility

Windows 10/11 x64 · any GPU with **Vulkan 1.0** · x86-64 CPU with AVX2 (main build) or SSE4.2 (`compat` build, picked automatically).

Tested on **AMD** Radeon RX 580 (Windows 11, full game at 60 FPS) and **SwiftShader** (software Vulkan on Linux, boot → match). **NVIDIA has not been tested on real hardware yet**; the backend was audited for it (format support queried with fallbacks, optional features only enabled when reported, `minImageCount + 1` swapchain, discrete GPU preferred on hybrid laptops, every generated shader validates as Vulkan 1.0 SPIR-V, `VK_SUBOPTIMAL_KHR` handled without recreate loops). Reports from NVIDIA users (with `logs\bomberman.log`) are very welcome.

## Install

You need: your own European PKG and RAP; RPCS3 installed **or** its source code (our tools read the keys from `rpcs3/Crypto/key_vault.h`; no keys are shipped here); Linux or **WSL** with `git curl cmake ninja-build python3 python3-pip` and `pip install cryptography pillow`; about 2 GB free (a full build of both executables took 11 minutes on 2 cores).

```sh
git clone https://github.com/Vinistarting/bomberman-ultra-pc.git
git clone https://github.com/sp00nznet/ps3recomp.git
git -C ps3recomp checkout a6d76c895f77eb30b90a806c830544e61821b77f
cd bomberman-ultra-pc

# 1. game files -> game/PS3_GAME (via RPCS3: install the PKG, copy dev_hdd0/game/NPEB00076/*)
python3 tools/extract_pkg.py NPEB00076.pkg /tmp/pkg --rpcs3-crypto <rpcs3>/rpcs3/Crypto
mkdir -p game && cp -r /tmp/pkg/NPEB00076 game/PS3_GAME

# 2. decrypt EBOOT (or RPCS3: Utilities -> Decrypt PS3 Binaries)
python3 tools/unself_npdrm.py game/PS3_GAME/USRDIR/EBOOT.BIN game/PS3_GAME/USRDIR/EBOOT.elf \
    --rap EP0555-NPEB00076_00-BOMBERMANGAME001.rap --rpcs3-crypto <rpcs3>/rpcs3/Crypto

# 3. toolchain (once) and build
tools/setup_deps.sh ~/bmu-deps
export BMU_LLVM_MINGW=~/bmu-deps/llvm-mingw BMU_DEPS=~/bmu-deps
tools/build_cross.sh ../ps3recomp
```

Then run **BombermanUltraLauncher.exe** on Windows, let it find the license (or point to it) and press **JOGAR**. Executables are always built on your machine from your files; no prebuilt binaries are distributed.

## Controls

Keyboard: arrows = d-pad, Z = ✕, X = ○, A = □, S = △, Q/W = L1/R1, 1/2 = L2/R2, Enter = START, Tab = SELECT. Controllers follow the PlayStation layout by button **position**. Hold **SELECT + START** for half a second to quit (Share + Options on DualShock 4, Create + Options on DualSense, View + Menu on Xbox).

## Known issues

PSN online play and leaderboards are gone (local multiplayer works) · text drawn as images stays in English · USB camera unsupported · NVIDIA untested on hardware.

## Credits and license

ps3recomp and flOw by sp00nznet; RPCS3 as format reference; SDL2, glslang/SPIRV-Tools, Vulkan-Headers and llvm-mingw (see [THIRD_PARTY.md](THIRD_PARTY.md)); ACFA-Port by SulphurS16 as repository reference.

The code in this repository is **open source** under the [MIT license](LICENSE). That covers this code only: the game — its code, art, audio and text — belongs to its owners and is neither distributed nor licensed by this project.
