# Bomberman ULTRA — unofficial PC port

Native Windows port of **Bomberman ULTRA** (PlayStation Network, Hudson Soft, 2009), built by static recompilation: the PS3's PowerPC and SPU code is translated to C/C++ and compiled for x86-64, with the PS3 system replaced by an HLE layer ([ps3recomp](https://github.com/sp00nznet/ps3recomp)). It is not an emulator.

> **Not affiliated with, endorsed or sponsored by Konami, Hudson Soft or Sony Interactive Entertainment.** Bomberman is a trademark of Konami Digital Entertainment; PlayStation is a trademark of Sony Interactive Entertainment. **This repository contains no game files** — no code, art, audio, text or license. You need your own copy of the game.

[Português](README.md) · [Technical notes (PT)](docs/TECNICO.md) · [Changelog](CHANGELOG.md) · [Contributing](CONTRIBUTING.md)

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

You need: your own European `.pkg` and its `.rap` license; Windows 10/11 x64, a Vulkan GPU, about **6 GB free** and an internet connection for the first build (~350 MB of tools). RPCS3 is **not** required.

1. Download `BombermanUltra-PC-<version>.zip` from [Releases](https://github.com/Vinistarting/bomberman-ultra-pc/releases) and extract it to a short path such as `C:\Games\BombermanUltra`. Leave the `.pkg` and `.rap` in the port folder, Downloads, Desktop or Documents and the launcher finds them (or point to them).
2. Run **BombermanUltraLauncher.exe** (SmartScreen: *More info → Run anyway*; the launcher is unsigned), click **Preparar jogo** (prepare game) and choose **Selecionar o PKG do jogo** (select the game PKG). The PKG is recognized by its header, so the file name doesn't matter.
3. A progress bar in the launcher follows the work (it can be cancelled and resumed) while it extracts only what the port uses from the PKG into `game\PS3_GAME`, decrypts `EBOOT.BIN` with your license (checked against the European 01.00 hash), downloads the toolchain into `toolchain\` (embedded Python, RPCS3's public `key_vault` files at a pinned commit, CMake, Ninja, llvm-mingw, ps3recomp, SDL2, glslang; all prebuilt files are hash-checked) and builds the game: **10–40 minutes** the first time.
4. Press **JOGAR** (play).

The other option, *Usar o jogo instalado no RPCS3*, imports `dev_hdd0\game\NPEB00076` from RPCS3 (with `EBOOT.elf` from *Utilities → Decrypt PS3 Binaries*).

The game executable is always built on your machine from your files; no game binaries are distributed. Nothing is installed system-wide. If the build stops, clicking **Preparar jogo** again resumes; the full log is `toolchain\build.log`. To update, extract a newer release over the folder (`game\`, `user\`, `toolchain\` and `launcher.ini` are kept) and click **Preparar jogo** again: only changed steps are redone.

Linux/WSL alternative: `tools/setup_deps.sh` + `tools/build_cross.sh` (see the [Portuguese README](README.md#alternativa-compilar-no-linux-ou-wsl)).

## Controls

Keyboard: arrows = d-pad, Z = ✕, X = ○, A = □, S = △, Q/W = L1/R1, 1/2 = L2/R2, Enter = START, Tab = SELECT. Controllers follow the PlayStation layout by button **position**. Hold **SELECT + START** for half a second to quit (Share + Options on DualShock 4, Create + Options on DualSense, View + Menu on Xbox).

## Known issues

PSN online play and leaderboards are gone (local multiplayer works) · text drawn as images stays in English · USB camera unsupported · NVIDIA untested on hardware.

## Credits and license

ps3recomp and flOw by sp00nznet; RPCS3 as format reference; SDL2, glslang/SPIRV-Tools, Vulkan-Headers and llvm-mingw (see [THIRD_PARTY.md](THIRD_PARTY.md)); ACFA-Port by SulphurS16 as repository reference.

The code in this repository is **open source** under the [MIT license](LICENSE). That covers this code only: the game — its code, art, audio and text — belongs to its owners and is neither distributed nor licensed by this project.
