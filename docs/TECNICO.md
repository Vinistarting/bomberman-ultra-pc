# Notas técnicas do port

Como o Bomberman ULTRA foi levado do PS3 para o PC, os problemas encontrados e as decisões tomadas. Para instalar e jogar, veja o [README](../README.md).

## O que tem dentro do jogo

**Engine**: é o *Bomberman Live* (XBLA) portado para o PS3. A Hudson manteve o **XUI** da Microsoft para menus e HUD (`XuiScene`, `.xui`, `.xus`) e uma engine própria de cena, "SG"/SceneGraph (`SgAsset.cpp`, `.xsg.ps3`, `.texture.ps3`). O áudio é **SCREAM + MultiStream** da Sony, rodando em SPU.

**Arquivos** (o PKG só traz 2 arquivos grandes):

| Arquivo | Tamanho | Conteúdo |
|---|---|---|
| `USRDIR/EBOOT.BIN` | 1,9 MB | SELF NPDRM (licença local, RAP) |
| `USRDIR/media.zip` | 31,7 MB | ZIP deflate com 712 assets |
| `USRDIR/Bman.bnk` | 40,3 MB | banco de som SCREAM (blocos `SBlk`) |
| `TROPDIR/NPWR00389_00/TROPHY.TRP` | 240 KB | troféus |

`media.zip` (gerado por `tools/catalog_assets.py`):

| Tipo | Qtde | Formato |
|---|---|---|
| `.xsg.ps3` | 330 | modelos/cenas SceneGraph (arenas `air_*`, personagens `Body_/Head_/Hat_/Face_`) |
| `.texture.ps3` | 301 | `SG28PT00` + nome + formato (0x16 ARGB8, 0x15, 0x08 ~DXT1, 0x0A ~DXT5) |
| `.xui` | 39 | telas em **XML legível** (`MainMenuScreen.xui`, `InGameUI.xui`…) |
| `.pfx` | 31 | partículas em texto (`sEmitter…`) |
| `.xus` | 7 | strings: en, fr, de, it, es, jp, kr |
| `.abc` | 4 | tabelas de glifos das fontes |

Caminhos que o jogo abre: `/dev_bdvd/PS3_GAME/USRDIR/media.zip` e `.../Bman.bnk` (derivados do argv[0]), `/dev_hdd1/cache.dat|idx` (cellSysCacheMount) e `/app_home/flog_ps3.fpg` (opcional, não existe no PKG). Os saves usam `cellSaveDataUserAutoSave/Load`.

**Binário** (`tools/analyze_eboot.py`):

- Entry OPD `0x1B2158` → código `0x10230`, TOC `0x1BC488`
- `.text` `0x10000–0x1A4348` (1,58 MB), dados `0x1B0000–0x2EF788` (inclui 1,1 MB de BSS)
- 1 programa SPU embutido como ELF (`ELF+0x174D00`): o decodificador MP3/mixer do MultiStream (`spu/msngSPURS_MP3`). Mais dois jobs SPURS de renderização ("crTaskRunJobList") embutidos como binário cru em `0x199A80` (0xA500 bytes) e `0x1A4000` (0x2E0 bytes), extraídos por `tools/extract_spu_jobs.py`.

**Imports** (`tools/analyze_eboot.py`):

| Módulo | Funções | Uso no jogo |
|---|---|---|
| sceNp / sceNp2 | 29 + 29 | ranking, presença, salas Matching2 (modo Live) |
| cellSysutilAvc2 | 19 | voice chat do online |
| cellSpurs | 21 | JobChain/Taskset do MultiStream |
| sys_io | 19 | cellPad (4 jogadores), teclado, mouse |
| cellSysutil | 16 | vídeo, callbacks, saves, cache |
| sysPrxForUser | 16 | threads, lwmutex, TLS |
| cellGcmSys | 14 | RSX |
| sys_fs | 14 | arquivos + AIO |
| sceNpTrophy | 10 | troféus |
| sys_net / cellNetCtl | 9 + 4 | sockets do online |
| cellAudio, cellKey2char, cellRtc, cellSysmodule | 5, 4, 2, 2 | — |

## Velocidade (60 FPS)

A lógica do jogo é presa ao frame: cada flip avança 1/60 s, e as animações somam constantes fixas por frame. Abaixo de 60 flips/s o jogo fica em câmera lenta, e acima disso fica acelerado. Os comandos que vêm depois de um flip no FIFO só rodam quando esse flip completa, no máximo 60 por segundo, como o `WaitFlip` do RSX. O flip completa antes de esperar a GPU (antes isso custava de 2 a 5 ms por frame). O modo de flip "preciso" do `cellGcmSys` completa exatamente o flip que o drain alcançou. Desligar "Limitar a 60 FPS" no launcher (`BMU_FPS_CAP=0`) solta o limite: o jogo corre mais rápido se o PC der conta.

Com F9 e `PS3_FTRACE=99999:<s>:<arquivo>` na seção `[env]` do `launcher.ini`, o jogo grava uma linha do tempo de `<s>` segundos a partir do momento em que a tecla é apertada. Ela mostra syscalls, drains do FIFO, flips, fences e job chains, e foi o que achou os gargalos.

## Tradução PT-BR

O texto do jogo fica em `strings_xx.xus` dentro do `media.zip` (tabela de strings XUI: `XUIS`, versão, tamanho, número de entradas e pares texto/chave em UTF-16BE). A tradução está em `traducao/pt-br.tsv` (chave ⇥ texto, `⏎` = quebra de linha) e `tools/build_ptbr.py` gera `traducao/strings_ptbr.xus` a partir do inglês, mantendo ordem, chaves e os códigos `%c`, `%1` e de ícone de botão. Os dois textos sem tradução são a página em branco da ajuda e o contato do suporte japonês.

Com "Português (Brasil)" escolhido, o launcher monta `game\PS3_GAME\USRDIR\media_ptbr.zip`, uma cópia do `media.zip` com `strings_en.xus` trocado pela tradução. As outras entradas são copiadas byte a byte. O zip só é refeito quando uma das fontes muda. O jogo roda em inglês e o runtime abre `media_ptbr.zip` no lugar do original (`PS3_VFS_REDIRECT`). A fonte da interface (`gamefontFE`) já tem todos os acentos do português. Textos desenhados como imagem, como o logo, continuam em inglês.

## Decisões do port

- **Online**: a PSN do jogo não existe mais. NP/Matching2/Avc2 respondem como um console offline (códigos de erro do RPCS3). O multiplayer local com até 4 controles não depende disso.
- **Licença**: `sceNpDrmVerifyUpgradeLicense` só confirma a versão completa se existir `game/licenses/<content_id>.rap`, a mesma regra do RPCS3. O RAP `EP0555-NPEB00076_00-BOMBERMANGAME001` já está lá. Se o log mostrar outro content_id, aquele conteúdo não está licenciado.
- **Compra na PSN** (`UpsellScreen.xui`): `sceNpDrmExecuteGamePurchase` é ignorada.

## Correções feitas no caminho

- **ps3recomp no MinGW**: `__try/__except` (só MSVC) no profiler e uma redeclaração `static` que o clang em modo GNU recusa; `_WIN32_WINNT=0x0A00`; libs do D3D12 que o MinGW não pega via `#pragma comment`; XInput 1.4 (o 1.3 não vem no Windows 10/11).
- **Portas de evento do SPURS por instância**: o jogo cria um SPURS para renderização (porta 1) e outro para o MultiStream, cujo `spu_printf` também pede a porta 1. Com a tabela global do ps3recomp, a segunda falhava com `BUSY`, o áudio não inicializava e a thread do MultiStream ficava girando. Agora cada instância tem as próprias portas.
- **Jobs SPURS**: os dois jobs de renderização (`crTaskRunJobList`) ficam embutidos no EBOOT em `0x199A80` e `0x1A4000`. Eles foram extraídos, liftados e registrados pelo fingerprint. Antes disso, todo frame dava `dispatch MISS`.
- **Local dos assets**: o jogo abre `/dev_bdvd/PS3_GAME/USRDIR/` (do argv[0]), não `/app_home/`.
- **Desempenho**: o perfil mostrou o PPU parado em `cellSpursJoinJobChain` esperando os jobs SPU de renderização (~80% do frame). Correções: (1) o backend Vulkan grava os draws de um frame num só command buffer (arena de 96 MB para vértices/constantes/staging, descriptor sets de um pool zerado a cada espera) em vez de submit+espera por draw; (2) os JOBs entre dois SYNC de uma job chain rodam num pool de threads (`PS3_JC_THREADS`, padrão = núcleos − 2, até 5 + o walker), cada um com seu número de SPU; (3) `bin\BombermanUltra.exe` é compilado para x86-64-v3 (AVX2/FMA: `shufb` e `fma` do SPU ganham caminho vetorial) e `BombermanUltra_compat.exe` para x86-64-v2 — o launcher escolhe pela CPU; (4) logs por frame só com `PS3_VERBOSE=1`, cópia do frame para a CPU só para dump, `mfc_for` sem `getenv`/lock por acesso; (5) `JoinJobChain` dorme numa variável de condição em vez de girar.
- **Gameplay (2ª rodada)**: medido no PC com o próprio log (`[rsx] thread do RSX`, `[rsx engine] CPU dos draws`). Correções: o drain do FIFO acordava só a cada `Sleep(4)` (agora espera o kick do jogo, até 1 ms); o present em FIFO bloqueava a thread do RSX até o vblank (agora MAILBOX/IMMEDIATE; `PS3RECOMP_VK_VSYNC=1` volta ao vsync); análise do fragment program memoizada por conteúdo (era refeita a cada draw, com memset de 32 KB); hash do VP de 8 em 8 bytes; caminho rápido de decodificação de vértices float/unorm8; uma thread fixa por job chain (antes uma thread nova a cada execução, ~250/s); cache por thread do fingerprint dos binários de job e do LS de 256 KB. Partida: 30–38 → ~50–60 FPS.
- **Câmera lenta na partida (3ª rodada)**: com linhas do tempo gravadas no PC (F9), o frame de ~24 ms se dividia assim: ~11 ms do drain do FIFO, que o jogo espera terminar; 2–5 ms entre o RSX alcançar o flip e o flip completar, porque ele esperava o `present_finish`; e ~12 ms de lógica mais job chains. As correções foram três. O flip completa antes da espera da GPU. A análise de entradas do vertex program, que era refeita a cada draw, ganhou cache por conteúdo. A leitura de vértices passou a ser feita por coluna, com laços especializados por formato, e a deduplicação de índices usa uma tabela direta quando a faixa é pequena; os dois caminhos foram conferidos contra o código antigo. Resultado: de ~40 para mais de 60 flips/s na partida, agora travado em 60.
- **Launcher**: instância única; adota um jogo já aberto (o botão vira ENCERRAR JOGO) e, ao fechar o jogo, instala sozinho uma atualização que tenha chegado em partes.
- **Bordas cortadas**: a janela SDL não era DPI-aware (com escala 125%/150% do Windows, 1920×1080 virava 2400×1350 e saía da tela). Agora é per-monitor DPI aware, cabe na área útil do monitor, pode ser redimensionada e mantém 16:9 com faixas pretas. A resolução do launcher é a de renderização.
- **Fechar a janela** encerra o processo (antes o jogo seguia rodando sem bombear mensagens e o Windows marcava "não respondendo"). Esc não fecha mais o jogo. Minimizar só pausa a apresentação.
- **Imagem errada no D3D12 → backend Vulkan**: o backend D3D12 do ps3recomp desenhava a cena errada (glifos gigantes no lugar da tela). O backend Vulkan, com `PS3RECOMP_VK_GUEST_PROGRAMS=1` (shaders do jogo traduzidos para SPIR-V pelo glslang), usa o motor de draw compartilhado e renderiza corretamente. O `main.cpp` liga esses padrões sozinho; o backend foi adaptado para Windows (carrega `vulkan-1.dll`, janela via SDL2 estático).

## Como o jogo é compilado

O botão **Preparar jogo** do launcher baixa o Python *embeddable* oficial (conferido por MD5 publicado em python.org) para `toolchain\python`, escreve o `python312._pth` com os caminhos de `tools\` e do ps3recomp e roda `tools\build_windows.py --pause` num console. O script só usa a biblioteca padrão (o AES vem da BCrypt do Windows via ctypes, ver `tools/aes_compat.py`) e faz, em ordem:

| Etapa | O que faz | Refeita quando muda |
|---|---|---|
| 0. PKG (opcional, `--pkg`) | extrai `PARAM.SFO`, `ICON0.PNG`, `EBOOT.BIN`, `media.zip`, `Bman.bnk`; RAP → RIF → klicensee e unself do EBOOT (`extract_pkg.py`, `unself_npdrm.py`, chaves dos `key_vault` do RPCS3 num commit fixo); confere o hash dos segmentos | a cada `--pkg` |
| 1. ferramentas | CMake, Ninja e llvm-mingw (SHA-256 fixos) | versões |
| 2. ps3recomp | zip do commit fixo + `patches/ps3recomp-mingw.patch` (aplicador próprio, em bytes, tudo ou nada) | commit ou patch |
| 3. dependências | Vulkan-Headers, SDL2 estático, glslang + SPIRV-Tools estáticos | versões |
| 4. lift | `ppu_loader`, `ppu_lifter` (paralelo), `gen_hle_nids`, jobs SPU (`extract_spu_jobs.py`), `build_spu_workloads`, `spu_lifter` | ps3recomp, EBOOT, script |
| 5. assets | ícone (`make_icon.py`) e `strings_ptbr.xus` a partir do `media.zip` do usuário | sempre |
| 6. compilação | `BombermanUltra_compat.exe` (x86-64-v2) e `BombermanUltra.exe` (x86-64-v3) | sempre (incremental) |

Cada etapa grava em `toolchain/state/` um hash das suas entradas; se o hash bate, ela é pulada. O número de jobs é limitado a ~1 por 2 GB de RAM, porque cada pedaço do código convertido é uma unidade de compilação grande. O launcher não é recompilado nesse fluxo (`-DBMU_BUILD_LAUNCHER=OFF`), já que está aberto.

Para testar o script fora do Windows há `--cmake`, `--ninja`, `--llvm-mingw`, `--deps-prefix` e `--src NOME=PASTA`; o fluxo completo (inclusive num caminho com espaços e com a llvm-mingw 20261020) foi conferido assim no Linux.
