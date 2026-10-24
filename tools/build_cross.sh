#!/bin/bash
# Bomberman ULTRA -- build completo (Linux/WSL -> Windows x64).
#
# Precisa:
#   - o jogo do usuário em game/ (ver README): game/PS3_GAME/USRDIR/EBOOT.elf
#     decriptado, media.zip, Bman.bnk e game/PS3_GAME/ICON0.PNG;
#   - o ps3recomp (https://github.com/sp00nznet/ps3recomp) no commit indicado
#     no README; patches/ps3recomp-mingw.patch é aplicado aqui;
#   - toolchain llvm-mingw + SDL2/glslang/Vulkan-Headers para Windows
#     (tools/setup_deps.sh; prefixos em BMU_LLVM_MINGW e BMU_DEPS);
#   - python3, cmake, ninja.
#
# Gera bin/BombermanUltra.exe (x86-64-v3, AVX2), bin/BombermanUltra_compat.exe
# (x86-64-v2) e BombermanUltraLauncher.exe na raiz.
#
# Uso: tools/build_cross.sh <ps3recomp>
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PS3RECOMP="$(cd "${1:?caminho do ps3recomp}" && pwd)"
T="$PS3RECOMP/tools"
ELF="$ROOT/game/PS3_GAME/USRDIR/EBOOT.elf"
DEPS="${BMU_DEPS:-/opt}"
JOBS="${JOBS:-$(nproc)}"

for f in "$ELF" "$ROOT/game/PS3_GAME/USRDIR/media.zip" "$ROOT/game/PS3_GAME/ICON0.PNG"; do
    [ -f "$f" ] || { echo "falta $f -- veja 'Instalação' no README"; exit 1; }
done

echo "==> ps3recomp: aplicando patches/ps3recomp-mingw.patch"
( cd "$PS3RECOMP" && git apply --check "$ROOT/patches/ps3recomp-mingw.patch" 2>/dev/null \
    && git apply "$ROOT/patches/ps3recomp-mingw.patch" ) || echo "    (já aplicado)"

echo "==> 1. PPU: loader + lifter + tabela HLE"
python3 "$T/ppu_loader.py" "$ELF" -o "$ROOT/analysis/loader"
python3 "$T/ppu_lifter.py" "$ELF" --functions "$ROOT/analysis/loader/EBOOT.functions.json" \
    --hle-stubs "$ROOT/analysis/loader/EBOOT.imports.json" --output "$ROOT/recompiled" -j "$JOBS"
python3 "$T/gen_hle_nids.py" --all --out "$ROOT/recompiled/ppu_hle_nids.cpp"

echo "==> 2. SPU: MultiStream (ELF embutido) + jobs SPURS de render"
python3 "$ROOT/tools/extract_spu_jobs.py" "$ELF" "$ROOT/spu_jobs"
mkdir -p "$ROOT/spu_gen2"
python3 "$T/build_spu_workloads.py" --lifted "$ROOT/spu_gen2" --out "$ROOT/spu_gen2/spu_workloads.c" \
    --constructor --input "$ELF" --input "$ROOT/spu_jobs/job_1a4000.elf"
# O job 0x199A80 tem tabelas depois de 0x9320: lift manual com --code-end.
python3 "$T/spu_lifter.py" "$ROOT/spu_jobs/job_199a80.elf" --auto-functions "$ROOT/spu_jobs/job_199a80.elf" \
    --code-end 0x9320 --base 0 --symbol-prefix bmujob_render -o "$ROOT/spu_gen2/bmujob_render"
cp "$ROOT/src/spu/jobmods_register.c" "$ROOT/spu_gen2/"

echo "==> 3. Ícones e tradução PT-BR (a partir dos arquivos do usuário)"
python3 "$ROOT/tools/make_icon.py" "$ROOT/game/PS3_GAME/ICON0.PNG" "$ROOT/src/bomberman.ico" "$ROOT/launcher/bomberman.ico"
python3 - "$ROOT" <<'PY'
import sys, zipfile, os
root = sys.argv[1]
z = zipfile.ZipFile(os.path.join(root, 'game/PS3_GAME/USRDIR/media.zip'))
os.makedirs(os.path.join(root, 'build-assets'), exist_ok=True)
open(os.path.join(root, 'build-assets/strings_en.xus'), 'wb').write(z.read('strings_en.xus'))
PY
python3 "$ROOT/tools/build_ptbr.py" "$ROOT/build-assets/strings_en.xus" "$ROOT/traducao/strings_ptbr.xus"

build() {   # build <pasta> <-march> <nome do exe>
    cmake -S "$ROOT" -B "$ROOT/$1" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/mingw-cross.cmake" \
        -DCMAKE_BUILD_TYPE=Release -DPS3RECOMP_DIR="$PS3RECOMP" -DRECOMP_DIR="$ROOT/recompiled" \
        -DPORT_SPU_INPUTS= -DSPU_DIR="$ROOT/spu_gen2" -DBMU_EXE_NAME="$3" \
        -DPS3RECOMP_RSX_VULKAN=ON -DPS3RECOMP_VULKAN_INCLUDE_DIR="$DEPS/vk/include" \
        -DCMAKE_PREFIX_PATH="$DEPS/sdl2-win;$DEPS/vk-win" \
        -DCMAKE_C_FLAGS="-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -march=$2" \
        -DCMAKE_CXX_FLAGS="-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -march=$2"
    cmake --build "$ROOT/$1" -j "$JOBS"
}
echo "==> 4. Compilação: versão compat (x86-64-v2)"
build build-win-compat x86-64-v2 BombermanUltra_compat.exe
echo "==> 5. Compilação: versão AVX2 (x86-64-v3) + launcher"
build build-win x86-64-v3 BombermanUltra.exe
cp "$ROOT/build-win/launcher/BombermanUltraLauncher.exe" "$ROOT/" 2>/dev/null || true
echo "OK: bin/BombermanUltra.exe, bin/BombermanUltra_compat.exe e BombermanUltraLauncher.exe"
