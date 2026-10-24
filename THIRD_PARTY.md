# Componentes de terceiros

Este repositório não inclui código de terceiros: os componentes abaixo são baixados e compilados durante o build (`tools/setup_deps.sh` e `tools/build_cross.sh`) e acabam dentro dos executáveis gerados.

| Componente | Uso | Licença |
|---|---|---|
| [ps3recomp](https://github.com/sp00nznet/ps3recomp) (sp00nznet) | lifter PPU/SPU e runtime HLE que substitui o sistema do PS3; alterado por `patches/ps3recomp-mingw.patch` | MIT |
| [SDL2](https://github.com/libsdl-org/SDL) | janela, controles (DualShock 4, DualSense, Xbox) | zlib |
| [glslang](https://github.com/KhronosGroup/glslang) | tradução dos shaders do jogo para SPIR-V | BSD-3-Clause / Apache-2.0 (vários) |
| [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools) | otimização de SPIR-V (dentro do glslang) | Apache-2.0 |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | cabeçalhos da API Vulkan | Apache-2.0 / MIT |
| [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) | toolchain (clang, mingw-w64, libc++/libunwind estáticos) | Apache-2.0 com exceção LLVM; mingw-w64: ZPL/domínio público |

Referências usadas no desenvolvimento, sem código copiado para cá:

- [RPCS3](https://github.com/RPCS3/rpcs3): algoritmos de RAP→RIF e unself. As chaves **não** estão neste repositório; `tools/unself_npdrm.py` e `tools/extract_pkg.py` as leem do código-fonte do RPCS3 que o usuário já tem.
- [flOw (sp00nznet)](https://github.com/sp00nznet/flow): arquitetura de port com o ps3recomp.
- [ARMORED CORE for Answer PC port](https://github.com/SulphurS16/ACFA-Port): modelo de organização e de cuidado com conteúdo do jogo.

O driver de vídeo do usuário fornece o `vulkan-1.dll` em tempo de execução.
