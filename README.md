# Bomberman ULTRA — port não oficial para PC

Port nativo para Windows do **Bomberman ULTRA** (PlayStation Network, Hudson Soft, 2009), feito por recompilação estática: o código PowerPC e SPU do PS3 é convertido em C/C++ e compilado para x86-64, com o sistema do PS3 substituído por uma camada HLE ([ps3recomp](https://github.com/sp00nznet/ps3recomp)). Não é um emulador.

> **Não afiliado, endossado nem patrocinado pela Konami, Hudson Soft ou Sony Interactive Entertainment.** Bomberman é marca registrada da Konami Digital Entertainment. PlayStation é marca registrada da Sony Interactive Entertainment. **Este repositório não contém nenhum arquivo do jogo** — nem código, nem arte, nem áudio, nem texto, nem licença. Você precisa ter o seu próprio jogo.

[English](README.en.md) · [Detalhes técnicos](docs/TECNICO.md) · [Mudanças](CHANGELOG.md) · [Como contribuir](CONTRIBUTING.md)

![Partida em 1080p](docs/gameplay.png)

## ⚠ Licença obrigatória: versão europeia

O port foi compilado a partir de **uma versão específica** do jogo e **só funciona com a licença dela**:

| | |
|---|---|
| Jogo | Bomberman ULTRA — **Europa** |
| Title ID | `NPEB00076` |
| Content ID | `EP0555-NPEB00076_00-BOMBERMANGAME001` |
| Licença | `EP0555-NPEB00076_00-BOMBERMANGAME001.rap` |

O arquivo `.rap` é a licença da sua compra na PSN; ele é obtido do seu próprio PS3/conta (as ferramentas para isso não fazem parte deste projeto). O launcher procura o `.rap` sozinho e também tem o botão **Localizar licença**. Sem a licença certa o botão JOGAR fica bloqueado, e o próprio `BombermanUltra.exe` também se recusa a abrir. Outras regiões (EUA `NPUB…`, Japão `NPJB…`) têm outro EBOOT e não funcionam com este port.

## Destaques

- Nativo 64 bits, Vulkan, **60 FPS travados** como no PS3 (o jogo tem a lógica presa ao frame); opção para destravar.
- Resolução de renderização de 720p a 4K, tela cheia sem bordas, contador de FPS.
- **Tradução em português do Brasil** (734 de 736 textos), além dos idiomas originais.
- **Controles**: DualShock 4, DualSense (USB/Bluetooth, vibração, cor do jogador na light bar), Xbox e genéricos, com conexão a quente, até 4 jogadores locais; teclado no jogador 1.
- **Steam / Big Picture**: botão "Adicionar à Steam" no launcher; segure **SELECT + START** para fechar o jogo pelo controle.
- Launcher em português com verificação de arquivos, licença, idioma, áudio e opções de vídeo.
- Saves locais. Sem RPCS3, sem firmware e sem `dev_flash` para jogar.

![Menus em português](docs/menu-ptbr.png)

## Como funciona

1. O `EBOOT.BIN` do seu PKG é decriptado com a sua licença (pelo RPCS3).
2. O ps3recomp converte as 6.249 funções PPU em C++ e os três programas SPU (áudio MultiStream e dois jobs de renderização) em C.
3. Tudo é compilado com o runtime HLE (`cellGcm`, `cellSpurs`, `cellPad`, sistema de arquivos, saves…) e com um backend Vulkan que executa o RSX: comandos do FIFO, shaders do jogo traduzidos para SPIR-V e texturas DXT.
4. O resultado é um `BombermanUltra.exe` que lê `media.zip` e `Bman.bnk` direto da sua pasta do jogo.

Como o código gerado vem do binário do jogo, **o executável é sempre compilado na sua máquina**, a partir dos seus arquivos: o botão **Preparar jogo** do launcher faz isso sozinho. Não distribuímos executáveis do jogo.

## Compatibilidade

| | |
|---|---|
| Sistema | Windows 10/11 64 bits (o jogo é compilado no próprio PC pelo launcher) |
| Vídeo | qualquer GPU com **Vulkan 1.0** e driver atualizado |
| CPU | x86-64 com AVX2 (versão principal) ou SSE4.2 (versão `compat`, escolhida sozinha pelo launcher) |

**GPUs testadas**:

- **AMD** Radeon RX 580 (Windows 11): jogo completo, 60 FPS.
- **SwiftShader** (Vulkan em software, Linux): boot → partida, imagens conferidas quadro a quadro.

**NVIDIA**: ainda não testado em hardware NVIDIA. Antes de publicar, o backend passou por esta revisão:

- os formatos usados (profundidade D32S8/D24S8, cores, BC1–3) são pedidos ao driver com fallback;
- recursos opcionais (`depthBounds`, `textureCompressionBC`) só são ligados se o driver informar;
- o swapchain usa `minImageCount + 1` e o modo de apresentação é escolhido entre os oferecidos;
- a GPU dedicada é preferida em notebooks híbridos (Optimus);
- todos os shaders gerados durante uma partida validam como SPIR-V para Vulkan 1.0 (só a capability `Shader`);
- `VK_SUBOPTIMAL_KHR` só recria o swapchain quando o tamanho muda de fato (alguns drivers o devolvem quadro a quadro, o que antes causaria travadas).

Se você tem NVIDIA, um relato (funcionou ou não, com o `logs\bomberman.log`) ajuda muito. Se o PC tiver duas GPUs, `PS3RECOMP_VK_DEVICE=<n>` na seção `[env]` do `launcher.ini` força uma delas (os números aparecem no log).

## Instalação

### O que você precisa

- O **seu** Bomberman ULTRA europeu (`NPEB00076`): o PKG e a licença `EP0555-NPEB00076_00-BOMBERMANGAME001.rap`.
- O [RPCS3](https://rpcs3.net), só para instalar o PKG e decriptar o EBOOT (o port não usa o RPCS3 para rodar).
- Windows 10/11 64 bits, GPU com Vulkan, cerca de **6 GB livres** e internet na primeira preparação (ela baixa ~350 MB de ferramentas).

### Passos

1. **Baixe** o `BombermanUltra-PC-<versão>.zip` em [Releases](https://github.com/Vinistarting/bomberman-ultra-pc/releases) e extraia num caminho curto, por exemplo `C:\Jogos\BombermanUltra`.
2. **No RPCS3**: *File → Install Packages/Raps/Edats* com o `.rap` e o PKG. Depois *Utilities → Decrypt PS3 Binaries* e escolha `dev_hdd0\game\NPEB00076\USRDIR\EBOOT.BIN`: o RPCS3 grava o `EBOOT.elf` ao lado.
3. **Abra o `BombermanUltraLauncher.exe`** e clique em **Preparar jogo**. Se o Windows avisar que o app não é reconhecido, use *Mais informações → Executar assim mesmo* (o launcher não é assinado).
   - Aponte a pasta do RPCS3 (ou `dev_hdd0\game\NPEB00076`). O launcher confere que é a versão europeia e que o `EBOOT.elf` é o decriptado dela, e copia `PARAM.SFO`, `ICON0.PNG`, `media.zip`, `Bman.bnk` e `EBOOT.elf` para `game\PS3_GAME`.
   - Aponte a licença `.rap`, se ele não achar sozinho.
   - Confirme. Uma janela de console baixa as ferramentas para `toolchain\` e compila o jogo: **10 a 40 minutos** na primeira vez, conforme o processador.
4. Clique em **JOGAR**.

O que o **Preparar jogo** baixa (tudo fica em `toolchain\`, nada é instalado no Windows):

| Ferramenta | De onde | Para quê |
|---|---|---|
| Python 3.12 embutido | python.org | roda o `tools\build_windows.py` e o ps3recomp |
| CMake 3.31, Ninja 1.12 | GitHub (Kitware, ninja-build) | build |
| llvm-mingw 20261020 | GitHub (mstorsjo) | compilador clang para Windows |
| ps3recomp (commit fixo) + `patches\ps3recomp-mingw.patch` | GitHub (sp00nznet) | conversão PPU/SPU e runtime |
| SDL2, Vulkan-Headers, glslang, SPIRV-Tools | GitHub (libsdl-org, KhronosGroup) | janela, controles, shaders |

Os binários prontos (Python, CMake, Ninja, llvm-mingw) são conferidos por hash. Se algo falhar, clicar em **Preparar jogo** de novo continua de onde parou; o log completo fica em `toolchain\build.log`.

### Alternativa: compilar no Linux ou WSL

```sh
git clone https://github.com/Vinistarting/bomberman-ultra-pc.git
git clone https://github.com/sp00nznet/ps3recomp.git
git -C ps3recomp checkout a6d76c895f77eb30b90a806c830544e61821b77f
cd bomberman-ultra-pc
# arquivos do jogo em game/PS3_GAME (PARAM.SFO, ICON0.PNG, USRDIR/{EBOOT.elf,media.zip,Bman.bnk})
tools/setup_deps.sh ~/bmu-deps        # uma vez: llvm-mingw, SDL2 e glslang para Windows
export BMU_LLVM_MINGW=~/bmu-deps/llvm-mingw BMU_DEPS=~/bmu-deps
tools/build_cross.sh ../ps3recomp
```

Sem o RPCS3, `tools/extract_pkg.py` e `tools/unself_npdrm.py` extraem o PKG e decriptam o EBOOT lendo as chaves do código-fonte do RPCS3 (`--rpcs3-crypto <rpcs3>/rpcs3/Crypto`); nenhuma chave vem neste repositório.

### Pasta final

```
BombermanUltra/
├── BombermanUltraLauncher.exe
├── bin/BombermanUltra.exe, BombermanUltra_compat.exe   (compilados no seu PC)
├── game/
│   ├── licenses/EP0555-NPEB00076_00-BOMBERMANGAME001.rap   (o launcher copia para cá)
│   └── PS3_GAME/{ICON0.PNG, PARAM.SFO, USRDIR/{EBOOT.elf, media.zip, Bman.bnk}}
├── toolchain/       ferramentas e código baixados pelo Preparar jogo
├── traducao/        tradução PT-BR (o .xus é gerado do seu media.zip)
└── user/            saves e cache (criado no 1º uso)
```

## Atualizar

Baixe a release nova e extraia **por cima** da pasta atual; `game\`, `user\`, `toolchain\` e `launcher.ini` são mantidos. Depois clique em **Preparar jogo**: só as etapas que mudaram são refeitas.

## Desinstalar

Apague a pasta. Para manter os saves, guarde antes `user\dev_hdd0`. Depois de compilar, `toolchain\` pode ser apagada para liberar espaço (o próximo Preparar jogo baixa tudo de novo). O atalho da Steam sai pela própria biblioteca da Steam (*Gerenciar → Remover atalho*).

## Controles

| Teclado | PS3 |
|---|---|
| Setas | direcional |
| Z | ✕ (confirmar, soltar bomba) |
| X | ○ (voltar) |
| A / S | □ / △ |
| Q / W | L1 / R1 |
| 1 / 2 | L2 / R2 |
| Enter | START (pausa) |
| Tab | SELECT |

As funções de cada botão na partida aparecem no próprio jogo, em *Ajuda e opções → Controles*.

Controles seguem o layout do PlayStation pela **posição** dos botões (o de baixo é ✕ também no Xbox). O primeiro controle conectado é o jogador 1. **SELECT + START por meio segundo** (Share + Options no DualShock 4, Create + Options no DualSense, View + Menu no Xbox) fecha o jogo.

## Launcher

| Opção | Efeito |
|---|---|
| Resolução | resolução de renderização (720p nativo do PS3 até 4K) |
| Tela cheia | janela sem bordas |
| Idioma | Português (Brasil), English, Français, Deutsch, Italiano, Español, 日本語, 한국어 ou o do Windows |
| Áudio | ligado, mudo ou desativado |
| Limitar a 60 FPS | ligado = velocidade original; desligado = o jogo acelera se o PC passar de 60 |
| Contador de FPS | no canto superior direito |
| Preparar jogo | importa o jogo da pasta do RPCS3 e compila o port neste PC |
| Localizar licença | aponta o `.rap` |
| Adicionar à Steam | cria o atalho de jogo não-Steam (`--jogar`: abre direto no jogo) |

## Configurações e saves

- `launcher.ini`: opções do launcher; a seção `[env]` repassa variáveis de depuração ao jogo.
- `user/dev_hdd0`: saves. `user/dev_hdd1`: cache do jogo.
- `logs/bomberman.log`: log da última execução, que inclui os controles reconhecidos e os FPS a cada 2 s.

## Problemas conhecidos

- **Online** (partidas e placares da PSN) não existe mais e fica desativado; o multijogador local funciona.
- Textos desenhados como imagem (logo, "READY/GO!", "Player 1" acima dos personagens) continuam em inglês.
- Câmera USB não é suportada.
- NVIDIA: veja [Compatibilidade](#compatibilidade).

## Créditos

- **ps3recomp** e **flOw** — sp00nznet: toda a base de recompilação estática de PS3.
- **RPCS3** — referência para formatos e algoritmos do PS3.
- **SDL2**, **glslang/SPIRV-Tools**, **Vulkan-Headers**, **llvm-mingw** — ver [THIRD_PARTY.md](THIRD_PARTY.md).
- **ACFA-Port** — SulphurS16: referência de organização deste repositório e da ideia de compilar no PC do usuário.

## Licença

O código deste repositório é **open source**, sob a [licença MIT](LICENSE). Isso vale **só** para o código daqui: o jogo, com seu código, arte, áudio e textos, continua sendo propriedade dos donos e não é distribuído nem licenciado por este projeto.
