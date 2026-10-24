#!/bin/bash
# Prepara a toolchain e as dependências do build Windows (Linux ou WSL, x86_64).
#
#   tools/setup_deps.sh [pasta]        (padrão: ~/bmu-deps)
#
# Baixa e compila, tudo dentro de <pasta>:
#   llvm-mingw   toolchain clang + mingw-w64 + libc++ (release pronta do mstorsjo/llvm-mingw)
#   vk/          Vulkan-Headers
#   sdl2-win/    SDL2 estático (janela, controles)
#   vk-win/      glslang + SPIRV-Tools estáticos (shaders do jogo -> SPIR-V)
# e no fim imprime as variáveis para o tools/build_cross.sh.
#
# Precisa de: git, curl, tar, cmake 3.20+, ninja, python3.
# Os builds de referência usaram exatamente estas versões, mas com uma
# llvm-mingw compilada localmente a partir dos scripts do próprio llvm-mingw
# (clang 18); a release pronta usa um clang mais novo e deve funcionar igual.
set -e
D="$(mkdir -p "${1:-$HOME/bmu-deps}" && cd "${1:-$HOME/bmu-deps}" && pwd)"
LLVM_MINGW_REL="20261020"
LLVM_MINGW_PKG="llvm-mingw-${LLVM_MINGW_REL}-ucrt-ubuntu-22.04-x86_64"
SDL_TAG="release-2.30.12"
VK_TAG="v1.4.366"
GLSLANG_TAG="vulkan-sdk-1.4.363.0"
J="${JOBS:-$(nproc)}"
cd "$D"

if [ ! -x "$D/llvm-mingw/bin/x86_64-w64-mingw32-clang" ]; then
    echo "==> llvm-mingw $LLVM_MINGW_REL"
    curl -fL -o llvm-mingw.tar.xz \
        "https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_REL}/${LLVM_MINGW_PKG}.tar.xz"
    tar xf llvm-mingw.tar.xz && rm llvm-mingw.tar.xz
    mv "$LLVM_MINGW_PKG" llvm-mingw
fi
export BMU_LLVM_MINGW="$D/llvm-mingw" BMU_DEPS="$D"
TC=(-DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=x86_64
    -DCMAKE_C_COMPILER="$D/llvm-mingw/bin/x86_64-w64-mingw32-clang"
    -DCMAKE_CXX_COMPILER="$D/llvm-mingw/bin/x86_64-w64-mingw32-clang++"
    -DCMAKE_RC_COMPILER="$D/llvm-mingw/bin/x86_64-w64-mingw32-windres"
    -DCMAKE_BUILD_TYPE=Release -G Ninja)

clone() { [ -d "$2" ] || git clone --depth 1 --branch "$3" "$1" "$2"; }

echo "==> Vulkan-Headers $VK_TAG"
clone https://github.com/KhronosGroup/Vulkan-Headers.git src-vulkan-headers "$VK_TAG"
cmake -S src-vulkan-headers -B b-vh -G Ninja -DCMAKE_INSTALL_PREFIX="$D/vk" >/dev/null
cmake --install b-vh >/dev/null

echo "==> SDL2 $SDL_TAG (estático)"
clone https://github.com/libsdl-org/SDL.git src-sdl2 "$SDL_TAG"
cmake -S src-sdl2 -B b-sdl "${TC[@]}" -DCMAKE_INSTALL_PREFIX="$D/sdl2-win" \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF >/dev/null
cmake --build b-sdl -j "$J" && cmake --install b-sdl >/dev/null

echo "==> glslang $GLSLANG_TAG + SPIRV-Tools (estáticos)"
clone https://github.com/KhronosGroup/glslang.git src-glslang "$GLSLANG_TAG"
( cd src-glslang && python3 update_glslang_sources.py )
cmake -S src-glslang -B b-glslang "${TC[@]}" -DCMAKE_INSTALL_PREFIX="$D/vk-win" \
    -DENABLE_OPT=ON -DGLSLANG_TESTS=OFF -DBUILD_SHARED_LIBS=OFF -DENABLE_GLSLANG_BINARIES=OFF \
    -DSPIRV_SKIP_TESTS=ON -DSPIRV_SKIP_EXECUTABLES=ON >/dev/null
cmake --build b-glslang -j "$J" && cmake --install b-glslang >/dev/null

cat <<MSG

Pronto. Antes do build:
  export BMU_LLVM_MINGW="$D/llvm-mingw"
  export BMU_DEPS="$D"
  tools/build_cross.sh <pasta do ps3recomp>
MSG
