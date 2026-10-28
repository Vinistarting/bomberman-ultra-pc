# Como contribuir

Obrigado pelo interesse! Relatos de compatibilidade (principalmente de placas **NVIDIA** e Intel), correções e melhorias no launcher, na tradução ou no runtime são bem-vindos. *English speakers: issues and pull requests in English are fine too.*

## Regra número um: nada do jogo no repositório

Este projeto só existe porque não distribui o jogo. **Nunca** inclua em commits, issues, pull requests ou anexos:

- arquivos do jogo ou derivados deles: `EBOOT.BIN`/`EBOOT.elf`, `media.zip`, `Bman.bnk`, `PARAM.SFO`, `ICON0.PNG`, PKG, texturas, áudio, textos extraídos;
- código gerado a partir do jogo: `recompiled/`, `spu_gen*/`, `spu_jobs/`, `analysis/`, `traducao/strings_ptbr.xus`, executáveis compilados (`bin/`);
- licenças e chaves: `.rap`, `.rif`, `act.dat`, `idps`, chaves de console ou do RPCS3;
- links para download de qualquer um desses.

O `.gitignore` já cobre tudo isso; confira `git status` antes de cada commit. Issues ou PRs com esse conteúdo serão fechados e o conteúdo apagado.

A tradução (`traducao/pt-br.tsv`) contém só texto em português escrito para este projeto, indexado pelas chaves do jogo; o arquivo final do jogo é montado na máquina de cada usuário.

## Montando o ambiente

O caminho mais simples é o mesmo de quem joga: baixe a release, rode o **BombermanUltraLauncher.exe** e use **Preparar jogo** (veja o [README](README.md#instalação)). Ele deixa tudo em `toolchain\`; depois disso dá para recompilar só o que mudou:

```bat
toolchain\python\python.exe -X utf8 tools\build_windows.py
```

Para o launcher, um CMake qualquer com llvm-mingw ou MSVC basta:

```sh
cmake -S launcher -B build-launcher -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-cross.cmake
cmake --build build-launcher
```

No Linux/WSL há também o `tools/setup_deps.sh` + `tools/build_cross.sh` (detalhes em [docs/TECNICO.md](docs/TECNICO.md)).

## Mudanças no ps3recomp

O port usa o [ps3recomp](https://github.com/sp00nznet/ps3recomp) num commit fixo (`a6d76c895f77eb30b90a806c830544e61821b77f`) mais `patches/ps3recomp-mingw.patch`. Para mexer no runtime:

1. faça a mudança dentro de `toolchain/src/ps3recomp` (ou de um clone seu no mesmo commit, com o patch aplicado);
2. gere o patch de novo a partir do commit fixo: `git diff a6d76c8 > patches/ps3recomp-mingw.patch` (só modificações de arquivos existentes; o aplicador do `build_windows.py` não cria arquivos novos);
3. rode o build de novo; a etapa do ps3recomp é refeita sozinha quando o patch muda.

Correções que servem para qualquer jogo merecem ir também para o ps3recomp original.

## Pull requests

- Uma mudança por PR, com uma descrição do que muda para quem joga.
- Diga onde testou: Windows, GPU e driver, e o que fez no jogo (menus, partida, 4 jogadores...).
- Textos para o usuário (launcher, mensagens) em português do Brasil; código e comentários podem ser em português ou inglês.
- Mantenha o launcher em Win32 puro, sem dependências novas, e o `build_windows.py` só com a biblioteca padrão do Python (ele roda no Python embutido).
- Mudou versões de dependências? Atualize `tools/build_windows.py`, `tools/setup_deps.sh` e o [THIRD_PARTY.md](THIRD_PARTY.md) juntos.

## Relatando problemas

Use os modelos de issue. Anexe o `logs\bomberman.log` (problema no jogo) ou o `toolchain\build.log` (problema no **Preparar jogo**), e informe GPU, driver, CPU e versão do Windows. Os logs não contêm nada do jogo além de nomes de arquivos.
