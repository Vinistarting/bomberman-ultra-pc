# Mudanças

## 1.0.0 — primeira versão pública

- Port completo do Bomberman ULTRA europeu (`NPEB00076`): boot, menus, partidas locais com até 4 jogadores, saves.
- Backend Vulkan com os shaders do jogo traduzidos para SPIR-V; resolução de 720p a 4K; tela cheia sem bordas.
- 60 FPS travados como no PS3 (o flip completa antes da espera da GPU e os comandos depois do flip só rodam quando ele completa); opção para destravar.
- Desempenho: draws em lote, jobs SPURS em paralelo, leitura de vértices por coluna, cache da análise de vertex programs, exe AVX2 e versão compat.
- Tradução PT-BR (734/736 textos) montada pelo launcher a partir do `media.zip` do usuário.
- Controles por SDL2: DualShock 4, DualSense, Xbox e genéricos, conexão a quente, vibração; SELECT + START fecha o jogo.
- Launcher: verificação de arquivos, licença RAP obrigatória da versão europeia, idioma, áudio, vídeo, limite de FPS, atalho na Steam (`--jogar`).
- Compatibilidade: revisão do backend Vulkan para NVIDIA (formatos, recursos opcionais, swapchain, `VK_SUBOPTIMAL_KHR`, validação dos SPIR-V).
