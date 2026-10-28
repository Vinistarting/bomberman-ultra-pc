#!/bin/sh
# Monta o pacote de release: código-fonte (git archive) + launcher compilado.
#
#   tools/package_release.sh <versão> <BombermanUltraLauncher.exe> <pasta de saída>
#
# Gera <saída>/BombermanUltra-PC-<versão>.zip e o .sha256. O pacote nunca leva
# nada do jogo: o jogo é compilado no PC do usuário ("Preparar jogo").
set -e
V="$1"; EXE="$2"; OUT="$3"
[ -n "$V" ] && [ -f "$EXE" ] && [ -n "$OUT" ] || { echo "uso: $0 <versão> <launcher.exe> <saída>"; exit 2; }
ROOT=$(git rev-parse --show-toplevel)
N="BombermanUltra-PC-$V"
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
rm -rf "$OUT/$N" "$OUT/$N.zip" "$OUT/$N.zip.sha256"
mkdir -p "$OUT/$N"
git -C "$ROOT" archive --format=tar HEAD | tar -x -C "$OUT/$N"
rm -rf "$OUT/$N/.github"
cp "$EXE" "$OUT/$N/BombermanUltraLauncher.exe"
sed 's/$/\r/' "$ROOT/docs/LEIA-ME.txt" > "$OUT/$N/LEIA-ME.txt"

# Trava de segurança: o mesmo filtro do CI, agora sobre o pacote.
bad=$(cd "$OUT/$N" && find . -type f | sed 's|^\./||' | grep -Ei '\.(rap|rif|pkg|edat|sdat|elf|bnk|sfo|xus)$|(^|/)(EBOOT\.BIN|media\.zip|ICON0\.PNG|act\.dat)$|^(recompiled|spu_gen[^/]*|spu_jobs|analysis|bin|game|toolchain)/' || true)
if [ -n "$bad" ]; then echo "conteúdo proibido no pacote:"; echo "$bad"; exit 1; fi
exes=$(cd "$OUT/$N" && find . -iname '*.exe' | sed 's|^\./||')
[ "$exes" = "BombermanUltraLauncher.exe" ] || { echo "executáveis inesperados: $exes"; exit 1; }

(cd "$OUT" && zip -qr -9 "$N.zip" "$N" && sha256sum "$N.zip" > "$N.zip.sha256")
rm -rf "$OUT/$N"
cat "$OUT/$N.zip.sha256"
