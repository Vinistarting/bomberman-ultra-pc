# Componentes de terceiros

Este repositório não inclui código de terceiros. Os componentes abaixo são baixados durante o build — pelo botão **Preparar jogo** do launcher (`tools/build_windows.py`, tudo em `toolchain/`) ou por `tools/setup_deps.sh` + `tools/build_cross.sh` — e os de código acabam dentro dos executáveis gerados no PC do usuário.

| Componente | Uso | Licença |
|---|---|---|
| [ps3recomp](https://github.com/sp00nznet/ps3recomp) (sp00nznet) | lifter PPU/SPU e runtime HLE que substitui o sistema do PS3; alterado por `patches/ps3recomp-mingw.patch` | MIT |
| [SDL2](https://github.com/libsdl-org/SDL) | janela, controles (DualShock 4, DualSense, Xbox) | zlib |
| [glslang](https://github.com/KhronosGroup/glslang) | tradução dos shaders do jogo para SPIR-V | BSD-3-Clause / Apache-2.0 (vários) |
| [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools) | otimização de SPIR-V (dentro do glslang) | Apache-2.0 |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | cabeçalhos da API Vulkan | Apache-2.0 / MIT |
| [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) | toolchain (clang, mingw-w64, libc++/libunwind estáticos) | Apache-2.0 com exceção LLVM; mingw-w64: ZPL/domínio público |
| [Python](https://www.python.org) (pacote *embeddable* 3.12) | roda o `build_windows.py` e o ps3recomp; só no build | PSF License |
| [CMake](https://cmake.org) | build; só no build | BSD-3-Clause |
| [Ninja](https://ninja-build.org) | build; só no build | Apache-2.0 |
| [RPCS3](https://github.com/RPCS3/rpcs3) `rpcs3/Crypto/key_vault.h` e `key_vault.cpp` (commit fixo) | lidos como dados pelos scripts para extrair o PKG e decriptar o EBOOT com a licença do usuário; só no build, nada é compilado deles | GPL-2.0 |

Referências usadas no desenvolvimento, sem código copiado para cá:

- [RPCS3](https://github.com/RPCS3/rpcs3): algoritmos de PKG, RAP→RIF e unself (portados para Python em `tools/extract_pkg.py` e `tools/unself_npdrm.py`). As chaves **não** estão neste repositório: são lidas dos `key_vault` do RPCS3, baixados no PC do usuário durante o build (ou de um código-fonte local, com `--rpcs3-crypto`).
- [flOw (sp00nznet)](https://github.com/sp00nznet/flow): arquitetura de port com o ps3recomp.
- [ARMORED CORE for Answer PC port](https://github.com/SulphurS16/ACFA-Port): modelo de organização e de cuidado com conteúdo do jogo.

O driver de vídeo do usuário fornece o `vulkan-1.dll` em tempo de execução.

O pacote de release traz só o `BombermanUltraLauncher.exe` compilado (com llvm-mingw: runtime mingw-w64 e libc++/libunwind estáticos) e o código deste repositório.
