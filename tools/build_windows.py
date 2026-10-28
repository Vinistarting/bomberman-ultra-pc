#!/usr/bin/env python3
"""Bomberman ULTRA -- compila o port no próprio PC do usuário (Windows).

É o que o botão "Preparar jogo" do launcher roda, numa janela de console, com
o Python embutido que o launcher baixou. Só usa a biblioteca padrão.

  python tools/build_windows.py [--root <pasta do port>] [--jobs N]

O que ele faz, tudo dentro de <port>/toolchain (nada é instalado no sistema):

  0. com --pkg: extrai o PKG europeu do usuário para game/PS3_GAME e decripta
     o EBOOT.BIN com a licença dele (chaves dos key_vault do RPCS3, baixados
     num commit fixo e conferidos por hash);
  1. baixa CMake, Ninja e a toolchain llvm-mingw (clang + mingw-w64);
  2. baixa o ps3recomp no commit fixado e aplica patches/ps3recomp-mingw.patch;
  3. baixa e compila SDL2, Vulkan-Headers e glslang/SPIRV-Tools (estáticos);
  4. converte o jogo do usuário (game/PS3_GAME/USRDIR/EBOOT.elf) em C/C++
     com o ps3recomp: PPU, SPU (MultiStream + jobs de render);
  5. gera o ícone e a tradução PT-BR a partir dos arquivos do usuário;
  6. compila bin/BombermanUltra_compat.exe (x86-64-v2) e
     bin/BombermanUltra.exe (x86-64-v3, AVX2).

Cada etapa concluída deixa uma marca em toolchain/state/; rodar de novo
continua de onde parou. --clean apaga as marcas (não os downloads).

Para testar fora do Windows (Linux, com uma toolchain já pronta):
  --cmake, --ninja, --llvm-mingw apontam ferramentas existentes;
  --deps-prefix usa SDL2/glslang já compilados (pula a etapa 3);
  --src NOME=PASTA usa um código-fonte local em vez de baixar.
"""
import argparse
import hashlib
import io
import json
import os
import shutil
import struct
import subprocess
import sys
import time
import urllib.request
import zipfile
import zlib

PS3RECOMP_COMMIT = "a6d76c895f77eb30b90a806c830544e61821b77f"
LLVM_MINGW_REL = "20261020"
CMAKE_VER = "3.31.6"
NINJA_VER = "1.12.1"
SDL_TAG = "release-2.30.12"
VK_HEADERS_TAG = "v1.4.366"
GLSLANG_TAG = "vulkan-sdk-1.4.363.0"
SPIRV_TOOLS_COMMIT = "ef96ed763b43b59b33b31b362f09a02b729fa1c9"     # known_good.json do glslang
SPIRV_HEADERS_COMMIT = "496543121ce6419f23d6fa5d7194ba66c36212d2"

CONTENT_ID = "EP0555-NPEB00076_00-BOMBERMANGAME001"

# Chaves públicas do PS3 para abrir o PKG e o EBOOT do usuário (com a licença
# dele): não ficam neste repositório. São lidas dos arquivos key_vault do
# código-fonte do RPCS3, baixados num commit fixo e conferidos por hash.
RPCS3_COMMIT = "0dec46b12e856756ba04271207a99a1de2208720"
RPCS3_CRYPTO_FILES = {
    "key_vault.h": "d5d0cede74c7900bb23ca8d60066194648ed4b976eaee60315c0bcbed7dc2aaf",
    "key_vault.cpp": "a2947ff6b681258c198552ec6344e16685e2df649018206daa3f41a0a1aadbd7",
}
# Do PKG só saem estes itens (o resto, como troféus, não é usado pelo port).
PKG_ITEMS = ("PARAM.SFO", "ICON0.PNG", "USRDIR/EBOOT.BIN", "USRDIR/media.zip", "USRDIR/Bman.bnk")

# Hash dos segmentos PT_LOAD do EBOOT.elf europeu (NPEB00076, APP_VER 01.00):
# o lift só é válido para exatamente este binário.
EBOOT_SEGMENTS_SHA256 = "7eb0eafaf45f4a238d0f05f2ad404506c70e86421f37ee117e1301004d6f4935"

WIN = os.name == "nt"
EXE = ".exe" if WIN else ""

# SHA-256 fixo dos binários prontos (os .zip de código do codeload não têm
# bytes garantidos, então só os binários são conferidos).
PINNED_SHA256 = {
    "cmake": "d163cd3ab4959b0a53fa8988f2ddbd2e6c501658201e6a154386bad9dbe4f836",
    "ninja": "f550fec705b6d6ff58f2db3c374c2277a37691678d6aba463adcbb129108467a",
    "llvm-mingw": "fd5462b7f5d93b2ff981fcd3f9c7e427e47f3203c64ea91c27bcc444eebd74cd",
}

DOWNLOADS = {
    "cmake": (f"https://github.com/Kitware/CMake/releases/download/v{CMAKE_VER}/cmake-{CMAKE_VER}-windows-x86_64.zip", "cmake"),
    "ninja": (f"https://github.com/ninja-build/ninja/releases/download/v{NINJA_VER}/ninja-win.zip", "ninja"),
    "llvm-mingw": (f"https://github.com/mstorsjo/llvm-mingw/releases/download/{LLVM_MINGW_REL}/llvm-mingw-{LLVM_MINGW_REL}-ucrt-x86_64.zip", "llvm-mingw"),
    "ps3recomp": (f"https://codeload.github.com/sp00nznet/ps3recomp/zip/{PS3RECOMP_COMMIT}", "src/ps3recomp"),
    "sdl2": (f"https://codeload.github.com/libsdl-org/SDL/zip/refs/tags/{SDL_TAG}", "src/sdl2"),
    "vulkan-headers": (f"https://codeload.github.com/KhronosGroup/Vulkan-Headers/zip/refs/tags/{VK_HEADERS_TAG}", "src/vulkan-headers"),
    "glslang": (f"https://codeload.github.com/KhronosGroup/glslang/zip/refs/tags/{GLSLANG_TAG}", "src/glslang"),
    "spirv-tools": (f"https://codeload.github.com/KhronosGroup/SPIRV-Tools/zip/{SPIRV_TOOLS_COMMIT}", "src/glslang/External/spirv-tools"),
    "spirv-headers": (f"https://codeload.github.com/KhronosGroup/SPIRV-Headers/zip/{SPIRV_HEADERS_COMMIT}",
                      "src/glslang/External/spirv-tools/external/spirv-headers"),
}


class Build:
    def __init__(self, a):
        self.root = os.path.abspath(a.root)
        self.tc = os.path.join(self.root, "toolchain")
        self.state = os.path.join(self.tc, "state")
        self.dl = os.path.join(self.tc, "downloads")
        self.jobs = a.jobs or default_jobs()
        self.src_override = dict(s.split("=", 1) for s in (a.src or []))
        self.deps = os.path.abspath(a.deps_prefix) if a.deps_prefix else os.path.join(self.tc, "deps")
        self.prebuilt_deps = bool(a.deps_prefix)
        self.cmake = a.cmake or os.path.join(self.tc, "cmake", "bin", "cmake" + EXE)
        self.ninja = a.ninja or os.path.join(self.tc, "ninja", "ninja" + EXE)
        self.mingw = os.path.abspath(a.llvm_mingw) if a.llvm_mingw else os.path.join(self.tc, "llvm-mingw")
        self.python = sys.executable
        self.pkg = os.path.abspath(a.pkg) if getattr(a, "pkg", None) else None
        self.rap = os.path.abspath(a.rap) if getattr(a, "rap", None) else None
        self.rpcs3_crypto_dir = os.path.abspath(a.rpcs3_crypto) if getattr(a, "rpcs3_crypto", None) else None
        self.log = open(os.path.join(self.tc, "build.log"), "a", encoding="utf-8")
        os.makedirs(self.state, exist_ok=True)
        os.makedirs(self.dl, exist_ok=True)

    # ---------------------------------------------------------------- util
    def say(self, msg):
        line = time.strftime("%H:%M:%S ") + msg
        print(line, flush=True)
        self.log.write(line + "\n"); self.log.flush()

    # Cada etapa grava uma "chave" (hash das entradas dela): depois de
    # atualizar o port (outro patch, outra versão de dependência, outro
    # lifter), só refaz o que mudou.
    def key(self, step):
        h = hashlib.sha256(step.encode())
        R = self.root
        if step == "tools":
            h.update(f"{CMAKE_VER} {NINJA_VER} {LLVM_MINGW_REL} {self.cmake} {self.ninja} {self.mingw}".encode())
        elif step == "ps3recomp":
            h.update(PS3RECOMP_COMMIT.encode() + self.src_override.get("ps3recomp", "").encode())
            h.update(open(os.path.join(R, "patches", "ps3recomp-mingw.patch"), "rb").read())
        elif step == "deps":
            h.update(f"{SDL_TAG} {VK_HEADERS_TAG} {GLSLANG_TAG} {SPIRV_TOOLS_COMMIT} {SPIRV_HEADERS_COMMIT} {self.mingw}".encode())
        elif step == "lift":
            h.update(self.key("ps3recomp").encode())
            for f in ("tools/extract_spu_jobs.py", "src/spu/jobmods_register.c", "game/PS3_GAME/USRDIR/EBOOT.elf"):
                h.update(open(os.path.join(R, *f.split("/")), "rb").read())
            h.update(open(os.path.abspath(__file__), "rb").read())
        return h.hexdigest()

    def done(self, step):
        try:
            return open(os.path.join(self.state, step)).read().split()[0] == self.key(step)
        except (OSError, IndexError):
            return False

    def mark(self, step):
        open(os.path.join(self.state, step), "w").write(self.key(step) + " " + time.ctime() + "\n")

    def run(self, cmd, cwd=None, env=None):
        self.log.write("$ " + " ".join(f'"{c}"' if " " in c else c for c in cmd) + "\n"); self.log.flush()
        p = subprocess.Popen(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True, encoding="utf-8", errors="replace")
        tail = []
        for line in p.stdout:
            self.log.write(line)
            tail.append(line.rstrip())
            tail = tail[-30:]
            s = line.strip()
            if s.startswith("[") and "/" in s.split("]")[0]:          # progresso do ninja: [n/total]
                print("\r   " + s[:110].ljust(110), end="", flush=True)
        p.wait()
        print("\r" + " " * 116 + "\r", end="")
        self.log.flush()
        if p.returncode != 0:
            print("\n".join(tail))
            raise SystemExit(f"\nERRO: comando falhou ({p.returncode}). Detalhes em toolchain\\build.log")

    def py(self, script, *args):
        self.run([self.python, script, *args])

    def download(self, url, path, sha256=None):
        """Baixa url para path (se ainda não estiver lá), conferindo o SHA-256."""
        if os.path.exists(path):
            return path
        tmp = path + ".part"
        req = urllib.request.Request(url, headers={"User-Agent": "bomberman-ultra-pc"})
        with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
            shutil.copyfileobj(r, f)
        if sha256:
            h = hashlib.sha256(open(tmp, "rb").read()).hexdigest()
            if h != sha256:
                os.remove(tmp)
                raise SystemExit(f"O download de {os.path.basename(path)} veio diferente do esperado (SHA-256).")
        os.replace(tmp, path)
        return path

    def rpcs3_crypto(self):
        if self.rpcs3_crypto_dir:
            return self.rpcs3_crypto_dir
        d = os.path.join(self.tc, "src", "rpcs3-crypto")
        os.makedirs(d, exist_ok=True)
        for name, sha in RPCS3_CRYPTO_FILES.items():
            url = f"https://raw.githubusercontent.com/RPCS3/rpcs3/{RPCS3_COMMIT}/rpcs3/Crypto/{name}"
            if not os.path.exists(os.path.join(d, name)):
                self.say(f"   baixando {name} (RPCS3 {RPCS3_COMMIT[:7]})")
            self.download(url, os.path.join(d, name), sha)
        return d

    def fetch(self, name):
        """Baixa (com cache) e extrai DOWNLOADS[name]; devolve a pasta."""
        url, rel = DOWNLOADS[name]
        dst = os.path.join(self.tc, rel)
        if name in self.src_override:
            return self.src_override[name]
        if os.path.isdir(dst) and os.listdir(dst):
            return dst
        zpath = os.path.join(self.dl, name + ".zip")
        if not os.path.exists(zpath):
            self.say(f"   baixando {name}: {url}")
            tmp = zpath + ".part"
            req = urllib.request.Request(url, headers={"User-Agent": "bomberman-ultra-pc"})
            with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
                total = int(r.headers.get("Content-Length") or 0)
                got = 0
                while True:
                    b = r.read(1 << 20)
                    if not b:
                        break
                    f.write(b); got += len(b)
                    pct = f" {got * 100 // total}%" if total else ""
                    print(f"\r   {got >> 20} MB{pct}   ", end="", flush=True)
            print()
            want = PINNED_SHA256.get(name)
            if want:
                h = hashlib.sha256()
                with open(tmp, "rb") as f:
                    for blk in iter(lambda: f.read(1 << 20), b""):
                        h.update(blk)
                if h.hexdigest() != want:
                    os.remove(tmp)
                    raise SystemExit(f"O download de {name} veio diferente do esperado (SHA-256). Tente de novo.")
            os.replace(tmp, zpath)
        self.say(f"   extraindo {name}")
        tmpdir = dst + ".tmp"
        shutil.rmtree(tmpdir, ignore_errors=True)
        with zipfile.ZipFile(zpath) as z:
            z.extractall(tmpdir)
        items = os.listdir(tmpdir)
        top = os.path.join(tmpdir, items[0]) if len(items) == 1 and os.path.isdir(os.path.join(tmpdir, items[0])) else tmpdir
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.rmtree(dst, ignore_errors=True)
        shutil.move(top, dst)
        shutil.rmtree(tmpdir, ignore_errors=True)
        return dst

    def toolchain_file(self):
        """Toolchain CMake para o llvm-mingw desta máquina (nativo no Windows)."""
        m = self.mingw.replace("\\", "/")
        deps = self.deps.replace("\\", "/")
        path = os.path.join(self.tc, "llvm-mingw.cmake")
        ar = f"{m}/bin/llvm-ar{EXE}" if os.path.exists(os.path.join(self.mingw, "bin", "llvm-ar" + EXE)) else "/usr/bin/llvm-ar"
        ranlib = f"{m}/bin/llvm-ranlib{EXE}" if os.path.exists(os.path.join(self.mingw, "bin", "llvm-ranlib" + EXE)) else "/usr/bin/llvm-ranlib"
        open(path, "w").write(f"""# gerado por tools/build_windows.py
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER   "{m}/bin/x86_64-w64-mingw32-clang{EXE}")
set(CMAKE_CXX_COMPILER "{m}/bin/x86_64-w64-mingw32-clang++{EXE}")
set(CMAKE_RC_COMPILER  "{m}/bin/x86_64-w64-mingw32-windres{EXE}")
set(CMAKE_RC_FLAGS "--preprocessor-arg=--target=x86_64-w64-mingw32 \\"-I{m}/x86_64-w64-mingw32/include\\"")
set(CMAKE_AR     "{ar}")
set(CMAKE_RANLIB "{ranlib}")
set(CMAKE_FIND_ROOT_PATH "{m}/x86_64-w64-mingw32" "{deps}/sdl2-win" "{deps}/vk-win")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
""")
        return path

    def cmake_build(self, src, bdir, *defs, install=False):
        tcf = self.toolchain_file()
        self.run([self.cmake, "-S", src, "-B", bdir, "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={self.ninja}",
                  f"-DCMAKE_TOOLCHAIN_FILE={tcf}", "-DCMAKE_BUILD_TYPE=Release", *defs])
        self.run([self.cmake, "--build", bdir, "-j", str(self.jobs)])
        if install:
            self.run([self.cmake, "--install", bdir])

    # ---------------------------------------------------------------- etapas
    def step_import_pkg(self):
        """PKG + licença do usuário -> game/PS3_GAME (sem RPCS3)."""
        if not self.pkg:
            return
        here = os.path.dirname(os.path.abspath(__file__))
        if here not in sys.path:
            sys.path.insert(0, here)
        import aes_compat
        from extract_pkg import Pkg, extract, load_pkg_key
        from unself_npdrm import load_keys, unself
        self.say("[0/6] Convertendo o PKG nos arquivos do jogo")
        try:
            cid = Pkg(self.pkg).content_id
        except (AssertionError, OSError, struct.error, UnicodeDecodeError):
            raise SystemExit(f"{self.pkg} não é um PKG de PS3 válido.")
        if cid != CONTENT_ID:
            raise SystemExit(f"Esse PKG é {cid}; o port só funciona com o europeu, {CONTENT_ID}.")
        rap = self.rap or os.path.join(self.root, "game", "licenses", CONTENT_ID + ".rap")
        if not os.path.exists(rap) or os.path.getsize(rap) != 16:
            raise SystemExit(f"Falta a licença {CONTENT_ID}.rap (use \"Localizar licença\" no launcher).")
        crypto = self.rpcs3_crypto()
        g = os.path.join(self.root, "game", "PS3_GAME")
        self.say(f"   AES: {aes_compat.BACKEND}")
        extract(self.pkg, g, load_pkg_key(crypto), only=set(PKG_ITEMS), log=self.say)
        miss = [i for i in PKG_ITEMS if i != "ICON0.PNG" and not os.path.exists(os.path.join(g, *i.split("/")))]
        if miss:
            raise SystemExit("O PKG não tem: " + ", ".join(miss))
        self.say("   decriptando USRDIR/EBOOT.BIN com a sua licença")
        K, SK = load_keys(crypto)
        elf = unself(open(os.path.join(g, "USRDIR", "EBOOT.BIN"), "rb").read(), open(rap, "rb").read(16),
                     K, SK, log=lambda m: None)
        out = os.path.join(g, "USRDIR", "EBOOT.elf")
        open(out + ".tmp", "wb").write(elf)
        if eboot_segments_sha256(out + ".tmp") != EBOOT_SEGMENTS_SHA256:
            os.remove(out + ".tmp")
            raise SystemExit("O EBOOT decriptado não confere com a versão 01.00 europeia.")
        os.replace(out + ".tmp", out)
        self.say("   EBOOT.elf conferido (Bomberman ULTRA europeu 01.00)")

    def check_game(self):
        g = os.path.join(self.root, "game", "PS3_GAME")
        need = [os.path.join(g, "USRDIR", f) for f in ("EBOOT.elf", "media.zip", "Bman.bnk")] + [os.path.join(g, "PARAM.SFO")]
        miss = [n for n in need if not os.path.exists(n)]
        if miss:
            raise SystemExit("Faltam arquivos do jogo:\n  " + "\n  ".join(miss) +
                             "\nUse \"Preparar jogo\" no launcher e selecione o PKG do jogo.")
        if eboot_segments_sha256(os.path.join(g, "USRDIR", "EBOOT.elf")) != EBOOT_SEGMENTS_SHA256:
            raise SystemExit("O EBOOT.elf não é o do Bomberman ULTRA europeu (NPEB00076, 01.00) decriptado.")

    def step_tools(self):
        if self.done("tools"):
            return
        self.say("[1/6] Ferramentas: CMake, Ninja e llvm-mingw")
        if not os.path.exists(self.cmake):
            self.fetch("cmake")
        if not os.path.exists(self.ninja):
            self.fetch("ninja")
        if not os.path.exists(os.path.join(self.mingw, "bin", "x86_64-w64-mingw32-clang" + EXE)):
            self.fetch("llvm-mingw")
        self.mark("tools")

    def step_ps3recomp(self):
        dst = os.path.join(self.tc, "src", "ps3recomp")
        self.p3 = self.src_override.get("ps3recomp", dst)
        if self.done("ps3recomp"):
            return
        self.say(f"[2/6] ps3recomp {PS3RECOMP_COMMIT[:7]} + patch do port")
        if "ps3recomp" not in self.src_override:
            shutil.rmtree(dst, ignore_errors=True)   # extrai de novo do zip (em cache): patch sempre na árvore limpa
        self.p3 = self.fetch("ps3recomp")
        apply_patch(os.path.join(self.root, "patches", "ps3recomp-mingw.patch"), self.p3, self.say)
        self.mark("ps3recomp")

    def step_deps(self):
        if self.prebuilt_deps or self.done("deps"):
            return
        self.say("[3/6] Dependências: Vulkan-Headers, SDL2, glslang (uma vez só, alguns minutos)")
        b = os.path.join(self.tc, "b")   # curto: caminhos do Windows (MAX_PATH)
        vh = self.fetch("vulkan-headers")
        self.run([self.cmake, "-S", vh, "-B", os.path.join(b, "vh"), "-G", "Ninja",
                  f"-DCMAKE_MAKE_PROGRAM={self.ninja}", f"-DCMAKE_INSTALL_PREFIX={os.path.join(self.deps, 'vk')}"])
        self.run([self.cmake, "--install", os.path.join(b, "vh")])
        sdl = self.fetch("sdl2")
        self.cmake_build(sdl, os.path.join(b, "sdl2"), f"-DCMAKE_INSTALL_PREFIX={os.path.join(self.deps, 'sdl2-win')}",
                         "-DSDL_SHARED=OFF", "-DSDL_STATIC=ON", "-DSDL_TEST=OFF", install=True)
        gl = self.fetch("glslang")
        self.fetch("spirv-tools")
        self.fetch("spirv-headers")
        self.cmake_build(gl, os.path.join(b, "glslang"), f"-DCMAKE_INSTALL_PREFIX={os.path.join(self.deps, 'vk-win')}",
                         "-DENABLE_OPT=ON", "-DGLSLANG_TESTS=OFF", "-DBUILD_SHARED_LIBS=OFF",
                         "-DENABLE_GLSLANG_BINARIES=OFF", "-DSPIRV_SKIP_TESTS=ON", "-DSPIRV_SKIP_EXECUTABLES=ON",
                         install=True)
        self.mark("deps")

    def step_lift(self):
        if self.done("lift"):
            return
        R, T = self.root, os.path.join(self.p3, "tools")
        elf = os.path.join(R, "game", "PS3_GAME", "USRDIR", "EBOOT.elf")
        self.say("[4/6] Convertendo o jogo (PPU e SPU) -- alguns minutos")
        for d in ("analysis", "recompiled", "spu_jobs", "spu_gen2"):   # sem sobras de um lift anterior
            shutil.rmtree(os.path.join(R, d), ignore_errors=True)
        self.py(os.path.join(T, "ppu_loader.py"), elf, "-o", os.path.join(R, "analysis", "loader"))
        self.py(os.path.join(T, "ppu_lifter.py"), elf,
                "--functions", os.path.join(R, "analysis", "loader", "EBOOT.functions.json"),
                "--hle-stubs", os.path.join(R, "analysis", "loader", "EBOOT.imports.json"),
                "--output", os.path.join(R, "recompiled"), "-j", str(min(self.jobs, 32)))   # Windows: Pool <= 61
        self.py(os.path.join(T, "gen_hle_nids.py"), "--all", "--out", os.path.join(R, "recompiled", "ppu_hle_nids.cpp"))
        self.py(os.path.join(R, "tools", "extract_spu_jobs.py"), elf, os.path.join(R, "spu_jobs"))
        os.makedirs(os.path.join(R, "spu_gen2"), exist_ok=True)
        self.py(os.path.join(T, "build_spu_workloads.py"), "--lifted", os.path.join(R, "spu_gen2"),
                "--out", os.path.join(R, "spu_gen2", "spu_workloads.c"), "--constructor",
                "--input", elf, "--input", os.path.join(R, "spu_jobs", "job_1a4000.elf"))
        job = os.path.join(R, "spu_jobs", "job_199a80.elf")
        self.py(os.path.join(T, "spu_lifter.py"), job, "--auto-functions", job, "--code-end", "0x9320",
                "--base", "0", "--symbol-prefix", "bmujob_render", "-o", os.path.join(R, "spu_gen2", "bmujob_render"))
        shutil.copy(os.path.join(R, "src", "spu", "jobmods_register.c"), os.path.join(R, "spu_gen2"))
        self.mark("lift")

    def step_assets(self):
        R = self.root
        self.say("[5/6] Ícone e tradução PT-BR")
        self.py(os.path.join(R, "tools", "make_icon.py"), os.path.join(R, "game", "PS3_GAME", "ICON0.PNG"),
                os.path.join(R, "src", "bomberman.ico"), os.path.join(R, "launcher", "bomberman.ico"))
        os.makedirs(os.path.join(R, "build-assets"), exist_ok=True)
        en = os.path.join(R, "build-assets", "strings_en.xus")
        with zipfile.ZipFile(os.path.join(R, "game", "PS3_GAME", "USRDIR", "media.zip")) as z:
            open(en, "wb").write(z.read("strings_en.xus"))
        self.py(os.path.join(R, "tools", "build_ptbr.py"), en, os.path.join(R, "traducao", "strings_ptbr.xus"))

    def step_compile(self):
        R = self.root
        tcf = self.toolchain_file()
        deps = self.deps
        for bdir, march, exe in (("build-win-compat", "x86-64-v2", "BombermanUltra_compat.exe"),
                                 ("build-win", "x86-64-v3", "BombermanUltra.exe")):
            self.say(f"[6/6] Compilando {exe} ({march})")
            flags = f"-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -march={march}"
            self.run([self.cmake, "-S", R, "-B", os.path.join(R, bdir), "-G", "Ninja",
                      f"-DCMAKE_MAKE_PROGRAM={self.ninja}", f"-DCMAKE_TOOLCHAIN_FILE={tcf}",
                      "-DCMAKE_BUILD_TYPE=Release", f"-DPS3RECOMP_DIR={self.p3}",
                      f"-DRECOMP_DIR={os.path.join(R, 'recompiled')}", "-DPORT_SPU_INPUTS=",
                      f"-DSPU_DIR={os.path.join(R, 'spu_gen2')}", f"-DBMU_EXE_NAME={exe}",
                      "-DBMU_BUILD_LAUNCHER=OFF",   # o launcher em uso está aberto (exe travado)
                      "-DPS3RECOMP_RSX_VULKAN=ON", f"-DPS3RECOMP_VULKAN_INCLUDE_DIR={os.path.join(deps, 'vk', 'include')}",
                      f"-DCMAKE_PREFIX_PATH={os.path.join(deps, 'sdl2-win')};{os.path.join(deps, 'vk-win')}",
                      f"-DCMAKE_C_FLAGS={flags}", f"-DCMAKE_CXX_FLAGS={flags}"])
            self.run([self.cmake, "--build", os.path.join(R, bdir), "-j", str(self.jobs)])


# -------------------------------------------------------------------- helpers
def total_ram_gb():
    try:
        if WIN:
            import ctypes

            class MS(ctypes.Structure):
                _fields_ = [("len", ctypes.c_ulong), ("load", ctypes.c_ulong),
                            ("total", ctypes.c_ulonglong), ("avail", ctypes.c_ulonglong),
                            ("tpf", ctypes.c_ulonglong), ("apf", ctypes.c_ulonglong),
                            ("tv", ctypes.c_ulonglong), ("av", ctypes.c_ulonglong), ("ave", ctypes.c_ulonglong)]
            m = MS(); m.len = ctypes.sizeof(MS)
            ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m))
            return m.total / 2**30
        return os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES") / 2**30
    except Exception:
        return 8.0


def default_jobs():
    """Um job por núcleo, mas sem passar de ~1 por 2 GB de RAM: cada pedaço do
    código convertido é uma unidade de compilação grande."""
    return max(1, min(os.cpu_count() or 4, int(total_ram_gb() / 2)))


def eboot_segments_sha256(path):
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF" or d[4] != 2:
        return ""
    phoff = struct.unpack(">Q", d[0x20:0x28])[0]
    n = struct.unpack(">H", d[0x38:0x3A])[0]
    h = hashlib.sha256()
    for i in range(n):
        t, _, off, va, _, fs, _, _ = struct.unpack(">IIQQQQQQ", d[phoff + 56 * i:phoff + 56 * i + 56])
        if t == 1 and fs:
            h.update(struct.pack(">QQ", va, fs)); h.update(d[off:off + fs])
    return h.hexdigest()


def apply_patch(patch_path, root, say=print):
    """Aplica um diff unificado do git (só modificações) sem precisar do git.

    Trabalha em bytes e separa as linhas só por \\n, como o git: o \\r de
    arquivos com fim de linha misto fica como parte da linha. Confere todos os
    arquivos antes de gravar qualquer um, então uma falha não deixa a árvore
    meio aplicada."""
    lines = open(patch_path, "rb").read().split(b"\n")
    i, results = 0, []
    while i < len(lines):
        if not lines[i].startswith(b"--- "):
            i += 1
            continue
        target = lines[i + 1][4:].strip().decode("utf-8")
        if target.startswith("b/"):
            target = target[2:]
        i += 2
        fpath = os.path.join(root, *target.split("/"))
        src = open(fpath, "rb").read().split(b"\n")
        out, pos = [], 0
        while i < len(lines) and lines[i].startswith(b"@@"):
            hdr = lines[i].split()
            old_start = int(hdr[1][1:].split(b",")[0])
            i += 1
            old, new = [], []
            while i < len(lines) and not lines[i].startswith((b"@@", b"diff --git", b"--- ")):
                l = lines[i]
                if l.startswith(b" "):
                    old.append(l[1:]); new.append(l[1:])
                elif l.startswith(b"-"):
                    old.append(l[1:])
                elif l.startswith(b"+"):
                    new.append(l[1:])
                elif l.startswith(b"\\"):
                    pass
                elif l == b"" and i == len(lines) - 1:
                    pass
                elif l == b"":
                    old.append(b""); new.append(b"")
                i += 1
            at = old_start - 1 if old else old_start
            found = -1
            for delta in range(0, 400):
                for cand in (at + delta, at - delta):
                    if pos <= cand <= len(src) - len(old) and src[cand:cand + len(old)] == old:
                        found = cand
                        break
                if found >= 0:
                    break
            if found < 0:
                raise SystemExit(f"patch não aplica em {target} (linha {old_start})")
            out += src[pos:found] + new
            pos = found + len(old)
        out += src[pos:]
        results.append((fpath, b"\n".join(out)))
    for fpath, data in results:
        open(fpath, "wb").write(data)
    say(f"   patch aplicado em {len(results)} arquivo(s)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--root", default=os.path.dirname(here))
    ap.add_argument("--jobs", type=int, default=0)
    ap.add_argument("--clean", action="store_true", help="refaz todas as etapas (mantém downloads)")
    ap.add_argument("--cmake"); ap.add_argument("--ninja"); ap.add_argument("--llvm-mingw")
    ap.add_argument("--deps-prefix")
    ap.add_argument("--src", action="append", metavar="NOME=PASTA")
    ap.add_argument("--pause", action="store_true", help="espera Enter no fim (janela aberta pelo launcher)")
    ap.add_argument("--pkg", help="PKG europeu do usuário: extrai e decripta para game/PS3_GAME antes do build")
    ap.add_argument("--rap", help="licença .rap (padrão: game/licenses/<content id>.rap)")
    ap.add_argument("--rpcs3-crypto", help="pasta rpcs3/Crypto local, em vez de baixar os key_vault")
    a = ap.parse_args()
    try:
        build(a)
    except BaseException as e:
        if isinstance(e, KeyboardInterrupt):
            print("\nInterrompido.")
            code = 130
        elif isinstance(e, SystemExit):
            code = e.code if isinstance(e.code, int) else 1
            if not isinstance(e.code, int) and e.code:
                print(e.code)
        else:
            import traceback
            traceback.print_exc()
            code = 1
        if code and a.pause:
            print("\nO build parou. O log completo está em toolchain\\build.log.")
            try:
                input("Pressione Enter para fechar esta janela...")
            except EOFError:
                pass
        sys.exit(code)
    if a.pause:
        print("\nTudo pronto! Esta janela fecha sozinha em 5 segundos.")
        time.sleep(5)


def build(a):
    os.makedirs(os.path.join(os.path.abspath(a.root), "toolchain"), exist_ok=True)
    b = Build(a)
    if a.clean:
        shutil.rmtree(b.state, ignore_errors=True); os.makedirs(b.state)
    t0 = time.time()
    b.say("=== Bomberman ULTRA: preparando o jogo neste PC ===")
    b.step_import_pkg()
    b.check_game()
    b.step_tools()
    b.step_ps3recomp()
    b.step_deps()
    b.step_lift()
    b.step_assets()
    b.step_compile()
    b.mark("ok")
    b.say(f"=== Pronto em {int(time.time() - t0) // 60} min: bin\\BombermanUltra.exe e bin\\BombermanUltra_compat.exe ===")


if __name__ == "__main__":
    main()
