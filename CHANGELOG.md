# Mudanças

## 1.2.0 — direto do PKG, sem RPCS3

- **Preparar jogo** agora pergunta de onde vêm os arquivos: **Selecionar o PKG do jogo** (recomendado) ou o jogo instalado no RPCS3.
- O launcher localiza o PKG sozinho pelo cabeçalho (pasta do port e até 3 pastas acima, Downloads, Área de Trabalho, Documentos), qualquer que seja o nome do arquivo, ou deixa escolher; recusa PKGs de outra região.
- O build extrai do PKG só o que o port usa, decripta o `EBOOT.BIN` com a licença `.rap` do usuário e confere o resultado pelo hash da versão europeia 01.00. As chaves públicas do PS3 vêm dos `key_vault` do RPCS3 num commit fixo, conferidos por SHA-256; nenhuma chave no repositório.
- `aes_compat.py` funciona sem pacotes extras: usa a BCrypt do Windows (com autoteste FIPS-197) no Python embutido e tem AES em Python puro como último recurso.

## 1.1.0 — compilar no próprio Windows

- **Preparar jogo** no launcher: aponta o jogo instalado no RPCS3, confere que é o europeu (`PARAM.SFO` e hash do `EBOOT.elf`), copia os arquivos, procura a licença no RPCS3 e compila o port neste PC. Sem WSL e sem instalar nada no Windows: baixa Python embutido, CMake, Ninja, llvm-mingw, ps3recomp, SDL2 e glslang para `toolchain\`, com hash conferido nos binários.
- `tools/build_windows.py`: build completo em etapas que continuam de onde pararam e só refazem o que mudou depois de uma atualização; aplica o patch do ps3recomp sem git.
- Pacote de release (launcher + código-fonte, nada do jogo) e CI que confere o patch do ps3recomp e compila o launcher.
- Ícone genérico no launcher quando ainda não há `ICON0.PNG`; `CONTRIBUTING.md`, modelos de issue (bug e compatibilidade de GPU) e de PR.

## 1.0.0 — primeira versão pública

- Port completo do Bomberman ULTRA europeu (`NPEB00076`): boot, menus, partidas locais com até 4 jogadores, saves.
- Backend Vulkan com os shaders do jogo traduzidos para SPIR-V; resolução de 720p a 4K; tela cheia sem bordas.
- 60 FPS travados como no PS3 (o flip completa antes da espera da GPU e os comandos depois do flip só rodam quando ele completa); opção para destravar.
- Desempenho: draws em lote, jobs SPURS em paralelo, leitura de vértices por coluna, cache da análise de vertex programs, exe AVX2 e versão compat.
- Tradução PT-BR (734/736 textos) montada pelo launcher a partir do `media.zip` do usuário.
- Controles por SDL2: DualShock 4, DualSense, Xbox e genéricos, conexão a quente, vibração; SELECT + START fecha o jogo.
- Launcher: verificação de arquivos, licença RAP obrigatória da versão europeia, idioma, áudio, vídeo, limite de FPS, atalho na Steam (`--jogar`).
- Compatibilidade: revisão do backend Vulkan para NVIDIA (formatos, recursos opcionais, swapchain, `VK_SUBOPTIMAL_KHR`, validação dos SPIR-V).
