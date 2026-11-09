# Log de desenvolvimento — port nativo de Mr. Bones

**Atualizado em:** 6 de novembro de 2026 (noite).  
**Objetivo:** portar Mr. Bones (USA, MK-81016 V1.000) para Windows nativamente, sem emulação de CPU no executável final. O emulador foi autorizado exclusivamente para referência e testes.  
**Estado:** RUNWAY ainda não está jogável; não há executável final do jogo. Há diagnóstico composto com teclado→pad, discos estáveis com faixas CD-DA e evidência REA renovada.

## 1. Avaliação dos discos

- A primeira cópia informada do disco 1 em `I:\Roms\Sega Saturn\Mr. Bones (USA)\...` apresentou `Unexpected end of archive`. Foi fornecida outra cópia em `C:\Users\jneom\Neomaster\Mr. Bones (USA) (Disc 1).7z`, que passou no teste completo do 7-Zip.
- A cópia do disco 2 em `C:\Users\jneom\Neomaster\Mr. Bones (USA) (Disc 2).7z` também passou no teste completo.
- SHA-256 dos 7z: disco 1 `76B5970CAB056AF3D79950C14781F93C29A425705FD8938EFD70C254206D7922`; disco 2 `277A1EA1D152AA979C9A9512E9771B2F28C3E224A698F68BDEDE078F57DD72BA`. O teste verifica a integridade interna; não autentica a origem das cópias.
- Os dois discos foram extraídos integralmente em `C:\Users\jneom\.codex\visualizations\2026\11\06\01a1b286-46be-7510-993f-aa78d06dd1cc\discos\disco1` e `...\discos\disco2`. O verificador do projeto passou em modo completo: disco 1, 483 verificações e 9/9 faixas de áudio; disco 2, 519 verificações e 11/11 faixas de áudio. A área de visualizações é temporária e esses caminhos podem deixar de existir.
- Inventários e relatórios anteriores: `C:\Users\jneom\OneDrive\Documentos\ChatGPT\REY\mr-bones-analysis\disc1-inventory.json`, `disc2-inventory.json`, `AVALIACAO-DISCO-1.md` e `AVALIACAO-INICIAL.md`. O inventário foi feito por leitor ISO simples, não por dois leitores independentes.

## 2. Mapas e ferramentas

- Os discos contêm `MAPFILES.ZIP` com 27 mapas de ligação, iguais por SHA-256 entre os discos. Foram extraídos em `C:\Users\jneom\Neomaster\mr-bones-maps`.
- Ghidra 12.1.4 instalado em `C:\Users\jneom\Tools\ghidra_12.1.4_PUBLIC`, com SuperH SH-2 big-endian integrado; o repositório antigo do processador já foi incorporado ao Ghidra.
- O projeto Ghidra `C:\Users\jneom\Neomaster\mr-bones-ghidra\MrBones.gpr` e `MrBones.rep` contém análises iniciais do carregador e de RUNWAY. Os resultados estão em `...\REY\mr-bones-analysis\CORRELACAO-GHIDRA.md`, `ghidra-loader.txt` e `ghidra-runway.txt`.
- O CLI REA foi usado para inspecionar o ZIP de mapas e medir entropia da faixa de dados; rejeitou 7z e o binário bruto como formatos não suportados na operação tentada. O MCP REA não estava disponível nas sessões anteriores; não se deve atribuir ao REA as descompilações realizadas diretamente no Ghidra. Evidência do ZIP em `...\REY\mr-bones-analysis\disc1-rea-map-evidence.json`.
- `rtk` foi solicitado nas instruções locais, mas não está disponível no PATH; foram usadas as ferramentas equivalentes disponíveis.

## 3. Correlações SH-2 e ressalva crítica

- `/02` foi correlacionado ao carregador ligado aproximadamente em `0x00200000`. RUNWAY, MAUSO e VILLAGE possuem entradas ligadas em `0x00232000`, e literais de `_main` e limites de BSS coerentes com seus mapas. Isso é evidência estática, não uma captura da carga em execução.
- RUNWAY `03CODE`: SHA-256 `7532E4B8E804C6908CE03C987587612876A65663E56FA32B2174D022DA9DC216`. A faixa 01 do disco 1 tem SHA-256 `1EDF45A5EF5496291A9BF268266067590CB21E3F4930FA6279C105198B6F1F73`.
- **Achado posterior:** o mapa LOADER não corresponde exatamente ao binário `/02`. O mapa nomeia `_SMPCgetPad` em `0x0020bf34`, mas RUNWAY chama `0x0020bf08`, que apresenta início de função plausível. `_CreateLoader` parece começar em `0x0020b834`, 0x2c antes do mapa. A tabela de funções de áudio também apresenta diferença em `.data`; a divergência não é um deslocamento global único. Outros símbolos do carregador devem ser revalidados individualmente. `_SoundEvent` e `_SoundStartSequence` têm prólogos e literais coerentes nos endereços analisados, mas não foram validados em execução.
- O estudo completo de RUNWAY ainda não foi concluído; pseudocódigo pode ter tipos, limites de funções e chamadas indiretas incorretos.

## 4. Referência em emulador

- Ymir 0.4.1 foi baixado para a área temporária `...\ymir`; a BIOS US/EU solicitada pelo usuário foi obtida e identificada por hash, em `...\ymir-profile\roms\ipl\mpr-17933.bin`.
- Em uma tentativa anterior, o Ymir reconheceu `[MK-81016] mr Bones` e mostrou a tela da BIOS `Set Language`. Não houve confirmação do título ou da fase RUNWAY: os comandos de entrada automatizados não avançaram a tela, e o aplicativo apresentou erros no seletor de arquivos.
- Em tentativa posterior, o pedido de aprovação do controle de computador para abrir o Ymir expirou. Nesta sessão o helper também reportou falha ao gravar seus arquivos de inicialização. Logo não há captura de gameplay nem referência RUNWAY-001 validada.
- O emulador e a BIOS destinam-se apenas à referência; não foram incorporados ao executável nativo.

## 5. Implementação no projeto

O repositório (hoje em `F:\MrBonesPort`) já possuía identificação dos discos por manifesto, leitura ISO/CUE/setores/faixas, SHA-256, configuração e `mbverify`. O commit base observado é `5afa9ed`; as alterações subsequentes estavam sem commit na verificação. A árvore do port foi migrada de `C:\Users\jneom\Neomaster\mr-bones-port` para `F:\MrBonesPort` (mesmo diretório dos discos `disco1`/`disco2`).

Foram acrescentados e integrados:

- `resource.c`: leitura nativa de arquivo do disco, adaptação parcial do papel do carregamento síncrono, sem afirmar equivalência completa com o original.
- `runway_assets.c`: carrega os arquivos de título, texturas, código/dados e pacote de som necessários à investigação da edição examinada.
- `stxtr.c`, `sprite_decode.c`, `runway_sprite.c` e `runway_projection.c`: leitura de registros de textura, conversão experimental, associação aos descritores e cálculo parcial de projeção. Não implementam animação, composição nem uma fase.
- `bones_state.c`, `bones_message.c`, `mover.c` e `runway_input.c`: adaptações nativas parciais de rotinas de estado, mensagens, movimento e uma iteração de entrada. A ligação à entrada física e ao agendador não está integrada nem validada em gameplay.
- `snd1.c`, `cdda.c`, `sound_sequence.c` e `sound_event.c`: leitura de pacotes, conversão de setores, seleção de canais e leitura de eventos. Não reproduzem os sons da fase.
- `pan_collision.c`: descompacta o prefixo RLE associado ao mapa de colisão. Não interpreta a colisão nem decodifica os tiles visuais.
- `src/tools/mbvisual.c` e `build\mbvisual.exe`: diagnóstico Windows nativo para duas texturas de RUNWAY, com setas para alternar. **Não é gameplay.** A conversão/paleta são experimentais; `--check` carrega e valida os dados sem abrir a janela. A GUI não foi validada visualmente nesta sessão.
- `docs/runway-evidence.md` registra as correlações, o escopo e as incertezas. O arquivo `docs/coverage.md` contém anotações anteriores sobre falta de emulador e faixas e deve ser atualizado: Ymir e discos completos foram preparados na área temporária, embora não exista referência de gameplay.

## 6. Resultados de testes e achados

- O build no diretório original passou com Clang/CMake/Ninja; os **16 testes** CTest existentes após as últimas integrações passaram. `mbvisual --check` com disco 1 retornou texturas `32x13, 8x8`; com disco 2, rejeitou a identidade esperada do disco 1. O programa em janela foi apenas iniciado oculto e continuou em execução; sua aparência não foi verificada nesta sessão.
- O parser de texturas percorreu 24 arquivos `04STXT` não vazios, com 6.093 registros; RUNWAY contém 316. Foram identificadas 308 referências de descritores de RUNWAY, 176 em modo de 256 cores e 132 em RGB. A seleção da paleta, transparência e composição reais permanecem por confirmar.
- Nos 24 pacotes `05SND1`, foram validados 288 segmentos e percorridos 1.095 eventos, dos quais 104 vazios, com 1.205 registros. Os significados dos comandos e o codec dos samples ainda não foram determinados.
- O prefixo de colisão do primeiro registro `15PAN` de RUNWAY consumiu 200 bytes e gerou 4.480 bytes; SHA-256 da saída `6E4B078619065B1761E8C95122C665ED073941F7F92C56E108D9E7813AB6D67E`. Uma varredura achou 2.651 registros sintaticamente plausíveis, mas os dois últimos produzem 4.960 bytes enquanto o buffer de colisão observado tem 4.480; portanto **não** está estabelecido que todos sejam frames válidos.
- Há indícios de que os bits de entrada correspondem a esquerda, direita, baixo e borda de C, mas sua ligação ao frontend Windows e a equivalência em execução ainda precisam ser verificadas.

## 7. Próximos trabalhos e limites

1. Obter referência jogável em RUNWAY com captura de entradas, quadros e áudio; a tentativa anterior ficou na BIOS ou não abriu a janela.
2. Confirmar o mapeamento por seção do carregador e revisar os símbolos/rótulos afetados pela divergência do mapa.
3. Reconstruir e integrar agendador, mensagens, estados, colisões, controles, câmera e animação com base nas instruções e na referência.
4. Concluir o decodificador dos blocos gráficos `15PAN`, selecionar paletas e implementar composição de fundo e sprites; a prévia diagnóstica atual não é a cena do jogo.
5. Identificar e reproduzir samples/driver/eventos de som, música e sincronização; a conversão CD-DA ainda não identifica a faixa usada pela fase.
6. Integrar tudo em um executável nativo, testar uma sequência reproduzível e só considerar RUNWAY pronta quando for jogável e comparada com a referência.

Os testes atuais validam componentes isolados e dados estruturais. Não houve gameplay completo, comparação visual/sonora da fase nem teste de distribuição em Windows limpo. Não há estimativa confiável de conclusão.

## 8. Continuação após a criação deste log

- Foi integrado `saturn_pad.c`: mapeia botões lógicos do controle Saturn e atualiza estados mantido/pressionado/solto conforme análise estática da rotina chamada em `0x0020bf08`. Um teste conecta os bits à rotina parcial de entrada RUNWAY; ainda não há mapeamento do teclado Windows nem validação em gameplay.
- Foi integrado `pan_frame.c`, descompactador experimental dos blocos gráficos `15PAN`, com quatro planos 320×224 de índices e limites. Um teste local percorreu 2.649 registros sintaticamente plausíveis; a ordem de buffers, a paleta e a equivalência visual não foram validadas no jogo.
- O diagnóstico `mbvisual` passou a carregar também uma prévia do primeiro fundo usando uma paleta hipotética, além das duas texturas. É somente diagnóstico; não implementa RUNWAY jogável.
- O build de trabalho passou nos 18 testes e `mbvisual --check` com o disco 1 retornou imagens de 32×13, 8×8 e 320×224. A validação visual da GUI e o teste em uma instalação Windows limpa continuam pendentes.
- A análise posterior identificou `_CreateLoader` em provável `0x0020b834` e `_SMPCgetPad` em `0x0020bf08`, diferentes do mapa; funções anteriores de áudio mantêm prólogos e literais coerentes com os endereços analisados. Não existe um deslocamento único comprovado para todo o carregador.

- A análise posterior de `_PalLoad` identificou que a primeira palavra de `_MainPalette` é um índice, e não uma cor. Foi implementado `pan_palette.c` e a prévia passou a reconstruir o fluxo de paleta; o banco escolhido para o fundo ainda é hipótese.
- Após a correção de paleta, o build de trabalho passou em 19 testes e o verificador do diagnóstico carregou as três imagens. A saída visual ainda não foi comparada com RUNWAY em execução.
- `docs/coverage.md` foi atualizado para refletir que o Ymir, a BIOS e todas as faixas foram preparados na área temporária, sem afirmar referência jogável.

## 9. Continuação: discos estáveis, host input, composição e REA

- **Espaço em C / migração:** a cópia inicial para C: esgotou o disco (erro 112). Discos e, em seguida, o código do port ficaram em `F:\MrBonesPort` (`disco1`, `disco2` + árvore do projeto). Junção `C:\Users\jneom\Neomaster\MrBonesPort` → `F:\MrBonesPort`. Disco 1: 10 faixas; disco 2: 12 faixas. `mbverify --quick` no disco 1: 53 arquivos e **9/9 faixas de áudio presentes**.
- **`host_input.c`:** teclado Windows → bits `MB_PAD_*` (setas/WASD, Z/X/C, Enter, V/B/N, Q/E). Alimenta `saturn_pad_update_host` e a iteração `runway_input_step`. Sem validação de gameplay.
- **`runway_compose.c`:** composição diagnóstica RGBA (fundo opaco + sprites com teste de alpha). Não é lista de comandos VDP1/VDP2.
- **`mbvisual`:** quatro páginas (duas texturas, fundo, composição); timer ~60 Hz lê o teclado; setas trocam a página; Shift+setas/WASD/ZXC alimentam o pad. `--check` reporta `32x13, 8x8, 320x224, compose 320x224` e smoke de input `x=3932 msgs=2 last=0x00020600`. Continua **não** sendo gameplay.
- Build: **21/21** testes CTest passaram (inclui `test_runway_compose` e `test_host_input`).
- **REA (MCP, sessão desta continuação):**
  - `open_binary` + `inspect_artifact` em `mr-bones-mapfiles.zip`: SHA-256 `7d4a7c4c…35ecff`, 28 nós / 27 arestas, inventário completo das entradas ZIP (`ev_1bc4e0cc…`, inventário `ev_c57e6ac6…`).
  - `inspect_byte_entropy` `/02`: 6,5493; run baixa em offset 122880, 8176 bytes (`ev_9c31f5ef…` / pai `ev_02c6c5c0…`).
  - `inspect_byte_entropy` RUNWAY `03CODE`: 5,9903; run baixa em 598016, 90112 bytes (`ev_46ee295a…` / pai `ev_a451c16f…`).
  - Extraído `/04RUNWAY/05SND1` (471.784 bytes, SHA-256 `1e0a8ff4…d97968`); entropia **7,3880** com várias runs altas (≥7,2) — compatível com samples comprimidos/codificados; **não** distingue compressão de cifra (`ev_d7130db6…` / pai `ev_719b3fbd…`).
  - Bundle exportado: `C:\Users\jneom\Neomaster\mr-bones-rea\session-evidence.json` (7 registros).
  - Limite do provedor: Ghidra via REA no Windows P0 **não admite SH-2**; descompilação SH-2 segue no Ghidra GUI/headless local (`SuperH:BE:32:SH-2`), fora do bridge REA desta sessão.

### Próximos trabalhos (atualizados)

1. Referência jogável RUNWAY-001 no Ymir (ainda pendente).
2. Agendador cooperativo (`ObjYeild`) e brains (`Stand_Brain` / `BonesWalk`) a partir da análise já salva.
3. Composição real (posições via projeção/descritores, paleta por banco, ordem de planos).
4. Codec/driver dos samples em `05SND1` (entropia alta; investigar segmentos já parseados por `snd1.c`).
5. Revalidação individual dos símbolos do carregador divergentes do mapa.
6. Executável nativo integrado só após sequência reproduzível comparada à referência.

## 10. Migração para F:\MrBonesPort

- Árvore do port (código, docs, manifest, build regenerado) unificada com `disco1`/`disco2` em `F:\MrBonesPort`.
- `C:\Users\jneom\Neomaster\mr-bones-port` passou a ser junção para `F:\MrBonesPort` (mesmo destino que `Neomaster\MrBonesPort`).
- Build limpo no F:: **21/21** testes; `mbvisual --check` e `mbverify --quick` no disco 1 OK.

## 11. Agendador cooperativo, joypad brain e harness `mbrunway`

### Inspeção

- Git em `master` a partir de `5afa9ed`; grande volume de trabalho nativo ainda sem commit (preservado).
- Testes anteriores: 21. Após esta etapa: **22/22**.
- Ymir + BIOS `mpr-17933.bin` localizados na área Codex e copiados para `tools/ymir/` (SHA-256 BIOS `96E106F7…30266F`). Ficha `docs/reference-runway-001.md` criada; **RUNWAY ainda não observada** no emulador nesta sessão.

### Ghidra (fatos)

- Headless SuperH em `/02`: `docs/ghidra-objyeild.txt`, `docs/loader-symbols.md`.
- `_ObjYeild` @ `0x002050b0` e `__ObjNewBrain` @ `0x00205168` **coincidem** com o mapa e têm prólogos/literais coerentes (cursor `0x00205058`).
- `_SMPCgetPad` mapa `0x0020bf34` permanece **interior**; início real `0x0020bf08`. `_CreateLoader` mapa `0x0020b860` vs início plausível `0x0020b834`. Sem deslocamento global único.

### Código nativo novo

- `obj_scheduler.c` — fibras Windows espelhando troca de SP de ObjYeild; `mb_obj_new_brain`, `mb_obj_delay`.
- `joypad_brain.c` — uma volta de JoypadBrain `0x00269774` (cópia para BonesInput, falhas→bit 0x08).
- `mbrunway.exe` — harness: carrega disco, compõe frame diagnóstico, roda joypad+input no agendador. Imprime explicitamente **NOT PLAYABLE**. Não é fase jogável.
- `mbrunway --check --steps 60`: `compose 320x224 steps=60 x=117960 yields=120`.

### Limitações

- Agendador nativo usa Fibers; não reproduz layout exato da ring table SH-2 em RAM.
- Composição e paleta continuam diagnósticas.
- Sem áudio integrado no harness.
- Sem referência RUNWAY-001 capturada.

### Próximos (anteriores — parcialmente cobertos na §12)

1. Observar RUNWAY no Ymir e preencher a ficha 001 (entradas/quadros/áudio).
2. `Stand_Brain` / `BonesWalk` / colisão a partir de `brain-deep.txt` / `walk-deep.txt`.
3. Composição por descritores/projeção reais.
4. Samples `05SND1` + eventos já parseados.
5. Só então aproximar critério de “RUNWAY concluída”.

## 12. RUNWAY nativa — critério de fase (reconstrução)

**Critério pedido:** iniciar, controlar e finalizar RUNWAY no executável nativo com gráficos e áudio, diferenças documentadas.

### Entregue

- `runway_phase.c` + `mbrunway.exe`: carrega disco → identidade → pan/sprite/paleta → loop stand/walk → finish por goal X ou Start; Escape aborta.
- Gráficos: fundo 320×224 + sprite projetado compostos em RGBA.
- Áudio: CD-DA track 2 (`host_audio` / waveOut) no modo janela; SFX PCM8 one-shot a partir de `05SND1` span 4 / sample 5 (~3748 B, 22.05 kHz) na borda stand→walk.
- Colisão: mapa 4480 B = 40×28 tiles × 4 B; célula `0xffffffff` = sólido; chão = topo sólido por coluna + snap Y; parede = degrau > `MAX_STEP_UP` (4).
- Testes: **25/25** (incl. `test_snd1_pcm`); `mbrunway --check` → `frames=143 status=RUNWAY finished (goal)` + `sfx_pcm8=3748`.
- Docs: `docs/runway-brains.md`, `docs/reference-runway-001.md` (bloqueio Ymir), `coverage.md` diferenças.

### §12b — Colisão útil + SFX SND1 (continuação)

- `pan_collision_map_*`: API tile (`solid_tile` / `ground_ty`); walk usa step-height.
- `snd1_pcm_*`: tabela BE16 → PCM8 assinado; `host_audio_play_pcm8` one-shot waveOut.
- Ainda experimental: codec SCSP real, multi-sample events, animação multi-frame.

### §12c — REA MCP + Ghidra Stand/Walk (2026-11-07)

- REA: SuperH raw recusado no Windows P0; entropy `03CODE` 5.9903 (`ev_8e3ae51b…`).
- `03CODE` load base **`0x00232000`** (não `0x00200000`).
- InputBrain: L=3932, R=1966 → `WALK_STEP_LEFT/RIGHT` no nativo.
- IDs Stand/Walk documentados em `docs/rea-runway-session.md`; dump `docs/ghidra-runway-brains.txt`.
- `D:\Emulation\roms\saturn\Mr. Bones (USA)` = mesmo Track01 que `F:\MrBonesPort\disco1`.

### §12d — BBS post/probe + tall (continuação)

- Loader: post `0x0020b264` / probe `0x0020b308` → `bones_bbs_*`; dump `docs/ghidra-message-probe.txt`.
- `runway_phase`: Down↔tall + msgs `0x10700`/`0x10600`/`0x20600`; boot `0x20A00`; passos tall.
- Testes: **26/26**; `mbrunway --check` → frames=286 goal.

### §12e — BBS peek/consume + handlers (continuação)

- APIs: peek `0x20b2e8`, consume `0x20b308`, match `(id&mask)==value`, unlink `0x20b2a4`; dumps `ghidra-bbs-peek.txt`.
- Stand: boot `0x20A00` = consume; `0x10700`/`0x20600` = peek; `0x10700`→`0x204128(0xc)` (scheduler enqueue) + `0x25d534` (brain switch).
- InputBrain célula `0x2858BC`: R-nontall no-op; idle nontall +1966 — host R em `bones_x` permanece adaptação documentada.
- Fase: Down held→post/frame; tall set/clear; C edge; testes BBS (FIFO/máscara/peek) + phase msgs.
- Ymir-001: CLI `-d` sobe processo; sem captura nova em `ymir-profile` (bloqueio de montagem/UI persiste).
- Testes: **26/26**; `mbrunway --check` → frames=286 goal + SFX.

### §12f — TallSet + InputBrain na fase (continuação)

- **Tall writer:** `TallSet_26D784` escreve `*(0x28DB8C)` = 1 se o brain corrente (`*0x28589C`) ∈ conjunto crouch `{0x2873EC…0x288084}` (8 slots); senão 0. Down→`0x10700`→`0x204128(0xc)` agenda brains; TallSet deriva tall.
- Fase usa `runway_input` (célula `input_cell` + posts Down/C); walk world continua host-facing.
- SFX PCM8 também em tall-enter e C (`sfx_flags`); ainda não SCSP.
- Projeção: `*28>>16` ≡ RWorldSprite `*7>>14`.
- Testes: **26/26**; `mbrunway --check` frames=286.
- **RUNWAY fiel ponta a ponta: NÃO concluída.**

### §12g — Mapa Bones X/Y (RWorldSprite → writers); host walk intacto

- **Lido pelo sprite:** `RWorldSprite` usa `*(int*)*param_1` / `+4` (world X/Y) e `+0xc/+0xe` (ângulo); câmeras `0x228FA4`/`0x2297B4`.
- **Objeto:** bloco `0x2858BC` = {X,Y,Z} + `0x2858C8` (+0xC) casa com o layout; grade de colisão = `0x2858D4`/`0x2858D8`.
- **Writers:** `InputBrain` soma em `0x2858BC` (X); `NearVel` integra vel em XYZ; `WalkAnim` reescreve via `WorldXform_206AB0(grid)`; `BonesYeild` faz `InvXform_2078C0(world)→0x2858D4` e, se clamp, `WorldXform→0x2858BC`.
- **Host walk NÃO trocado:** R nontall é no-op no InputBrain; movimento R real exige vel `0x285904`/`0x285928` → NearVel. Próximo elo antes de unificar `bones_x`.
- Dumps: `docs/ghidra-bones-world.txt`, `ghidra-world-writers.txt`, `ghidra-world-xform-loader.txt`, `ghidra-bones-pos-link.txt`, `ghidra-inv-xform.txt`; mapa em `docs/runway-brains.md` §12g.

### §12h — vel → NearVel → world; walk host unificado; EXE jogável

- **Layout:** vel vec3 `0x2858FC` (velA=`0x285904`), accel `0x285908`, velB `0x285928`.
- **Writers:** `VelThrust_25E48C`, `NearVel_25B1C8` (`vel+=accel` + world), `CMover_Move_259D28`, `KillVelAccel_25E088`, `VelFn_262FD0`/`2636F8`.
- **Nativo:** pad → `vel`/`vel_a`/`vel_b` → integrate → `bones_x`; `input_cell = bones_x`. InputBrain só scratch (mensagens).
- Dumps: `ghidra-vel-chain.txt`, `ghidra-vel-writers2.txt`. Mapa em `runway-brains.md` §12h.
- Jogatina RUNWAY: start / L-R / tall / C / goal / CD-DA / SFX PCM8 — diferenças restantes abaixo.

### §12i — Air/jump + gravidade (nativo jogável)

- Stand peeks `MSG_JUMP` `0x20800` (flag&8 → BrainSwitch). Host: B edge → post + `vel[1]=JUMP_IMPULSE`, `accel[1]=GRAVITY_Y`, `RUNWAY_ST_AIR`.
- Integração CMover (`vel+=accel`); land snap em `ground_ty`. Impulse/gravidade ≈ família VelThrust — **não** byte-idênticos ao Saturn.
- Dump: `docs/ghidra-jump.txt`. Help mbrunway: X=jump. SFX bit8.

### §12j — Anim multi-frame STXTR (Bones body bank)

- Descritores VDP1 em `03CODE` `@0x0023204C`, stride `0x38`, **13** records → STXTR (tamanhos 32×13…48×44…32×14).
- `runway_phase` decodifica o banco; walk cicla por `bones_x` screen px / 6; stand segura índice; air segura frame 5 (48×44).
- Sem tabela de índices SH-2 fechada (refs literais aos VAs = 0); seleção nativa é aproximação jogável. Sem flip-X / VDP1.
- Clusters maiores (`0x253858` 20 frames ~56×45 etc.) ainda não ligados.

### §12k — Colisão fina GetHixel / PushOutOfWalls / WallAboveCheck

- MAP: `_PANCOLLGetHixel` `0x256a68`, `_BonesPushOutOfWalls` `0x25c694` (ex-WalkAnim), `_BonesWallAboveCheck` `0x25c900` (ex-CollProbe).
- Mapa: 4480 B = **40×112** fine rows; hixel 2-bit via `BitExtract_213FA0(byte, x&6)&3`; sólido = 3. Bias grid `+0xA0/+0x70` → pixel 0..319×0..223.
- HitChkScrn* `.data` zerado na imagem; writer fechado em §12l.
- Fase: push-out após integrate; jump bloqueado se wall-above. Dumps: `ghidra-coll-fine.txt`, `ghidra-coll-fine-code.txt`.

### §12l — HitChk runtime + bancos STXTR + flip-X + CPanScroll

- **Writer HitChk:** `BonesSetConfig` `0x25d75c` copia `config+0x50..0x6c` → pool `PTR@25d904..920` → `HitChkScrn{Top,Bot}{Left,Right}{X,Y}` `@0x285A6C..88`. Mid `@0x285A5C..68` sem fill neste path.
- **Template bnscfg** `0x2873EC` (e crouch set): AABB TR/TL/BR/BL = (±5, −8)/(±5, +12). Nativo: `pan_hit_offsets_default` + `from_config`; origem HitChk = centro → lift feet−12; push filtra chão (sólido com ar 2px acima).
- **Bancos:** morph `0x23204C`×13 + full `0x253858`×20 (stride `0x38`). Walk → morph; stand/air/tall → full. Índice walk ≈ screen-X (stand-in de `Mplaynext` cue `0xc`; tabelas VA não literais).
- **Flip-X:** `runway_compose_blit_sprite_ex` quando `facing<0` (H-flip VDP1).
- **CPanScroll** `0x257320`: Brain loop infinito atualiza shorts de scroll + helpers pan-anim; host ainda 1 frame `15PAN` (multi-frame/scroll doc-only).
- Dumps: `ghidra-hitchk-config.txt`, `ghidra-hitchk-writers.txt`, `ghidra-stride38.txt`; CPanScroll em `ghidra-bones-pos-link.txt`.
- Validado: CTest **26/26**; `mbrunway --check` frames=286 goal + PCM8.

### §12m — TallSet real + 15PAN multi-frame + Mplaynext

- **Tall:** remove proxy Down-held. `MSG_DOWN` `0x10700` → `sched_enqueued_0c` (loader `0x204128(0xc)`) + `BrainSwitch` stand-in → `brain_slot` ∈ crouch `{0x2873EC…0x288084}` → `TallSet_26D784` → `tall`. Release → `BONES_BRAIN_STAND`. Módulos: `bones_tall.*`.
- **15PAN:** blob ~38 MB, **2651** records pandcomp (BE32-sized). `pan_scroll` / `pan_stream_*` seek por `camera_x` (CPanScroll/PlayPanAnim stand-in). Visual atualiza; **colisão congelada no record-0** (deltas por frame ainda não alinhados ao `bones_x` host — seeking vivo bloqueava o walk).
- **Mplaynext:** cue stride `0xc`, duração default **8** (tabelas ~`0x233D8C`); `mplay_seq` avança índice do banco morph no walk (substitui screen-X).
- Validado: CTest **28/28**; `mbrunway --check` frames=286 goal + PCM8.

### §12n — SoundEvent / SoundStartSequence + 05SND1 multi-SFX

- **RE (Ghidra):** `_SoundEvent` `@0x204128` (LOADER) percorre records de 0x20 B na tabela de eventos (comp0 SND1); `_SoundStartSequence` `@0x2041d0` rotaciona contadores de grupo (0→0..2, 1→3..4) e chama callback. Stand_Brain flag&2: `SoundEvent(0xc)` antes do BrainSwitch; BonesWalk também referencia `0xc`. Dumps: `docs/ghidra-sound-loader.txt`, `docs/ghidra-sound-code.txt`.
- **05SND1:** 12 spans BE32; span4 = 20 PCM8 (s05/s07=3748); span5 = 24 PCM8. Banco host `runway_sfx`: WALK→s4.5, TALL→s4.7, C→s4.8(68), JUMP→s5.6(612), LAND→s4.4(36) @22050 Hz.
- **Fase:** `sfx_flags` multi-bit (WALK/TALL/C/JUMP/LAND); `sound_event_id` = índice `_SoundEvent` (Down→`0xc`; limpo por frame). `mbrunway` despacha via `runway_sfx_dispatch` + `host_audio_play_pcm8` (além do one-shot antigo).
- **Nota:** `sched_enqueued_0c` = legado do nome; significa SoundEvent(0xc) disparado, não enqueue de scheduler Obj.
- Validado: CTest **29/29** (`test_runway_sfx`); `mbrunway --check` multi-SFX bank.

### §12o — EndLevel / SkelBar / pause / hazards

- **RE (Ghidra 03CODE):** `endgame.o` `CEndLevel`/`EndLevel@2563d4`; `EvalSuccess_CLevelManager@26d574` → `EndLevel`; `BonesLooseSkeletism@2686c0` → `EndLevel(1)` na morte. `skelbar.o`: max `0x20000`, `+0x48` base / `+0x4c` pump, `UpdateSkelBar` → 1..14 segs (step −9314). `PauseBrain@2685dc` seta `bPaused` (~`0x2143EC`) até Start. `CheckForHit_CThrown@26da50` range `0xB6DB` (~20px) → `BonesObjHitChk`. `CheckFallHole` / gang `CheckObstacle` / poofs / lord dumpados. Dump: `docs/ghidra-endgame.txt`.
- **Host:** `runway_skel` + `runway_hazard` (3 zonas + thrown). Start = pause toggle; goal X = `FINISHED` (EndLevel win); skeletism 0 / fall = `LOST`. HitTimeout=20; dano default `0x4000`.
- Validado: CTest **30/30** (`test_runway_skel`); `mbrunway --check` EndLevel win + skel/hits.

### §13 — 03MENU + NvmShadow + mrbones.exe

- **RE loader `/02`:** `BackupLoad/SaveNvmShadow` copiam **0x28** B; `LoadFirstLevel@20b91c` init vazio (`[4]=1`, `+0x1c=3`); `DirNameTable@22da00` / `DirTable` / `DirIDTable` (BSS runtime). Dump: `docs/ghidra-loader-backup.txt`.
- **RE `/03MENU/03CODE`:** `main@23c278` “Starting MENU”; `CMenuSelector` Brain/StartMenuScreen/SelectMenuEntry (cases 0–6: start level, Options, LevelSelect, toggles); `StartScreen` / `LevelSelectScreen` / `OptionsScreen`. Dump: `docs/ghidra-menu.txt`.
- **Host:** `nvm_shadow` → `<save_dir>/nvmshadow.bin`; `menu_phase` Title→Start→Options/Levels; `mrbones.exe` MENU→RUNWAY (outros níveis: mensagem).
- Validado: CTest **32/32**; `mbrunway --check` OK; `mrbones --check` MENU→RUNWAY EndLevel OK.

### §14 — stage_runner multi-fase + discos 1/2

- **MAPS (mr-bones-maps):** side-scroll MAUSO/VILLAGE/BATWOOD1–3/GLASS/VALLEY/SHADOW/VORTEX/LAFFRIOT → `scroller`+`scrlcoll`+`sidescrl`+`bnscore` (sem `pan_coll`). ICELAKE1–2/MOAT/LOGS/RIDE/STAING/RUNWAY → `pan_coll`+`CPanScroll`+`15PAN`. INSERTCD/INSRTDUK: mains curtos; `_DirNameTable@22da00` / `gfdr_setupDirNameTbl`.
- **Discos:** Disc1 tem 03CODE real nas fases cedo (RUNWAY…VORTEX); Disc2 stub (~19800) nessas. Disc2 tem real RIDE…LAFF; Disc1 stub. `STAGE_STUB_CODE_MAX=20000`.
- **Host:** `stage_catalog` (DirNameTable stand-in + preferred_disc); `stage_assets` (02TITL/03CODE/04STXT/05SND1/15PAN genérico); `stage_runner` (PLAYING / NEED_DISC=INSERTCD / smoke finish). `runway_*_load_dir(iso_dir)`. Só RUNWAY → `runway_phase` jogável; demais = smoke de assets.
- **mrbones.exe:** MENU → qualquer nível do catálogo; `--disc2` + troca automática; `--level /NNNAME`; `--check` MENU→RUNWAY + MAUSO smoke + RIDE NEED_DISC→disc2.
- Validado: CTest **34/34** (`test_stage_catalog`, `test_stage_runner`); `mrbones --check --disc2` multi-disc OK.

### §15 — especiais + campanha 24 + NvmShadow + discos

- **MAPS especiais:** GUITAR/DRUM → `guitar.o`/`drum.o` + scrlcoll + EndLevel; GENROOM → pan_coll + reds/blues/zap; BIGBONES → pan_coll + skelbar; FINALE → `finale.o` + scrlcoll; DUKMOVIE → `DUCKPLAY`/`20AVI` (não é dir ISO; por fase `19DUCK`/`20AVI`); LOSE → AVI-only (`20AVI`~1.3MB, sem 03CODE).
- **Discos:** GUITAR/DRUM/BIGBNS reais no Disc1; GENRM/FINALE reais no Disc2; TROLL1/2 EMPTY (sem CODE real); LOSE AVI em ambos.
- **Host:** `stage_kind` + campanha 24; `stage_progress` (win→unlock next + `mb_nvm_save`; lose→`/28LOSE`; FINALE→COMPLETE); `stage_assets` carrega DUCK/AVI; EMPTY = skip instantâneo.
- **mrbones.exe:** MENU→campanha; auto-save NvmShadow; INSERTCD D1↔D2; `--campaign` / `--level`; `--check` percorre 24 fases + especiais + LOSE.
- Validado: CTest **35/35** (`test_stage_progress`); `mrbones --check --disc2` EXIT=0; nvm `level_word=24` / 24 unlocks.

### §16 — pan-playable 15PAN + HUD + MENU STXT

- **Fases jogáveis (além de RUNWAY):** LOGS (D1), ICE1/ICE2/MOAT/RIDE/STAING (D2) — `stage_catalog.playable=1` quando `15PAN` + CODE real; `stage_runner` → `runway_phase_load_dir`.
- **Loader flexível:** `runway_assets_load_dir` exige CODE+STXT; TITL/SND1 soft fora de RUNWAY; palette scan + grayscale fallback; STXTR walk / placeholder sprite; `runway_sfx` soft em spans ausentes; `finish_x` curto + hazards off em `generic_pan`; soft-floor quando coll record-0 vazia (RIDE/ICE/STAING).
- **HUD:** `runway_compose_hud_skelbar` (14 segs) + `runway_compose_hud_pause` (PAUSE.O / placa âmbar); blit HEALTH.O heurístico no render.
- **MENU:** `menu_phase` carrega `/03MENU/04STXT` (+ CODE palette) e blita placas STXTR no frame.
- **Resume NEED_DISC:** flag `stage_runner.playable` preservada (catálogo NULL no resume).
- Validado: CTest **35/35**; `mrbones --check --disc2` EXIT=0 (LOGS/RIDE/ICE1/ICE2/MOAT/STAING = EndLevel win nativo).

### §17 — sidescrl_phase (scroller sem 15PAN)

- **MAPS:** MAUSO/VILLAGE/BATWOOD1–3/GLASS/VALLEY/SHADOW/VORTEX/LAFFRIOT → `scroller.o` + `scrlcoll.o` (`pFloorHixels`, SCRLCOLLGetHixel3D/findfloor) + `sidescrl.o` (LoadCDB/LoadMap/DrawMap/CheckScroll) + `bkgdata.o`.
- **ISO:** 02TITL + 03CODE + 04STXT + 05SND1 + camadas 07ACDB…14DTIL (sem 15PAN útil).
- **Host:** `sidescrl_phase` — carrega STXT/TITL/CODE + layers; strip scroll a partir de ATIL; sprites STXTR; walk/jump/pause; `runway_skel` + hazard zones; CD-DA track 3 + `runway_sfx` soft. Soft floor (LoadMap/`pFloorHixels` runtime ainda aberto).
- **stage_runner:** `sidescrl_live`; catalog `playable` para as 10 fases side; prefer runway se `15PAN>100`.
- Validado: CTest **36/36** (`test_sidescrl_phase`); `mrbones --check --disc2` EXIT=0 (MAUSO/VALLEY/BAT*/GLASS/VORTEX/VILLAGE/SHADOW/LAFF = EndLevel win nativo).

### §18 — special_phase (ritmo / ação / apresentação)

- **MAPS:** GUITAR/DRUM → `guitar.o`/`drum.o` + scrlcoll + EndLevel; BIGBONES/GENROOM → pan_coll + skel/reds/blues; FINALE → `finale.o` + scrlcoll; DUKMOVIE → 19DUCK/20AVI via `/28LOSE` (não é dir ISO); LOSE AVI-only.
- **ISO:** Disc1 = GUITAR/DRUM/BIGBNS reais; Disc2 = GENRM/FINALE reais; LOSE AVI+DUCK em ambos.
- **Host:** `special_phase` — modos `RHYTHM` (janela de beat A/B/C/RIGHT, 8 hits → win), `ACTION` (walk/jump + skel soft hazards como sidescrl), `PRESENT` (páginas STXT; FINALE → WIN, LOSE → LOST). Render 04STXT/02TITL; cues SND1/CD-DA via `sfx_flags`. Soft: tabelas rhythm literais / AVI decode / AI inimigos.
- **stage_runner:** `special_live` preferido sobre runway mesmo com 15PAN (BIGBNS/GENRM); catalog `playable=1` para RHYTHM/BIGBONES/GENROOM/FINALE com CODE>stub; LOSE via special mesmo com playable=0.
- **mrbones --check:** RIGHT+A pulsado em ritmo; 5000 frames nas especiais; LOSE avi via `special.avi_size`.
- Validado: CTest **37/37** (`test_special_phase`); `mrbones --check --disc2` EXIT=0 (GUITAR/DRUM rhythm win; BIGBNS/GENRM action win; FINALE present win; LOSE present→outcome LOSE avi~1.29MB duck~135k).

### §19 — partida completa mrbones.exe (sem smoke)

- **Fluxo:** 03MENU visual (STXT gfx) → campanha nativa Disc1 (RUNWAY…VORTEX, skip TROLL) → INSERTCD Disc2 → RIDE…LAFF → FINALE → CREDITS → `nvmshadow.bin`.
- **Host:** `--full-campaign` / `--check` rejeitam smoke; cada fase exige `runway_live` | `sidescrl_live` | `special_live`. CD-DA reinicia por fase + `05SND1` via `runway_sfx`. Win32: `APP_CREDITS` (180 frames) pós-COMPLETE; auto-save NvmShadow a cada win.
- **Contagem:** 22 fases jogadas (24−2 EMPTY); LOSE/DUKMOVIE validado à parte no `--check`.
- Validado (reconfirmado): CTest **38/38** (`test_full_campaign`); `mrbones --full-campaign --disc2` EXIT=0; `mrbones --check --disc2` EXIT=0 (`check: §19 full-campaign + LOSE/DUKMOVIE + NvmShadow OK`); `nvmshadow.bin` 40 B / `level_word=24`.

### §20 — fidelidade crítica (duck_avi + floor ATIL + rhythm)

- **duck_avi:** RIFF/AVI Saturn (`LIST/rec` + `00dc` DUCK 320×208 + `01wb` PCM). Soft TM1 block paint (sem tabelas 19DUCK completas); PCM one-shot no host. Integrado em PRESENT (FINALE/LOSE).
- **sidescrl:** floor ATIL soft (§20); §23 promove a `scrl_map` (LoadCDB/AMAP/SCRLCOLL).
- **rhythm:** `beat_period` por fase (GUITAR=16, DRUM=12).
- Validado: CTest **39/39** (`test_duck_avi`); LOSE `frames_drawn>0`; FINALE `avi_frames` capped 240; `mrbones --check --disc2` EXIT=0.

### §21 — host nativo final (`mrbones` 0.21.0)

- **Entrega:** executável de campanha completa sem emulação SH-2 — MENU→22 fases nativas→INSERTCD→FINALE→CREDITS + duck_avi + CD-DA/SND1 + `nvmshadow.bin`.
- **CLI:** `--version` → `mrbones 0.21.0`; `--full-campaign` / `--check` rejeitam smoke.
- **Escopo fechado (host):** jogatina ponta a ponta nos discos USA. Fidelidade SH-2/Ymir/SCSP/VDP permanece explicitamente fora (lista abaixo).
- Validado: CTest **39/39**; `mrbones --check --disc2` EXIT=0; `mrbones --version`.

### §22 — TM1/19DUCK + 15PAN coll live + hazards×7 (`mrbones` 0.22.0)

- **duck_tm1 / duck_avi:** tabelas reais 19DUCK (BE ydt/cdt @~131436 + vector walk @~131988, blob 134744) via `duck_avi_open_with_duck`; decode TM1 method 16/24 com fallback soft. Usado em PRESENT (FINALE/LOSE) e validado em GUITAR/LOSE.
- **15PAN coll dinâmica:** `pan_scroll_update` aplica `pan.collision` do pandcomp atual; record-0 = world-absolute; frames seguintes = viewport com `coll_probe_px` (bones−camera+START_PX). Soft-floor genérico mantido.
- **Hazards×7:** `runway_hazards_init_pan_stage` para RUNWAY/LOGS/ICE1/ICE2/MOAT/RIDE/STAING; step ativo em todas as fases pan.
- Validado: CTest **39/39**; `mbrunway --check` EXIT=0; `mrbones --check --disc2` EXIT=0; `--version` → `0.22.0`.

### §23 — SCRLCOLL / LoadCDB / multi-camada (`mrbones` 0.23.0)

- **Análise MAP/CODE:** MAUSO `sidescrl.o` LoadCDB@`2a1d38` LoadMap@`2a1c78` DrawMap@`2a19f4`; `scrlcoll.o` GetHixel3D@`2a2ee8` findfloor@`2a2f0c` InitGetFloor@`2a2e6c` `pFloorHixels`; `pMapDefA/B/C/D` + `bkgdata.o`.
- **CDB:** header BE16 A-style `[pix_w][pix_h]…` (MAUSO 1176×608, BAT1 3160×1344) ou B-style `[flags][…][tw][th][n]`; pares 07ACDB/08ATIL … 13DCDB/14DTIL.
- **AMAP:** polyline BE16 `(x,y)` → floor `y>>8` (VALLEY/BAT*); walls só com AMAP (degrau >36px).
- **Host `scrl_map`:** init planos A–D, `build_collision` (=InitGetFloor), `draw` (=DrawMap parallax 1/2/3/4), `get_hixel`/`find_floor`/`wall_at`. Palette CODE+02TITL; `sidescrl_phase` compõe strip + física SCRLCOLL nas 10 fases side.
- Validado: CTest **40/40** (`test_scrl_map` + `test_sidescrl_phase`); `mrbones --check --disc2` EXIT=0; `--version` → `0.23.0`.

### §24 — rhythm tables + inimigos/boss (`mrbones` 0.24.0)

- **Análise:** `DrumSequence@0026b58c` (drumtab.o, 16-byte BE `{time,kind0..3,flags,meta}`); GUITAR `SequenceTable@28c3ac` + period 16; RUNWAY gang/lord/poofs/tbones (`CheckForHit` range `0xB6DB`); GENRM reds/blues/`TimingZapMax=0x10`; FINALE `RingDefinitionTable@2b0158`. Dump: `docs/ghidra-rhythm-enemies.txt`.
- **Host `rhythm_table`:** 48 notas DRUM + grid GUITAR; kind→A/B/C/RIGHT; wired em `special_phase` RHYTHM.
- **Host `stage_enemy`:** Gang Throw→CThrown, Lord/Poofs, GENRM Red/Blue/Zap, FINALE Ring/Bolt (fx sem dano no PRESENT), BIGBNS lord/gang. Integrado em `runway_phase`, `sidescrl_phase`, `special_phase`.
- Validado: CTest **41/41** (`test_rhythm_enemies`); `mrbones --check --disc2` EXIT=0; `--version` → `0.24.0`.

### §25 — DirNameTable + title AVI + créditos (`mrbones` 1.0.0)

- **Dir\* tables:** LOADER.MAP COMMON `_DirNameTable@0022da00` (0x3c0), `_DirTable@0022ddc0` (0xc), `_DirIDTable@0022f950` (0x1e0) são BSS preenchidos em runtime por `GFDR_SetupDirTbl`/`gfdr_setupDirNameTbl` a partir do ISO — **não** estão no blob `02LOADER.bin`. Host embute as 28 folhas top-level (03MENU…30MAP, slots 16 B) + `mb_dir_table` (count=28, VAs literais) + DirID 1-based; `stage_catalog_verify_dir_tables` confere contra `cd_iso_walk`.
- **MENU title:** `menu_phase` carrega `/03MENU/02TITL` + `20AVI` + `19DUCK`; `duck_avi_open_with_duck` + step até `MENU_TITLE_AVI_MAX` (90); Start pula para START.
- **Créditos pós-FINALE:** `CREDITS_HOLD_FRAMES=360` × `CREDITS_PAGES=6`; roll de folhas DirNameTable (barras soft); wired em Win32 timer e `--full-campaign`/`--check`.
- Validado: CTest **41/41**; `mrbones --full-campaign --disc2` EXIT=0; `mrbones --check --disc2` EXIT=0 (`check: §25 host 1.0.0 DirNameTable + title AVI + credits 1.0 OK`); `mbrunway --check` EXIT=0; `--version` → `1.0.0`.

### §26 — DecompByte SH-2 + Fonte Bitmap 8×8 + Renderização de Inimigos/Chefes + Mundo Multi-Telas + Pista de Notas (`mrbones` 1.0.0+)

- **RE SH-2 (`docs/ghidra-vdp2-tiles.txt` + `docs/ghidra-decompbyte.txt`):**
  - `_DecompByte@0x002a0b4c` (`compress.o`, size `0x4ec`): bit-pump MSB-first + jump table de 16 nibbles `@0x002a0b94` (`nibble 0` → `0x2a0bd4` fim de stream/SCU DMA; `nibble 1..15` → `dist = code >> 4`, copia `nibble + 1` bytes `2..16` de `dst - dist`).
  - `_LoadCDB@0x002a1d38`, `_LoadMap@0x002a1c78`, `_DrawMap@0x002a19f4` → `_AddTiles@0x002a1570`, `_SCRLCOLLInitGetFloor@0x002a2e6c` → `_MAPGetHixel@0x002a3016`, `guitar.o` (`StartLick`/`StopLick`/`Brain__7CGBones`), `fbones.o`/`ring.o`/`bolt.o` (`ShootRing` + `RingDefinitionTable@0x2b0158`).
- **Fonte Bitmap 8×8 (`mb_font.h` / `mb_font.c`):**
  - 95 glifos ASCII (`0x20..0x7E`) com `mb_font_draw_text_shadow` / `mb_font_draw_text_centered`.
  - Textos reais renderizados no framebuffer `320×224` em `menu_phase.c` (`TITLE`, `MAIN MENU`, `OPTIONS`, `LEVEL SELECT`), `runway_compose_hud_pause` (`"PAUSED"`), HUD das fases (`runway_phase.c`, `sidescrl_phase.c`, `special_phase.c`) e rolo de créditos de 6 páginas (`mrbones.c`).
- **Inimigos, Projéteis e Chefes Visíveis (`stage_enemy.h` / `stage_enemy.c`):**
  - `stage_enemies_render` desenha sprites animados para `GANG`, `LORD`, `POOF_RED`, `POOF_BLUE`, `RED`, `BLUE`, bobinas/raios `ZAP`, anéis `RING`, raios `BOLT` e projéteis `CThrown` em todas as fases `15PAN` (×7), `sidescrl` (×10) e `special` (`GENRM`, `BIGBNS`, `FINALE`).
- **Mundo Multi-Telas & Células VDP2 8×8 (`scrl_map.c` + `sidescrl_phase.c`):**
  - `scrl_decomp_byte` implementa `_DecompByte@0x002a0b4c`; `sample_plane` amostra células 8×8 de `cdb + 10` (`LoadCDB`) + `til + 0x48` (`DecompTile`); `sidescrl_phase` percorre a largura multi-telas do mapa (`scroll_w`) com câmera contínua e inimigos distribuídos (`stage_enemies_populate_sidescrl_strip`).
- **Vídeo `20AVI` + Note Highway em `GUITAR`/`DRUM` & TM1/Áudio (`special_phase.c`, `duck_tm1.c`, `mrbones.c`):**
  - `special_phase` reproduz o vídeo `20AVI` de fundo durante `RHYTHM` e `ACTION`, desenha a pista de notas rolante (`[A]`, `[B]`, `[C]`, `[R]`), dispara múltiplos samples `05SND1` por tipo de nota e estende o buffer PCM `01wb` (`262144` samples) + preditor chroma de blocos `4×4`/`2×4` em `duck_tm1.c`.
- **Validado:** CTest **41/41** (100% aprovados); `mrbones --check --disc2` EXIT=0.

### Fora de escopo host (fidelidade SH-2 — não bloqueia 1.0.0)

- DecompTile/TileHeap/VDP2 cell bit-exact; SCSP; VDP1.
- Gang/Lord/Poofs AI byte-fiel (pathfinding/anim full); TM1 bit-exact vs FFmpeg/mfp_avi; créditos/AVI SH-2 literais (font atlas).
- GFS Dir\* fill em runtime no Saturn (host usa literais ISO + verify); guitar.o lick structs / finale.o Brain full; TROLL sem dados.
- Ymir RUNWAY-001; recompilação SH-2 completa.

### Comando

```
F:\MrBonesPort\build\mrbones.exe --version
F:\MrBonesPort\build\mrbones.exe "F:\MrBonesPort\disco1\Mr. Bones (USA) (Disc 1).cue" "F:\MrBonesPort\manifest\versions.txt" --disc2 "F:\MrBonesPort\disco2\Mr. Bones (USA) (Disc 2).cue"
F:\MrBonesPort\build\mrbones.exe "...disc1.cue" "...manifest\versions.txt" --disc2 "...disc2.cue" --full-campaign --save-dir F:\MrBonesPort\build\full_campaign_save
F:\MrBonesPort\build\mrbones.exe "...disc1.cue" "...manifest\versions.txt" --disc2 "...disc2.cue" --check --save-dir F:\MrBonesPort\build\check_save
F:\MrBonesPort\build\mbrunway.exe "...cue" "...manifest\versions.txt" --check
```

### 2026-11-08 — diagnóstico de gráficos e abertura

- O host Windows usa renderização GDI por software (`StretchDIBits`); não há pipeline de carregamento de shaders no código examinado.
- Confirmado no código: `pan_scroll` recebe banco 0 de `runway_phase` e converte canais diretamente; catálogo usa `cd_iso_lookup`. Não foi comprovado que isso elimina todas as causas de travamento ou assegura fidelidade de paleta ao Saturn.
- Em `stage_enemy`, corrigidos quatro offsets de registros STXTR que não apontavam para cabeçalhos válidos e o modo 16 bpp (PMOD 5). Removidas inversões adicionais de vermelho/azul em cinco caminhos de sprites: `mb_sprite_decode` e o compositor já trabalham em RGBA; conversão BGRA é feita na apresentação Win32.
- Verificado com o disco 1 que os 20 quadros da caminhada carregam e produzem imagens distintas; incluído teste de regressão em `test_rhythm_enemies`.
- Abertura: `03MENU/20AVI` é TrueMotion1 (DUCK) com áudio ADPCM IMA DK3, conforme `ffprobe`. `menu_phase` avança o AVI mas não compõe esses quadros na tela; seu menu desenha textos e placas heurísticas. `duck_tm1` e `soft_tm1_blit` são aproximações e não foram validados contra FFmpeg/Ymir. A tela original ainda não está restaurada; não há base para declarar o vídeo corrigido.

### 2026-11-08 — investigação da abertura e do áudio

- `ffprobe` identificou `/03MENU/20AVI` como TrueMotion 1 (`DUCK`), 152×176, aspecto de pixel 2:1, 15 fps, 3.087 quadros, e ADPCM IMA DK3 estéreo a 44.100 Hz. O número de quadros e a duração são os declarados pelo AVI, não uma confirmação da sequência exibida pelo jogo.
- O FFmpeg decodificou quadros do arquivo extraído do disco em `build/menu_reference_first.png` e `build/menu_reference_10s.png`; servem de referência do codec, sem demonstrar que o jogo exibe o vídeo inteiro na abertura.
- `duck_avi.c` interpreta bytes comprimidos de `01wb` como PCM; o host também converte para mono de 8 bits e limita a reprodução. O parser pula os cabeçalhos RIFF aninhados e limita os quadros a 2.048. O menu avança o AVI, mas não compõe os quadros na tela; a rotina TM1 atual ainda não foi validada como fiel.
- Nesta tentativa, Ymir abriu sem disco e solicitou configuração da BIOS no perfil corrente. Não foi capturada a tela original nem comparados áudio, temporização e transições. Não foram alterados os decodificadores nesta etapa: a correção e a validação da abertura permanecem pendentes.

### 2026-11-08 — compilação estática Windows x64

- Gerada compilação separada `build-static` com MSVC x64, `Release` e `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` (/MT); o projeto `mbcore` é biblioteca estática.
- `build-static/mrbones.exe --version` retornou `mrbones 1.0.0`. Inspeção PE: imports USER32.dll, GDI32.dll, WINMM.dll e KERNEL32.dll, todas do Windows; nenhuma DLL do runtime Visual C++ consta nos imports.
- `ctest --test-dir build-static --output-on-failure`: 41/41 testes aprovados, incluindo o teste automatizado de campanha. Isso não comprova fidelidade audiovisual nem uma partida manual completa.
- A compilação inclui o estado atual do código; a decodificação fiel e validação da abertura continuam pendentes conforme diagnóstico anterior.

### 2026-11-08 — retomada da abertura

- Ymir iniciado com `tools/ymir-profile` e disco 1: reconheceu MK-81016 e chegou à tela de seleção de idioma da BIOS. Não se obteve a abertura do jogo nem comparação audiovisual.
- Nenhuma correção de codec foi implementada nesta tentativa. A execução `cmake --build build-static --parallel 4` terminou com sucesso; ela recompila somente o estado existente, sem corrigir a abertura.

### 2026-11-08 — correção pontual no leitor DUCK

- A captura fornecida mostra LOSE com listras; ela evidencia corrupção na reprodução, sem identificar isoladamente a causa.
- Corrigida a validação de tamanho em `load_deltas_from_duck`: o código lê até 144 bytes após a assinatura e aceitava apenas 64, podendo ler fora de um recurso truncado. Incluído teste de regressão para assinatura próxima ao fim do buffer.
- Recompilado `build-static` (Release /MT); `ctest --test-dir build-static -R test_duck_avi --output-on-failure` passou. O teste não valida a imagem contra o original. TM1, ADPCM e apresentação fiel continuam pendentes.

### 2026-11-08 — prevenção de áudio DK3 incorreto

- O leitor identifica cabeçalho de áudio IMA DK3 estéreo e não trata seus bytes comprimidos como PCM. Até existir decodificador, o áudio DK3 fica silencioso e o estado de abertura registra formato não suportado.
- Recompilado `build-static` Release /MT. Testes `test_duck_avi`, `test_menu_phase` e `test_special_phase` aprovados; teste com AVI real verifica DK3, 44100 Hz, estéreo e ausência de PCM inventado.
- Ainda pendentes: FFmpeg estático mínimo, vídeo TM1 fiel, reprodução de áudio e sincronização, comparação audiovisual com Ymir. Os testes não comprovam fidelidade de imagem.

### 2026-11-08 — biblioteca nativa de codecs em preparação

- Baixado FFmpeg 7.1.1 oficial (`ffmpeg-7.1.1.tar.xz`, SHA-256 `733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1`) e compilado no MSVC x64 em `build-static/ffmpeg-min`, com `--disable-everything --disable-programs --disable-doc --disable-autodetect --disable-network --disable-x86asm --disable-shared --enable-static --enable-demuxer=avi --enable-decoder=truemotion1,adpcm_ima_dk3` e bibliotecas de conversão habilitadas.
- A configuração informa LGPL 2.1 ou posterior. Verificação de diretivas de `truemotion1.o`: `LIBCMT`, runtime estático. O vínculo, a conformidade de distribuição LGPL e os testes do port ainda precisam ser concluídos.
- Essa etapa não altera o decodificador usado pelo jogo: listras, áudio DK3 e validação no Ymir continuam pendentes. Não há novo executável nesta etapa.

### 2026-11-08 — decodificação nativa FFmpeg e reprodução inicial

- Integrados ao `build-static` os arquivos estáticos FFmpeg 7.1.1 com TrueMotion1 e IMA DK3 (`MB_FFMPEG_ROOT`); sem processo externo de FFmpeg. O leitor mantém os quadros em vetor dinâmico, sem o limite anterior de 2.048, e decodifica DK3 para PCM S16 estéreo em buffer crescente.
- O menu compõe quadros do AVI e o host Windows encaminha o PCM completo para waveOut, com interrupção na saída do menu ou troca de fase. A apresentação inicial usa 1 quadro a cada 4 ticks do host; outros modos e a sincronização exata continuam a validar. O aspecto de 2:1 é inferido para quadros estreitos; precisa ser substituído pelos metadados do fluxo.
- Comparação independente: quadro 30 de MENU e quadro 63 de LOSE, convertidos e posicionados em 320×224, idênticos byte a byte às saídas do FFmpeg de linha de comando. PCM completo de MENU idêntico byte a byte (36.298.032 bytes). CTest: 41/41. Esses testes não comprovam o sequenciamento e a sincronização no jogo.
- `mrbones.exe` Release recompilado com FFmpeg estático; inspeção PE mostra DLLs Windows USER32, GDI32, WINMM, bcrypt e KERNEL32. A instalação de FFmpeg 7.1.1 configurada informa LGPL 2.1 ou posterior; assegurar requisitos de redistribuição estática antes de distribuir.
- Pendente: comparação audiovisual com o Ymir, sequência original de abertura, sincronização e cancelamento reais, tempos de LOSE e outras cenas, testes de erros e compatibilidade, além de eliminar o fallback aproximado de builds sem FFmpeg. A presença do decoder não prova a correção visual da tela de derrota completa nem fidelidade do menu.

### 2026-11-08 — observação do original e apresentação de LOSE

- Ymir 0.4.1 com perfil local e disco 1: foram observados SEGA, Amoebae e Angel Studios, a sequência no cemitério e, posteriormente, o menu com logotipo de ossos, céu estrelado e opções START/OPTIONS. A interface atual do port ainda difere desse menu; a ordem de exibição de todas as telas e a interação não foram comparadas quadro a quadro.
- Os três caminhos de vídeo em fases especiais passaram de um quadro a cada dois ticks para um a cada quatro ticks, aproximação de 15 fps no host. Removidos o nome da fase e a barra de progresso sintéticos sobre vídeos PRESENT, inclusive LOSE. O menu passa ao estado de seleção quando o AVI termina.
- A proporção dos quadros agora usa `sample_aspect_ratio` informado pelo decoder. Após recompilação estática, quadros MENU 30 e LOSE 63 permanecem iguais byte a byte às referências FFmpeg; testes `test_special_phase`, `test_duck_avi` e `test_menu_phase` passaram. Novo teste confere que o quadro de LOSE não recebe sobreposição de diagnóstico e que avança apenas no quarto tick.
- Pendente: comparar a tela de derrota e o áudio em execução no Ymir, reconstruir os gráficos originais do menu, medir sincronização por relógio real e validar encerramento/interrupção. As taxas baseadas em tick são aproximações, não uma confirmação de sincronismo.

### 2026-11-08 — duração e som de LOSE

- Identificados 203 intervalos de vídeo no AVI de LOSE, sendo os 97 últimos blocos `00dc` vazios: representam a manutenção do último quadro, não descarte de tempo. O leitor preserva esses intervalos e mantém a imagem; a sequência passa de 106 para 203 quadros (~13,53 s a 15 fps), próxima dos ~13,51 s do PCM estéreo decodificado.
- O áudio completo de LOSE (2.383.040 bytes PCM) coincide byte a byte com a saída FFmpeg de referência. Evitada a reprodução simultânea de CD-DA nas cenas PRESENT com áudio embutido e removido o disparo de efeito de seleção em cada quadro do vídeo.
- O leitor agora obtém a duração do quadro do `avih` aninhado; o host usa tempo decorrido do relógio monotônico do Windows para avançar os quadros, com limitação de 250 ms por atualização. Pausa e sincronismo em situações de atraso ainda necessitam validação audiovisual.
- Recompilado Release /MT com FFmpeg estático; CTest 41/41. Os testes verificam 203 quadros em LOSE, preservação do quadro durante os 97 intervalos vazios e comparação do PCM. O Ymir alcançou o menu original; não foi obtida a tela LOSE, e a comparação em tempo real de áudio/vídeo permanece pendente.

### 2026-11-08 — executável correto e retorno de LOSE

- `Jogar_MrBones.bat` apontava para `build/mrbones.exe` antigo (14:11), sem os codecs integrados; agora usa `build-static/mrbones.exe`, mantendo `build` como diretório de trabalho para preservar a localização anterior dos dados de usuário.
- Em LOSE, manter o direcional pressionado não acelera mais o contador de páginas nem interrompe o vídeo antes do fim; removido o limite de 240 páginas para LOSE. Um teste cobre os 203 intervalos mesmo com direcional mantido.
- Corrigido ciclo LOSE→LOSE: ao finalizar a cena, `stage_progress` retorna ao menu. Verificado na janela do port iniciado diretamente em `/28LOSE`: foi observada uma imagem de caveira sem as listras da captura anterior e, após a cena, o estado `MENU START` com `PROGRESS LOSE -> MENU`.
- Recompilação Release estática concluída; CTest 41/41. A janela posterior usa o menu genérico, que difere do menu observado no Ymir. Ainda não foi observada LOSE no Ymir nem validada a sincronização por captura do áudio.

### 2026-11-08 — investigação dos recursos do menu e tempo decorrido

- Extraído `/03MENU/04STXT` do disco e inventariados 113 registros. Os registros 108–111 (offsets `0x24810`, `0x24ba0`, `0x24f30`, `0x25420`) contêm as imagens originais START/OPTIONS em variantes; os registros anteriores incluem nomes de fases, como GRAVE ESCAPE e BLIND MAN'S CABIN. O seletor atual escolhe arbitrariamente os oito primeiros candidatos aceitos, explicando a placa de fase incorreta no menu. Ainda não foi identificada com segurança a paleta original ou a fonte do logotipo; os resultados não foram aplicados como solução definitiva.
- Removido o teto artificial de 250 ms da medição do tempo decorrido: a atualização do vídeo pode recuperar quadros após atrasos da janela. O relógio é reiniciado depois de preparar o áudio PCM para não contar o tempo de cópia do buffer como reprodução. Isso reduz uma fonte de deriva, mas não substitui a comparação com o relógio do dispositivo de áudio.
- Acrescentado teste de acúmulo de tempo que cobre processamento de múltiplos quadros e preservação do tempo residual. Compilação estática e testes `test_duck_avi`, `test_special_phase`, `test_stage_progress` e `test_menu_phase` passaram (4/4).
- Pendente: reprodução e comparação de LOSE no Ymir, teste auditivo da sincronização e reconstrução do menu original a partir dos recursos e paleta corretos.

### 2026-11-08 — garantia da cadeia de codecs Windows

- Configurações CMake para Windows agora apontam por padrão a `build-static/ffmpeg-min` e falham explicitamente se a instalação estática de FFmpeg estiver ausente, em vez de gerar silenciosamente um executável com o decodificador aproximado. Com FFmpeg, a inicialização não executa mais `duck_tm1_init` e suas tabelas heurísticas; os decoders são os das bibliotecas estáticas.
- A suíte completa passou 41/41. Após fortalecer o teste, `test_duck_avi` confirmou que os decodificadores nativos de vídeo e áudio estão ativos na compilação Windows. Verificados os imports do executável: somente DLLs padrão USER32, GDI32, WINMM, bcrypt e KERNEL32.
- Nova tentativa no Ymir: menu original acessível, mas as entradas sintéticas enviadas pela automação não mudaram a seleção, mesmo após ativar a janela. LOSE e sua comparação audiovisual no Ymir ainda não foram obtidos; a falta dessa evidência impede declarar a restauração completa.

### 2026-11-08 — relógio do dispositivo de áudio

- A thread PCM consulta `waveOutGetPosition` e publica a posição para o host. A progressão do vídeo usa esse relógio enquanto está disponível, aceitando posições em milissegundos, amostras ou bytes; em falha do dispositivo retorna ao relógio monotônico. A pausa da fase sinaliza `waveOutPause`/`waveOutRestart` à thread proprietária do dispositivo.
- Acrescentado `test_movie_audio`: com áudio estéreo silencioso, confirmou progressão, pausa, retomada e fim de 500 ms neste computador. Em ambientes sem dispositivo ou sem posição suportada, o teste sinaliza SKIP. Teste automatizado do relógio não substitui comparação auditiva da cena real.
- Executado CTest completo 42/42 e recompilado Release estático. A tentativa de controlar o menu do Ymir com entradas sintéticas novamente não avançou até LOSE; comparação da cena com o Saturn e comparação auditiva continuam pendentes.

### 2026-11-08 — entrada, relógio e recursos gráficos do menu

- Corrigida a persistência do estado do controle no laço de janela: uma tecla mantida não é mais interpretada como novo acionamento a cada timer. Teste da fase de ritmo confirma que manter START não alterna repetidamente a pausa.
- Corrigida transição do relógio de áudio após falha de `waveOutGetPosition`: o fim do áudio não é anunciado como relógio válido depois de uma falha, evitando que o port aplique novamente toda a duração como avanço de vídeo. O teste do dispositivo de áudio passou neste computador.
- Inventário ISO encontrou `/03MENU/07ACDB` (458 bytes, cabeçalho 320×224), `/03MENU/08ATIL` (29.344 bytes), `/03MENU/09BCDB` (138 bytes) e `/03MENU/10BTIL` (15.056 bytes), candidatos às camadas VDP2 da tela original. O `scrl_map` atual usa amostragem heurística e não demonstra decodificação correta desses arquivos; a correlação com logo/fundo e paletas continua por provar.
- Recompilado Release estático; testes `test_movie_audio`, `test_special_phase` e `test_saturn_pad` aprovados. Nova tentativa de entrada no Ymir, inclusive vários acionamentos, não alterou o menu; LOSE e a comparação audiovisual seguem pendentes.

### 2026-11-08 — comparação de todos os quadros e correção de bordas

- Acrescentado modo opcional no teste de AVI para gerar SHA-256 de cada quadro efetivamente decodificado. Comparados com FFmpeg 5.1.2 independente: os 106 quadros codificados de LOSE agora coincidem integralmente; os 97 intervalos vazios restantes mantêm o último quadro. O PCM de LOSE já havia coincidido integralmente.
- A comparação mostrou corrupção de borda quando LOSE muda de BGR0 para RGB555: a escrita de `sws_scale` podia alcançar bytes fora da largura nominal, e a tela não era limpa quando as dimensões mudavam. O conversor agora utiliza área temporária com margem, limpa o fundo e copia apenas a largura visível; testes cobrem a borda e a mudança de dimensão.
- Na abertura, 3.084 dos 3.085 quadros codificados coincidem com referência FFmpeg com escala fixa. O último muda de 152×176 para 128×160; a comparação de escala fixa não é aplicável e uma comparação com escala ajustada ainda mostrou diferença em valores próximos do preto. Essa diferença permanece sem conclusão, inclusive quanto ao comportamento original no Saturn.
- Recompilado Release estático; CTest 42/42 e verificação opcional de todos os quadros passaram. LOSE ainda não foi alcançado no Ymir e não houve validação auditiva em tempo real.

### 2026-11-08 — duração do último quadro

- Separado o fim da decodificação do fim da apresentação: o último quadro permanece visível por um intervalo completo antes da transição. As apresentações especiais passam a decodificar o primeiro quadro ao carregar, em vez de iniciá-lo após o primeiro intervalo.
- Acrescentados testes para a retenção final e para a duração nominal de LOSE em relação ao PCM real (diferença menor que 100 ms). `test_movie_audio`, `test_duck_avi`, `test_menu_phase` e `test_special_phase` passaram; executável Release estático recompilado.
- No Ymir, o menu Debug disponibiliza visualizadores de memória e ferramentas VDP/SH-2, mas isso ainda não levou a uma captura de LOSE. Não se comparou a sincronização audiovisual em tempo real com o emulador; a reconstrução do menu original também continua incompleta.

### 2026-11-08 — erros de decodificação durante a cena

- O decoder agora registra falha e encerra a apresentação se um quadro TrueMotion1 não puder ser decodificado. No menu, o áudio pendente/em execução é interrompido e o erro permanece visível no estado; nas cenas especiais, a fase é abortada e o host interrompe o áudio. Falha no primeiro quadro também impede o início normal da cena.
- Um teste injeta quadro inválido em AVI real e verifica erro e encerramento, além dos testes normais dos discos. Compilação Release estática e testes `test_duck_avi`, `test_special_phase`, `test_menu_phase` e `test_movie_audio`: 4/4.
- O menu Debug do Ymir disponibiliza visualizadores de memória, SH-2 e VDP, mas a cena LOSE ainda não foi alcançada nem comparada; a validação audiovisual permanece pendente.

### 2026-11-08 — correlação das camadas originais do menu

- No Ymir 0.4.1 com o disco 1, o depurador mostrou saída 320×224 progressiva, CRAM em modo 2 e duas camadas VDP2 indexadas de 256 cores. A tela observada contém céu estrelado, nuvens, logotipo de ossos e os sprites START e OPTIONS.
- Correlacionados os arquivos originais `/03MENU/08ATIL` e `/03MENU/10BTIL` com a VRAM: os 20 primeiros blocos descomprimidos de ATIL coincidem byte a byte com sequências na VRAM do Ymir. A reconstrução experimental dos 70 blocos de ATIL mostra céu e nuvens; a de BTIL mostra o logotipo. Os cabeçalhos ACDB/BCDB indicam 320×224 e uma grade 10×7 de blocos 32×32. Cada bloco contém 16 descritores de células 8×8; a rotina `scrl_decomp_byte` reproduz os bytes de caracteres examinados. Essa análise ainda não foi integrada à apresentação do port.
- A CRAM capturada permite visualizar as cores: o céu corresponde ao banco de cores 1 e o logotipo ao banco 2; os sprites de seleção usam outro banco. A origem dessas paletas nos dados do jogo ainda não foi identificada com segurança. As imagens e dumps de investigação em `build-static` e `tools/ymir-profile/dumps` são evidências locais, não recursos de execução nem parte do executável.
- A tela LOSE não foi alcançada no Ymir e não houve comparação de áudio em tempo real. Portanto, a fidelidade da abertura, a tela original do menu e a validação audiovisual de LOSE continuam pendentes.
- Verificação nesta etapa: `cmake --build build-static --config Release` informou que o executável já estava atualizado; `ctest --test-dir build-static --output-on-failure` passou 42/42. Não houve nova alteração no código nesta etapa.

### 2026-11-08 — AVI truncado e tamanhos inválidos

- O leitor AVI verifica agora o tamanho declarado do RIFF e os limites dos blocos externos, de `movi` e de listas internas; aritmética de padding usa `size_t`. Cabeçalhos curtos, arquivos truncados e tamanhos de bloco fora do contêiner retornam erro, em vez de aceitar silenciosamente uma cena parcial. O estado registra a causa genérica após liberar recursos.
- Adicionados testes com cabeçalho curto, AVI real de LOSE truncado em um byte e tamanho `0xffffffff` injetado em um bloco; os recursos originais de MENU, LOSE e GUITAR continuam abrindo nos testes. Isso não equivale à validação da reprodução no Ymir.
- Executados `vcvars64.bat`, `cmake --build build-static --config Release` e `ctest --test-dir build-static --output-on-failure`: compilação concluída e 42/42 testes passaram. `dumpbin /dependents build-static/mrbones.exe` lista USER32, GDI32, WINMM, bcrypt e KERNEL32, sem DLL de codec ou runtime de desenvolvimento.
- A paleta da tela original foi localizada na RAM de trabalho do Saturn durante o menu, mas a fonte exata nos arquivos do disco ainda não foi identificada. O menu permanece genérico; LOSE no Ymir e a comparação auditiva ainda não foram obtidos.

### 2026-11-08 — reconstrução visual do menu principal

- Localizada a tabela original de 768 cores em `/03MENU/03CODE`, iniciada no offset `0x5100` pelo índice 256 e terminada em `0x5d04`. Cada cor tem marcador `01` seguido por BGR; as amostras dos bancos 1 e 2 coincidem com os valores observados na RAM do Saturn. A tabela é lida do disco em execução, sem usar os dumps do Ymir no produto.
- Implementada a leitura dos 70 blocos de 32×32 de cada uma das camadas `/03MENU/08ATIL` e `/03MENU/10BTIL`, incluindo descritores que apontam para caracteres de ACDB/BCDB. O menu principal compõe céu, nuvens e logotipo com os bancos corretos, usa os quatro sprites originais de START/OPTIONS e troca os estados ao navegar. A camada do logotipo recebe deslocamento vertical de 19 pixels conforme o VDP2 observado.
- Comparação visual no Ymir 0.4.1: composição, textos, cores e posições principais correspondem qualitativamente ao menu observado. O céu continua estático no port e não foi feita comparação quadro a quadro nem validação do áudio e das transições; as telas posteriores ao menu principal ainda têm reconstruções provisórias. A cena LOSE no Ymir permanece pendente.
- Adicionado teste com os recursos reais para garantir carregamento dos planos e quatro sprites, e alternância reversível da seleção. `cmake --build build-static --config Release` com ambiente MSVC e `ctest --test-dir build-static --output-on-failure`: compilação concluída e 42/42 testes passaram. A captura local do port está em `build-static/menu-port.png`; é evidência produzida a partir dos dados do usuário e não parte da distribuição.

### 2026-11-08 — identificação do vídeo de inicialização

- Após soft reset no Ymir, foram observados o início do Saturn, Amoebae e cenas do cemitério, seguidos por Angel Studios, tela de direitos e menu. Quadros de `/29INTRO/20AVI` incluem Amoebae, cemitério e título, ao passo que `/03MENU/20AVI` é outro vídeo de cerca de 205,8 s. A associação anterior automática do vídeo longo à inicialização estava incorreta.
- `menu_phase_load` agora carrega `/29INTRO/20AVI` e `/29INTRO/19DUCK` para a abertura. O arquivo tem 764 intervalos a 15 fps (~50,93 s), e o áudio DK3 decodificado tem 8.979.728 bytes PCM estéreo a 44.100 Hz. Ao pular ou finalizar a cena, o indicador de áudio pendente é limpo.
- O quadro de índice 90 decodificado no port, após a mesma expansão para 320×176, coincidiu byte a byte com FFmpeg 5.1.2; o áudio PCM inteiro também coincidiu com a referência FFmpeg (SHA-256 `c2b10269f23db928a9f60ed9d5387408aec013455380c9292e3ac6edc3e66461`). Testes verificam o recurso, a duração em intervalos, a identificação do áudio e seu hash. As comparações são com FFmpeg, não uma medição audiovisual sincronizada do Ymir.
- Recompilado `build-static` Release com MSVC e executado `ctest --test-dir build-static --output-on-failure`: 42/42 testes passaram. A ordem exata de todos os trechos, duração percebida, sincronização auditiva, título de direitos no port, ativação posterior do vídeo longo e LOSE no Ymir continuam pendentes.

### 2026-11-08 — comparação integral do vídeo de abertura

- Substituída a cor sintética `(8,6,20)` das bordas e do fundo do quadro por preto, conforme as faixas observadas durante a abertura no Ymir. O teste de borda foi atualizado para cobrir também a mudança de dimensão em LOSE.
- Comparados os 764 quadros de `/29INTRO/20AVI`, após conversão para 320×176 e composição em 320×224, com FFmpeg 5.1.2 independente: 764/764 hashes SHA-256 coincidiram, incluindo 595 quadros BGR0 e 169 RGB555LE. O hash do PCM completo já coincide com o mesmo FFmpeg; os testes exercitam a reprodução e as bordas, mas não comprovam sincronização e fidelidade ao hardware.
- Inspecionada a configuração de entrada do Ymir: o controle Saturn está na porta 1 e START está associado a F/G/H. A automação ainda não avançou a seleção até LOSE. Não houve comparação auditiva em tempo real ou captura da derrota no original.
- Recompilado Release estático com ambiente MSVC e executado `ctest --test-dir build-static --output-on-failure`: 42/42 testes passaram.

### 2026-11-08 — áudio real e dependências de compilação

- Uma gravação loopback da abertura executada no Ymir foi comparada ao PCM de `/29INTRO/20AVI`: trechos de 3 s nos primeiros 18 s apresentaram correlação normalizada de aproximadamente 0,975 a 0,998, após alinhamento temporal. A captura cobre somente o começo e não comprova a sincronização audiovisual completa.
- A primeira captura do executável do port ficou silenciosa. O diagnóstico mostrou que o Ninja registrava zero dependências de cabeçalhos no ambiente MSVC localizado, porque o prefixo de `/showIncludes` fora detectado com codificação incorreta; objetos de `mrbones.c` permaneceram desatualizados após mudanças em `menu_phase.h`. A imagem inicialmente preta, isoladamente, não provou travamento: o vídeo avançava, mas o áudio não iniciava devido ao layout divergente da estrutura.
- Corrigida em CMake a identificação específica do prefixo corrompido em português e reconstruídos todos os objetos. `ninja -t deps` passou a listar 29 cabeçalhos de `mrbones.c`; ao atualizar o timestamp de `menu_phase.h`, o Ninja recompilou tanto o host quanto o módulo e teste do menu. Nova gravação da saída real do port apresentou correlação de aproximadamente 0,998–0,999 em trechos de 2 s entre os primeiros 16 s e o PCM do disco, com alinhamento de ~0,194 s nesta captura. O loopback informou descontinuidades e a medida não determina latência audiovisual.
- O leitor DK3 agora rejeita blocos incompletos em vez de tentar aceitar dados parciais; teste altera o alinhamento da abertura para produzir um bloco truncado. Confirmados os blocos de tamanho múltiplo de 2048 nos AVIs INTRO, MENU e LOSE deste disco.
- Executado build limpo Release estático e, depois da verificação incremental de dependências, `ctest --test-dir build-static --output-on-failure`: 42/42 testes passaram. LOSE no Ymir e a comparação audiovisual completa continuam pendentes.

### 2026-11-08 — transição da abertura e captura prolongada

- Captura loopback adicional do Ymir abrangeu trechos da abertura entre 5 e 37 s. Janelas de 2 s, alinhadas ao PCM do disco, apresentaram correlações de aproximadamente 0,982 a 1,000 nos pontos de 5 a 35 s. A captura não cobre toda a cena e pode conter outras saídas do sistema.
- Captura de 56 s do port recompilado: trechos de 2 s nos pontos de 5 a 40 s apresentaram correlações de 0,979 a 0,999 com o PCM, com atraso alinhado de aproximadamente 171 ms na captura. A fonte decai após ~44 s e fica silenciosa a partir de ~47 s; os valores de correlação em trechos de quase silêncio não são conclusivos. A medição de loopback não mede diretamente o sincronismo entre imagem e som.
- Observada na execução Windows a passagem automática do título para o estado `MENU START`. Acrescentado teste que avança o relógio externo de toda a abertura real, verifica o título após 50 s e a transição ao menu após 51 s, além da limpeza do áudio pendente. Teste não substitui a comparação visual e auditiva completa com o Ymir.
- Compilação Release verificada e suíte `ctest --test-dir build-static --output-on-failure`: 42/42 passaram. Não foi possível operar o controle virtual do Ymir para chegar a LOSE; comparação da derrota permanece aberta.

### 2026-11-08 — teste integrado do relógio de áudio

- Ampliado `test_movie_audio` para carregar a abertura real, enviar seu PCM ao dispositivo `waveOut` e avançar `menu_phase_step` a partir da posição informada pelo dispositivo, como no host Windows. A cada atualização, o quadro é comparado com o índice esperado pelo tempo do áudio. Neste computador o teste efetivamente executou, chegando a 1.203 ms e ao quadro 19; não foi um SKIP.
- O teste cobre o início e a lógica de relógio, mas não mede a apresentação na tela ou a latência acústica. LOSE ainda não foi acessado no Ymir, e suas transições e áudio continuam sem comparação direta.
- Recompilado `build-static` Release e executado `ctest --test-dir build-static --output-on-failure`: 42/42 testes passaram.

### 2026-11-08 — quadro e áudio simultâneos no Ymir

- Gravado loopback de áudio do Ymir a 44.100 Hz durante a abertura e capturada pelo próprio emulador uma imagem F12 na mesma execução (`tools/ymir-profile/screenshots/mr Bones [MK-81016]-20261108T160919.363.png`). A captura do quadro foi solicitada às 1794164959,361 s; o loopback começou às 1794164901,281 s.
- A busca por diferença média nos 764 quadros identificou o quadro 365 de `/29INTRO/20AVI` como melhor correspondência. Em 320×224, a diferença média absoluta de RGB foi 1,68 por canal na escala 0–255, após reduzir a captura do Ymir por vizinho mais próximo; existem diferenças visíveis de cor/bordas e não se conclui identidade pixel a pixel. Comparação local: `build-static/ymir-intro-sync-comparison.png`.
- A correlação de uma janela de áudio de 1 s imediatamente anterior à captura indicou final no instante 24,331 s do PCM, correlação 0,9998. O tempo nominal do quadro 365 a 15 fps é 24,333 s. A diferença estimada é inferior a um quadro neste ponto, mas inclui incerteza dos relógios, buffer e latência do loopback; é uma amostra do Ymir, não uma medida integral da latência do port.
- Verificada a compilação Release estática (`cmake --build build-static --config Release`, sem trabalho pendente). A comparação de LOSE no Ymir, a sincronização apresentada pelo port em outros pontos e as diferenças de imagem permanecem pendentes.

### 2026-11-08 — análise das diferenças de pixels no quadro 365

- Reexaminado o quadro do Ymir e o mesmo quadro decodificado pelo FFmpeg, em 320×224 e após redimensionamento por vizinho mais próximo. Para todos os 35.840 pares horizontais, a diferença entre as imagens segue a forma `(d,d,0)` no pixel par e `(0,0,-d)` no pixel ímpar; 25.341 pares têm `d != 0`, com magnitude máxima 86. A média absoluta global é 1,68 por canal. O padrão é estruturado e não é explicado simplesmente pela ordem RGB/BGR.
- Criada uma sonda local de diagnóstico em `build-static/probe-native-frame.c`: após decodificar o quadro 365 com as bibliotecas estáticas, o `AVFrame` tem 160×176 e 640 bytes por linha. O byte extra do formato nativo observado assume apenas 0 e 128 e não reproduz diretamente o `d` medido. Esses dados não bastam para determinar se a diferença vem do decodificador Saturn, da apresentação VDP ou da captura; não foi aplicada uma correção visual especulativa.
- Conferidas as dependências PE: USER32, GDI32, WINMM, bcrypt e KERNEL32. A comparação visual completa e a referência de LOSE no Ymir permanecem abertas; o executável continua o build estático já testado.

### 2026-11-08 — identificação da diferença de luminância em TrueMotion

- Investigado o algoritmo RGB24H em FFmpeg 7.1.1. A tabela Y codifica dois preditores (`lo` e `hi`) que afetam componentes distintos do pixel de 32 bits. No Saturn, a captura sugere que eles representam luminâncias de dois pixels horizontais; a duplicação convencional de um pixel 160→320 perde essa diferença.
- Criada apenas para diagnóstico uma variante experimental em `build-static/ffmpeg-7.1.1/libavcodec/truemotion1-probe-full.c`. Ela acumula `lo-hi` no byte auxiliar do AVFrame, preservando os canais RGB normais; uma segunda decodificação sem a alteração fornece a base para subtrair o conteúdo original desse byte. A variante foi compilada em sonda separada e não foi vinculada ao executável do port.
- No quadro 365, para cada pixel BGR original e diferença `d` recuperada, a reconstrução `(R+d,G+d,B)` / `(R,G,B-d)`, com aritmética de bytes, coincidiu exatamente com os 168.960 canais RGB da área de vídeo na captura do Ymir, após redução por vizinho mais próximo. A hipótese agora tem evidência direta para esse quadro; ainda precisa ser testada em outras cenas, resoluções, quadros interdependentes e transições antes de integrar e declarar correção.
- O build estático distribuível permanece o anterior. Falta implementar uma integração reproduzível da variante com a biblioteca estática, preservar o caminho de formatos 15-bit, executar comparações adicionais e verificar LOSE no Ymir.

### 2026-11-08 — reconstrução nativa dos pixels TrueMotion do Saturn

- Integrado `src/third_party/truemotion1_saturn.c`, derivado do decoder FFmpeg 7.1.1 (LGPL 2.1 ou posterior). Uma segunda instância nativa acompanha os preditores de luminância; a diferença dos bytes auxiliares permite recuperar os dois pixels horizontais que a expansão anterior duplicava. O produto usa bibliotecas e código nativos estáticos, sem processo externo de FFmpeg nem CPU emulada.
- A reconstrução de 320×224 coincidiu byte a byte com três capturas F12 do Ymir na abertura, correspondentes aos quadros 175, 250 e 365; os hashes foram incorporados ao teste. O quadro 100 RGB555 de LOSE também permaneceu idêntico à referência FFmpeg, cobrindo a troca de formato. A cena LOSE em si ainda não foi capturada no Ymir, portanto sua fidelidade à referência continua aberta.
- `CMakeLists.txt` agora exige a instalação e a árvore configurada correspondentes do FFmpeg 7.1.1, já existentes em `build-static/ffmpeg-min` e `build-static/ffmpeg-7.1.1`. O README registra esses requisitos, o custo de distribuição estática sob LGPL e o estado experimental do projeto. O uso de duas instâncias do decoder aumenta o trabalho de decodificação; não foi feito benchmark em máquinas lentas.
- Compilado `build-static` Release /MT e executado `ctest --test-dir build-static --output-on-failure`: 42/42 passaram. `dumpbin /dependents` lista apenas USER32, GDI32, WINMM, bcrypt e KERNEL32. A validação completa de sincronização em tempo real e a comparação de LOSE no Ymir seguem pendentes.

### 2026-11-08 — referência direta de LOSE

- No Ymir 0.4.1, com o disco 1, um breakpoint em `0020B95C` interrompeu `LoadFirstLevel` depois da leitura do backup. Alterado temporariamente em RAM o próximo nível em `00228970` de 0 para `0x1c` (diretório 3), sem alterar o disco, para executar `/28LOSE` diretamente. A cena mostrou o personagem e depois retornou ao menu; esta intervenção não testa o percurso normal de derrota.
- Captura F12 `tools/ymir-profile/screenshots/mr Bones [MK-81016]-20261108T163030.278.png`: reduzida de 640×448 a 320×224 por vizinho mais próximo e convertida a RGBA, seu SHA-256 é `8ca7079c8f4176deb7d4c55916dccdc93af3c8947e3be7b3c9fad05f3f580db1`. Coincide byte a byte com o quadro 39 (índice zero, excluindo quadros vazios) de LOSE decodificado pelo port. A captura e o quadro preservam proporção, cores e imagem, sem as listras. A comparação cobre um momento, não a sequência inteira.
- Incorporado esse hash ao `test_duck_avi`. Executados `cmake --build build-static --config Release` e `ctest --test-dir build-static --output-on-failure` no ambiente MSVC x64: 42/42 passaram. Executável nativo estático previamente reconstruído permanece `build-static/mrbones.exe`.
- Pendente: captura e comparação do áudio de LOSE no Ymir, sincronização durante toda a cena, transição de derrota pela jogabilidade e comparação integral dos vídeos e transições. O teste de acesso forçado não prova esses itens.

### 2026-11-08 — áudio e sincronização de LOSE

- Repetido o acesso direto a `/28LOSE` no Ymir pelo breakpoint `0020B95C` e alteração temporária da RAM, com captura loopback a 44.100 Hz estéreo em `build-static/ymir-lose-loopback.wav`. O áudio do vídeo foi alinhado ao PCM decodificado do disco: janelas de 1 s iniciadas em 0, 2, 4, 6, 8, 10 e 12 s apresentaram correlações de 0,9980 a 0,9998 no canal esquerdo. O início alinhado ficou em 16,029 s na captura; o PCM tem 13,509 s. O loopback está sujeito à latência e às demais saídas do sistema.
- Durante a mesma gravação, a captura F12 `tools/ymir-profile/screenshots/mr Bones [MK-81016]-20261108T163317.051.png` coincidiu byte a byte, após redução de 640×448 para 320×224, com o quadro 41 decodificado do port. Pela hora de início informada pelo gravador (1794166378,277 s), pelo instante de captura (1794166397,051 s) e pelo alinhamento do áudio, a captura situa-se em ~2,745 s da fonte; o quadro 41 a 15 fps situa-se em ~2,733 s. A diferença estimada de 12 ms é inferior a um quadro, com incertezas de agendamento, relógio e latência, e cobre um único ponto do Ymir.
- Executado o port real com `--level /28LOSE`, enquanto o Ymir estava pausado; gravada sua saída em `build-static/port-lose-loopback.wav`. As mesmas janelas da fonte apresentaram correlação arredondada a 1,0000 no canal esquerdo; o início capturado ficou em ~0,161 s. A captura confirma saída de áudio efetiva e continuidade nos pontos medidos, mas não mede latência de apresentação audiovisual do port. Os scripts locais utilizados estão em `build-static/capture-lose.py`, `build-static/capture-port-lose.py` e `build-static/compare-lose.py`.
- Reexecutado `cmake --build build-static --config Release` no ambiente MSVC x64; Ninja informou que o build já estava atualizado. `dumpbin /dependents build-static/mrbones.exe` lista USER32, GDI32, WINMM, bcrypt e KERNEL32, sem DLL de codec ou runtime de desenvolvimento. Suíte anterior após a última alteração: 42/42 testes.
- Não foram comparados os dois canais separadamente, a sincronização do port em todos os quadros, o percurso normal de derrota e todas as transições do menu. O acionamento direto da cena não comprova esses aspectos.

### 2026-11-08 — estéreo e relógio de LOSE

- Reanalisadas as gravações loopback de LOSE por canal e janelas de 1 s iniciadas em 0, 2, 4, 6, 8, 10 e 12 s da fonte. No Ymir, as correlações foram 0,997805–0,999827; no port, 0,999964–0,999994. Os dois canais de cada gravação tiveram o mesmo alinhamento temporal nas janelas medidas. Na última meia segunda do PCM, as correlações foram 0,9763/0,9866 no Ymir e 0,99999/0,99999 no port. O loopback e a busca de alinhamento não demonstram igualdade bit a bit nem fidelidade acústica absoluta.
- `tests/test_movie_audio.c` agora exercita LOSE real com `special_phase_step` e o relógio do dispositivo `waveOut`, além do teste da abertura. Neste computador, o teste chegou a 1.209 ms e ao quadro 19 esperado, sem SKIP. A verificação mede o avanço por relógio no começo, não a latência efetiva de exibição ou a cena inteira.
- `cmake --build build-static --config Release` concluiu; `ctest --test-dir build-static --output-on-failure` passou 42/42. O executável estático permanece atualizado; a alteração desta etapa atinge apenas o teste.
- Continuam pendentes a validação audiovisual integral no port, a transição de derrota por jogabilidade normal e as demais transições do menu.

### 2026-11-08 — LOSE até o término pelo relógio real

- Ampliado `test_movie_audio` para executar LOSE inteiro com o relógio do dispositivo `waveOut`, verificando durante a reprodução a correspondência entre posição de áudio e índice esperado do vídeo, incluindo a passagem ao relógio de parede quando o áudio termina. Neste computador, sem SKIP, o teste chegou a 13.509 ms, percorreu os 203 intervalos e atingiu `SPECIAL_ST_LOST`.
- Recompilado o build Release e executada a suíte completa: 42/42 testes passaram. O teste não captura a imagem apresentada no monitor e não substitui a comparação da transição por uma partida normal no Ymir.

### 2026-11-08 — retorno ao menu na execução Windows

- Executado `build-static/mrbones.exe` com o disco 1, manifesto, `--level /28LOSE` e diretório de salvamento de teste. Depois do término da apresentação, a janela mostrou `MENU START` com os recursos do menu; o processo registrou `PROGRESS LOSE -> MENU`. O Ymir também retornou ao menu depois de LOSE selecionado pelo depurador. A comparação confirma o destino observado nas duas execuções, sem validar o momento exato ou o percurso normal de derrota.
- A execução de teste terminou por Escape. Não houve alteração no executável; permanecem válidos o build Release e os 42/42 testes registrados na etapa anterior.

### 2026-11-08 — música do menu e continuidade CD-DA

- A gravação do Ymir após LOSE apresentou conteúdo correspondente à faixa 02 do disco 1 (correlação 0,833 em janela de 0,5 s, incluindo a região anterior ao índice 01). O port deixava o menu silencioso. `mrbones.c` agora inicia a faixa 02 ao entrar no menu, encerra-a ao desativar música e interrompe-a ao entrar em uma fase; a seleção e as transições completas do original continuam a investigar.
- A primeira medição no port revelou atraso crescente no CD-DA: janelas separadas por 5 s da captura correspondiam a cerca de 4,3 s da faixa. `host_audio.c` aguardava cada buffer terminar antes de preparar o seguinte. A reprodução agora mantém dois buffers enfileirados e drena o trecho final antes de liberar os recursos.
- Nova captura do executável após LOSE: janelas de 0,5 s nos tempos 15, 17, 19 e 20 s da captura corresponderam, respectivamente, aos tempos 3,2765, 5,2765, 7,2765 e 8,2765 s da faixa 02, com correlação arredondada a 1,00000 no canal esquerdo. O loopback reportou descontinuidades; a comparação cobre esses pontos, não o ciclo completo ou todos os canais. Não foi adicionada regressão automatizada específica do fluxo CD-DA; a captura e comparação foram feitas na execução real.
- Recompilados `mrbones.exe`, `mbrunway.exe` e o teste de áudio em Release /MT. `ctest --test-dir build-static --output-on-failure` passou 42/42; `dumpbin /dependents build-static/mrbones.exe` listou USER32, GDI32, WINMM, bcrypt e KERNEL32.

### 2026-11-08 — confirmação adicional da música do menu

- Gravada nova amostra do menu do Ymir após retomar a emulação (`build-static/ymir-menu-loopback.wav`). Janelas de 0,5 s aos 20, 22 e 23 s da captura corresponderam aos tempos 93,7939, 95,7939 e 96,7939 s da faixa 02 do disco, com correlações no canal esquerdo de 0,7645 a 0,7708. A sequência temporal confirma a identificação da faixa; a diferença de forma de onda em relação ao PCM direto permanece sem explicação e impede declarar identidade sonora.
- Tentado pular a abertura no executável por teclado automatizado enquanto se gravava áudio. A janela permaneceu no estado TITLE durante as tentativas; não foi possível concluir se o problema está na injeção de teclas ou no tratamento da entrada. O teste unitário já cobre a transição de estado, mas o salto e a interrupção do áudio em uma interação real permanecem sem validação nesta tentativa.
- Nenhum código do port foi alterado nesta etapa; permanece o build estático Release anterior, com 42/42 testes.

### 2026-11-08 — mistura dos canais da música do menu

- Analisadas as janelas do Ymir antes comparadas isoladamente com o canal esquerdo da faixa 02. Um ajuste linear aos dois canais do CD mostrou que os dois canais capturados são aproximadamente a soma de esquerdo e direito: coeficientes próximos de 0,125 em cada entrada e saída, com correlação ajustada de 0,974–0,997. A comparação do áudio LOSE na mesma configuração indicou ganho geral de ~0,335 por canal no Ymir, contra ~0,419 no port; portanto, a mistura observada no menu corresponde aproximadamente a `(L+R)*3/8` após normalizar esse volume geral. Esta é uma inferência de capturas, não uma reconstrução confirmada dos registradores SCSP.
- Criada a mistura somente para a música do menu, sem mudar o estéreo das demais faixas ou do vídeo. Acrescentado teste de amostras positivas, negativas e de cancelamento em `test_cdda`. A captura final da saída do executável com a nova mistura permanece pendente; outra aplicação estava reproduzindo áudio durante esta etapa e a gravação loopback incluiria essa fonte.
- Compilado o Release estático e conferidas as dependências PE: USER32, GDI32, WINMM, bcrypt e KERNEL32. Na suíte, 41/42 passaram e `test_movie_audio` falhou sem diagnóstico após a abertura; repetido isoladamente, passou com LOSE até 13.509 ms e 203 quadros. Não se estabeleceu a causa da falha intermitente.

### 2026-11-08 — confirmação da mistura e repetição dos testes

- Aplicada matematicamente a mistura `(L+R)*3/8` às amostras originais da faixa 02 e comparada com os dois canais da captura do menu no Ymir, após alinhamento: correlações esquerda/direita de 0,97428/0,97715, 0,99649/0,99649 e 0,99631/0,99631 nas janelas de 0,5 s aos 20, 22 e 23 s da gravação. Os ganhos medidos para as duas últimas janelas foram ~0,336, compatíveis com o ganho geral medido no Ymir; a primeira janela apresenta maior diferença. Esta comparação usa a fórmula sobre a fonte e não a saída capturada do novo executável.
- Adicionadas mensagens de diagnóstico ao teste de sincronização de LOSE para detectar regressão do relógio ou falha de decodificação. A execução interrompida pela pausa do usuário terminou em segundo plano: o log registra duas repetições aprovadas. Depois, `ctest --test-dir build-static --output-on-failure` passou 42/42. A causa da falha intermitente anterior ainda não foi reproduzida.

### 2026-11-08 — captura da mistura efetiva no executável

- Executado o Release Windows com `--level /28LOSE` e gravada a saída loopback após a transição para o menu em `build-static/port-menu-mixed-loopback.wav`. Comparadas janelas de 0,5 s aos 15, 17, 19 e 20 s da captura com a mistura `(L+R)*3/8` da faixa 02: os tempos correspondentes na faixa foram 3,1785, 5,1785, 7,1785 e 8,1785 s. As correlações arredondadas a seis casas decimais foram 1,000000 nos dois canais e o ganho relativo ficou entre 0,99898 e 0,99983; isso comprova a aplicação e a continuidade da mistura nos pontos medidos.
- A captura do Ymir foi obtida em outros trechos da faixa; o ajuste dessa mesma fórmula a três janelas anteriores do Ymir foi de 0,974–0,997. Não se trata de uma comparação de duas capturas do mesmo trecho nem prova de identidade durante toda a música ou da configuração exata do hardware.
- `cmake --build build-static --config Release` confirmou que o executável está atualizado. `dumpbin /dependents` novamente listou apenas USER32, GDI32, WINMM, bcrypt e KERNEL32; a última suíte completa passou 42/42. O salto da abertura em interação real e a transição normal pela jogabilidade ainda não foram confirmados.

### 2026-11-08 — entrada curta e salto da abertura

- A entrada era lida apenas por `GetAsyncKeyState` a cada timer de ~16 ms, portanto uma pressão e liberação completas entre duas leituras podiam ser perdidas. O host integrado agora guarda eventos `WM_KEYDOWN` não repetidos até o próximo passo e combina os botões com o estado mantido do teclado. `host_input_key_button` e o teste de mapeamento cobrem teclas válidas e desconhecidas; as demais ferramentas continuam usando sua leitura anterior.
- No executável Windows recompilado, uma pressão automatizada de Enter durante `MENU TITLE` produziu `MENU START` observado na janela. Na gravação loopback `build-static/port-skip-loopback.wav`, janelas aos 25, 27 e 29 s corresponderam à música de menu nos tempos 2,9071, 4,9071 e 6,9071 s da faixa, com correlação arredondada a 1,00000 no canal esquerdo. Nos trechos medidos a música tocou sem contribuição significativa detectável do áudio da abertura; a latência exata do corte não foi medida.
- Build Release recompilado; `ctest --test-dir build-static --output-on-failure` passou 42/42. O teste interativo verificou o salto no port, não o comando e a temporização equivalentes no Ymir.

### 2026-11-08 — tentativa de salto no Ymir

- Iniciado Ymir 0.4.1 com o disco 1 e `tools/ymir-profile`; observado o vídeo original da abertura. A tentativa de enviar Start (`F`, conforme `Input.Port1.ControlPad.Binds`) coincidiu com a abertura de um painel de configurações e a ferramenta de controle detectou entrada do usuário na janela. Não houve observação inequívoca de salto nem medida de tempo; a comparação do comando no original continua pendente.
- Nenhuma alteração foi feita no executável nesta tentativa. O último build Release e a suíte 42/42 permanecem os registrados na seção anterior.

### 2026-11-08 — comparação adicional dos quadros de LOSE

- Gerada referência independente com `ffmpeg -v error -i build-static/lose_reference.avi -map 0:v:0 -vf "format=rgba,pad=320:224:0:8:black" -vsync 0 -f framehash -hash sha256 -y build-static/lose-reference-current.hash`. A referência e o port contêm 106 quadros não vazios. Os índices 94–105 coincidiram byte a byte; todos representam o mesmo quadro final mantido. Os índices 0–93 diferem do decoder padrão do FFmpeg.
- A divergência não é prova de erro do port: duas capturas independentes do Ymir, registradas nas etapas anteriores, coincidiram byte a byte com os quadros 39 e 41 do port, e a reconstrução nativa foi feita para recuperar luminâncias horizontais que o decoder padrão não fornece. A amostra ainda não valida os demais quadros visualmente no Ymir nem a transição normal pela jogabilidade.
- Não houve alteração de código ou executável nesta etapa; permanecem o build estático Release e a suíte 42/42 da última recompilação.

### 2026-11-08 — imagem de referência fornecida pelo usuário

- O usuário informou que a versão emulada funciona e apresentou uma captura da cena LOSE com o esqueleto sobre fundo preto. A imagem é visualmente consistente com a cena observada no Ymir e com o quadro 41 anteriormente identificado na captura do emulador; não foi estabelecida correspondência pixel a pixel com a imagem enviada, pois ela tem outra dimensão e enquadramento.
- A captura isolada não comprova áudio, temporização ou o caminho de entrada na cena. Permanecem aplicáveis as evidências e limitações das comparações anteriores; nenhum arquivo de dados do jogo foi incluído no executável.

### 2026-11-08 — alinhamento da imagem enviada com o port

- Corrigida a identificação anterior: a imagem enviada pelo usuário se alinha melhor ao quadro **48**, não ao quadro 41, da sequência LOSE. O quadro 48 foi extraído do decoder atual por `MB_DUCK_LOSE_48_OUT` no `test_duck_avi`; após alinhamento afim estimado por características da imagem, 69 de 73 correspondências foram consideradas consistentes. Com redimensionamento por vizinho mais próximo, a diferença média absoluta foi 0,80 por canal em toda a captura e 6,12 por canal nas regiões não pretas, numa escala de 0–255. A diferença inclui recorte, escala, posicionamento, arredondamento e diferenças de captura; não é igualdade pixel a pixel.
- Acrescentada opção de diagnóstico ao teste para salvar esse quadro, sem mudar o executável do jogo. `cmake --build build-static --config Release` recompilou o teste e `ctest --test-dir build-static -R '^test_duck_avi$' --output-on-failure` passou. O build estático do jogo permanece atualizado; a suíte completa anterior passou 42/42.

### 2026-11-08 — relógio de áudio durante toda a abertura

- Ampliado `test_movie_audio` para avançar a abertura inteira pelo relógio real do dispositivo `waveOut`, verificando o índice de vídeo esperado em cada atualização e a passagem ao relógio de parede após o fim do áudio, até `MENU_SCR_START`. A execução nesta máquina não fez SKIP: áudio chegou a 50.905 ms, o vídeo percorreu 764 intervalos e o menu foi alcançado; em seguida LOSE terminou com 13.509 ms e 203 intervalos.
- `cmake --build build-static --config Release` recompilou o teste e `ctest --test-dir build-static -R '^test_movie_audio$' -V` passou em 65,43 s. A suíte completa anterior passou 42/42; não foi repetida nesta etapa, que alterou apenas o teste. O relógio interno não mede a latência visual real nem valida todos os quadros contra o Ymir.

### 2026-11-08 — erros de vídeo obrigatório

- A abertura agora recusa `/29INTRO/20AVI` ausente, ilegível ou inválido e preserva uma mensagem de erro; um quadro inválido durante a reprodução encerra a abertura com tela preta e status descritivo, sem cair na apresentação genérica. O host registra o motivo de falha de carga. O teste do menu injeta um quadro inválido após a carga e verifica estado, mensagem e fundo.
- Para LOSE, MOVIE e INTRO, o carregador de fase exige que `special_phase` abra o vídeo e já não recorre ao modo de diagnóstico sintético quando a carga falha. Falhas de codec TrueMotion1 ou DK3 indisponível no build FFmpeg também são informadas ao abrir o AVI. Não foi criada nesta etapa uma imagem de disco corrompida para testar falha inicial de LOSE ponta a ponta.
- Recompilado `build-static` Release /MT. A suíte completa passou 42/42 antes da última alteração restrita ao diagnóstico de codec; depois dela, os cinco testes diretamente relacionados a menu, fases e AVI passaram 5/5. `dumpbin /dependents` lista USER32, GDI32, WINMM, bcrypt e KERNEL32. As comparações no Ymir registradas nas etapas anteriores permanecem parciais.

### 2026-11-08 — teste de LOSE corrompido

- Criada em `build-static` uma cópia temporária do arquivo de dados do disco 1. `cd_iso_lookup` localizou `/28LOSE/20AVI` em LBA 139865; somente na cópia, os quatro bytes `RIFF` em offset bruto 328962496 foram substituídos por `FAIL`. O original foi verificado antes e depois e permaneceu com `RIFF`.
- `stage_runner_begin` rejeitou a cópia (`result=-1`, estado FAILED, `special_live=0`, `STAGE LOSE load failed on disc 1`). O executável Release foi executado com a mesma cópia e `--level /28LOSE`: imprimiu `stage /28LOSE load failed` e retornou código 1, sem abrir uma cena genérica. A cópia temporária foi removida após a verificação.
- Acrescentado ao `test_stage_runner` um teste opcional ativado por `MB_DISC1_CORRUPT_LOSE`, para repetir a rejeição quando uma cópia corrompida for fornecida. Com a cópia de teste, `ctest --test-dir build-static -R '^test_stage_runner$' --output-on-failure` passou. A comparação de todos os quadros com o Ymir e a derrota por jogo normal continuam pendentes.

### 2026-11-08 — teste da abertura corrompida

- Localizado o início de `/29INTRO/20AVI` no offset bruto 330599488 do arquivo de dados do disco 1. Criada cópia temporária em `build-static` e alterados apenas nela os quatro bytes `RIFF` para `FAIL`; o original foi conferido antes e depois e permaneceu intacto.
- O executável Release, executado com a cópia e diretório de salvamento de teste, retornou código 1 e informou `MENU load failed: INTRO/20AVI: duck_avi: not RIFF/AVI`, sem iniciar apresentação substituta. Acrescentado teste opcional em `test_menu_phase` ativado por `MB_DISC1_CORRUPT_INTRO`; a execução com a cópia passou. A cópia temporária foi removida.
- `cmake --build build-static --config Release` atualizou somente o teste. A suíte completa 42/42 é anterior à última alteração de diagnóstico de codec; os cinco testes diretamente relacionados e este teste adicional passaram depois dela. A comparação da transição de derrota por jogo normal e dos demais quadros com o Ymir continua pendente.

### 2026-11-08 — decodificação além de 2.048 quadros

- Decodificado sequencialmente o AVI `/03MENU/20AVI` inteiro no decoder atual: 3.085 quadros não vazios, incluindo os posteriores ao antigo limite de 2.048. A lista de hashes da execução está em `build-static/menu-full-current.sha256`.
- `test_duck_avi` agora percorre esse vídeo mesmo sem variável de diagnóstico, verifica a quantidade total e compara hashes dos quadros não vazios de índices 2.048 e 3.084; em seguida reabre o recurso para os demais testes. `ctest --test-dir build-static -R '^test_duck_avi$' --output-on-failure` passou. Os hashes fixam uma regressão da saída atual; não são uma comparação visual com o Ymir.
- O build Release atualizou somente o teste; o executável estático do jogo permanece atualizado. O vídeo longo é distinto da abertura `/29INTRO/20AVI` e sua ativação no fluxo original ainda precisa de confirmação.

### 2026-11-08 — inspeção do vídeo longo do menu

- Extraídos para inspeção local quadros do AVI `/03MENU/20AVI` com FFmpeg (`build-static/menu-probe450.png`, `menu-probe900.png`, `menu-probe1800.png`, `menu-probe2700.png`). O quadro 450 mostra um personagem junto a trilhos e o 1800 mostra esqueletos de olhos vermelhos; trata-se de uma sequência narrativa distinta da abertura de `/29INTRO`.
- O laço descompilado `Brain_CMenuSelector` em `docs/ghidra-menu.txt` trata direções e seleção em um laço contínuo; a inspeção superficial não identifica uma condição de inatividade para acionar o vídeo, mas chamadas indiretas ainda não foram resolvidas. Portanto, não há evidência suficiente para definir automaticamente o ponto de reprodução no port.
- Nenhum código ou executável foi alterado nesta inspeção. A rota de entrada dessa sequência e sua comparação audiovisual com o Ymir continuam pendentes.

### 2026-11-08 — vídeo narrativo antes de RUNWAY

- Correlacionados `MENU/A.MAP`, `/03MENU/03CODE` carregado em `0x232000` e o carregador no Ghidra. `StartScreen` em `0x23e0f4` aponta para `StartGameEntry` em `0x23dfe4`, cujos primeiros campos são `1, 1, 3, 4, 3`. Em `SelectMenuEntry` (`0x23cbc4`), o caso 1 no disco 1 passa os campos `+0x10` e `+0x0c` a `LoadNewDuckMovie` (`0x239ae8`): respectivamente 3 e 4. `CEndLevel::Brain` (`0x2399cc`) encaminha os campos a `PlayNewDuckMovie` (`0x20b714`); este reproduz o filme do primeiro diretório e chama o carregador com o segundo. A tabela de diretórios identifica 3 como MENU e 4 como RUNWAY. Descompilações: `docs/ghidra-menu-movie-flow.txt` e `docs/ghidra-loader-movie-flow.txt`.
- Integrada ao port a reprodução de `/03MENU/20AVI` e seu áudio DK3 após selecionar Start Game, antes de RUNWAY. O áudio CD-DA do menu é interrompido e o relógio do vídeo usa a posição do áudio; Enter permite pular para a fase. Vídeo ausente ou inválido gera erro. O ponto de reprodução é sustentado pelos mapas e binários; a temporização e o comando de salto ainda não foram comparados no Ymir.
- Os testes verificam tanto o salto quanto a passagem natural após toda a duração simulada de 206 s, sem limite de 90 ou 2.048 quadros. Na execução Windows, observados MENU TITLE → MENU START → MENU STORY, quadros da sequência narrativa e, após Enter, RUNWAY. A tela de RUNWAY apresentou cenário e cores ainda incorretos; esta observação não valida a fase jogável.
- Recompilado `build-static` Release /MT e executada a suíte completa: 42/42 passaram. `dumpbin /dependents` lista USER32, GDI32, WINMM, bcrypt e KERNEL32. A sequência narrativa integrada ainda requer captura audiovisual comparativa no Ymir e não foi medida a latência de tela do port.

### 2026-11-08 — comparação audiovisual do vídeo narrativo

- No Ymir 0.4.1, o carregador original foi interrompido em `0x0020B95C` e os parâmetros temporários em RAM foram ajustados para executar o vídeo do diretório 3 e depois carregar o diretório 4. Esse acesso forçado permitiu comparar o filme, mas não substitui a validação do percurso normal pelos menus nem altera os dados dos discos.
- Três capturas nativas do Ymir (`tools/ymir-profile/screenshots`, horários 20:22:07.587, 20:22:18.257 e 20:22:28.914) foram reduzidas de 640×448 para 320×224 por vizinho mais próximo. Os hashes RGBA coincidiram integralmente com os quadros não vazios 72, 232 e 392 de `build-static/menu-full-current.sha256`. Entre o primeiro e o terceiro decorreram 21,327 s para 320 quadros, compatível com 15 quadros/s. Não se trata de comparação de todos os quadros ou da latência real do port.
- Registrados 55 s de loopback do Ymir em `build-static/ymir-story-loopback.wav`. As janelas de um segundo começando em 3, 8, 15, 25 e 35 s do PCM `build-static/menu_audio.pcm` produziram correlações normalizadas, no canal esquerdo, de 0,9999, 0,9999, 0,9999, 0,9983 e 0,9986; offsets da captura 17,4568, 22,4567, 29,4565, 39,4563 e 49,4561 s. Isso verifica trechos do sinal e sua cadência, mas não mede a sincronização ponta a ponta no executável nem valida o segundo canal nesta captura.
- `cmake --build build-static --config Release` confirmou que a compilação estava atualizada (`ninja: no work to do`). A suíte completa mais recente segue em 42/42, conforme a seção anterior. A transição normal, o comando de salto no Ymir, o restante do vídeo e os problemas visuais de RUNWAY continuam pendentes.

### 2026-11-08 — reinicialização normal e canal direito do filme

- Desativado o breakpoint e executado soft reset no Ymir 0.4.1 com o disco 1, sem alteração de RAM. Observados os logotipos, a abertura do cemitério, a tela de direitos e a chegada automática à tela original com START e OPTIONS. A reprodução continuou apesar das tentativas de enviar Start durante a abertura; não se pode concluir que o jogo ignore o comando porque as teclas simuladas também não alteraram a seleção no menu. A configuração do controle da porta 1 mostrou Start em F/G/H e os botões A/B/C em J/K/L; as tentativas por G e K não produziram transição observável. A seleção pelo controle e o salto no original seguem sem validação.
- Comparado também o canal direito de `ymir-story-loopback.wav` ao PCM de `/03MENU/20AVI` nas mesmas janelas de 1 s: correlações 0,99992, 0,99988, 0,99987, 0,99817 e 0,99879, para os pontos 3, 8, 15, 25 e 35 s. Os deslocamentos coincidem com os do canal esquerdo. São comparações parciais da saída do Ymir, sem medição simultânea de imagem e áudio no port.
- Sem alteração de código nesta etapa; continuam válidos o build Release estático e a última suíte completa 42/42. A abertura da execução nativa ainda não reproduz todas as telas observadas antes do filme e o acesso normal ao vídeo narrativo no Ymir não foi comprovado pela automação de entrada.

### 2026-11-08 — rejeição de áudio AVI truncado

- Corrigido `append_pcm` para rejeitar bloco de áudio não vazio com menos de dois bytes, tanto em `movi` quanto dentro de `LIST rec`; antes, um `01wb` de um byte em `rec` era ignorado. Blocos vazios continuam aceitos. O carregador informa AVI inválido ou truncado, sem apresentar esse bloco como áudio válido.
- Acrescentada regressão em `test_duck_avi`: uma cópia em memória do AVI de LOSE recebe um bloco `01wb` de um byte seguido de `JUNK` com tamanho ajustado, mantendo válidos os limites do contêiner. O arquivo original não é alterado. O parser rejeitou o bloco curto; os recursos reais e os demais testes associados também passaram.
- Recompilado `build-static` Release x64 com `vcvars64.bat` e `cmake --build build-static --config Release`. `ctest --test-dir build-static --output-on-failure`: 42/42, 92,65 s. SHA-256 de `build-static/mrbones.exe`: `84abb5a9b0a7fd5853d1ca8dc6775e0d57785f01808867579552176f354ed464`. `dumpbin /dependents` mostra USER32, GDI32, WINMM, bcrypt e KERNEL32.
- Consultada a árvore ISO de Disco 1 (`build-static/disc1-iso-list.txt`); a origem das telas de logotipo observadas antes de `/29INTRO/20AVI` ainda não foi estabelecida. Permanecem pendentes sua reconstrução, o teste do controle no Ymir e a medição audiovisual ponta a ponta.

### 2026-11-08 — amostra audiovisual simultânea do executável

- Com o Ymir pausado, executado `build-static/mrbones.exe` com o disco 1 e gravado o loopback estéreo em `build-static/port-intro-simultaneous.wav`, iniciando às 1794180810,113 s. A correlação de cada canal com o canal correspondente de `intro-reference.pcm`, em janelas de um segundo iniciadas aos 5, 10, 15, 20, 25 e 30 s da fonte, foi arredondada a 1,00000; o alinhamento foi de 0,1815 s na gravação para todos os pontos medidos.
- Durante essa gravação, observada a janela real do port entre 1794180836,931 e 1794180837,028 s e novamente entre 1794180845,014 e 1794180845,165 s. Aplicando o alinhamento de áudio, esses intervalos correspondem aproximadamente a 26,636–26,733 s e 34,719–34,870 s da fonte. As imagens observadas, respectivamente o portão do cemitério e a caveira diante do céu, foram visualmente consistentes com quadros da fonte extraídos em 26,67 e 34,8 s (`build-static/ref-t26.png`, `ref-t34.png`). A comparação visual é qualitativa e as capturas da janela foram exibidas na sessão de controle, não salvas como arquivos; não comprova igualdade de pixels ou latência abaixo de um quadro.
- O arquivo de gravação inclui latências de captura e do dispositivo; essa amostra não cobre toda a abertura nem a sequência narrativa. A origem das telas de logotipo que precedem `/29INTRO/20AVI` continua não identificada. O build estático e os 42/42 testes são os registrados na seção anterior; não houve alteração de código nesta etapa.

### 2026-11-08 — apresentação dos filmes na janela

- Em `mrbones.c`, a abertura, o filme narrativo e as cenas especiais de apresentação com vídeo agora ocupam a maior área central de 320:224 que cabe na janela, com fundo preto nas bordas. As mensagens de controle, status e progresso não são desenhadas sobre essas apresentações; continuam disponíveis nas demais cenas e na barra de título. O framebuffer e a decodificação não foram alterados.
- Conferida a abertura em execução no Windows em janela normal e maximizada: a imagem permaneceu centralizada, proporcional e sem os textos de diagnóstico na área do vídeo. A mesma condição de apresentação se aplica a LOSE, mas sua imagem não foi capturada durante esta verificação; o acesso direto terminou antes da observação. A jogabilidade de RUNWAY apareceu com cores e cenário incorretos e continua não validada.
- Recompilado o executável principal Release estático após liberação do arquivo que estava aberto: `cmake --build build-static --config Release`. `test_menu_phase`, `test_special_phase` e `test_duck_avi` passaram 3/3; a suíte completa 42/42 é anterior a esta alteração de apresentação. `dumpbin /dependents` lista USER32, GDI32, WINMM, bcrypt e KERNEL32, sem DLL de codec ou runtime de desenvolvimento.
- As comparações parciais de imagem e áudio com Ymir descritas nas seções anteriores permanecem válidas para o decoder. Ainda faltam as telas de logotipo anteriores ao filme, o controle no Ymir e a comparação audiovisual integral da apresentação.

### 2026-11-08 — transição observada no executável

- Iniciada a versão estática com `--level /28LOSE`. A barra de título indicou `LOSE present` com decoder nativo e `soft=0`; após o término, a janela mostrou a tela de menu e `PROGRESS LOSE -> MENU`. Na sequência, a janela foi usada e passou a `MENU STORY`, exibindo quadros do filme narrativo sem texto de diagnóstico no quadro. A imagem de LOSE não ficou visível na captura da janela: ela estava encoberta e a cena terminou antes de ser trazida à frente. Não se conclui que toda a nova apresentação de LOSE tenha sido comparada visualmente.
- Examinado `LOADER/LOADER.MAP`; os símbolos `_LoadFirstLevel`, `_PlayDuckMovie` e `_PlayNewDuckMovie` estão localizados, mas a consulta aos nomes do mapa e à árvore ISO não identificou a origem dos logotipos observados antes de INTRO. Sua reconstrução permanece aberta. O vídeo narrativo visto nesta execução é evidência do fluxo do port, não da mesma seleção no Ymir.
- Nenhum código foi alterado; o executável estático e as verificações continuam os da seção anterior. O controle no Ymir e a comparação audiovisual completa também seguem pendentes.

### 2026-11-08 — apresentação de LOSE observada

- Executado `build-static/mrbones.exe` com o disco 1 e `--level /28LOSE`; a janela foi trazida à frente durante a apresentação. O estado observado indicava aproximadamente 71/203 intervalos e 72 quadros processados (`tm1=72`, `soft=0`). A imagem exibiu o esqueleto sobre fundo preto, sem as listras da captura original do defeito e sem o texto de diagnóstico sobre a área do filme. Esta é uma inspeção visual pontual; não é uma comparação de pixels com o Ymir nesse mesmo quadro.
- Esta observação complementa as comparações anteriores de quadros 39 e 41 com o Ymir e as capturas de áudio; não valida o percurso normal por derrota nem a cena inteira. A barra de título ainda apresenta informações técnicas e as telas anteriores a INTRO continuam por identificar.
- Não houve alteração de código ou nova compilação nesta etapa. O executável usado foi o Release estático recompilado na seção de apresentação da janela; os testes e dependências são os ali registrados.

### 2026-11-08 — novos quadros de LOSE no Ymir

- Executado Ymir 0.4.1 com disco 1 e perfil local. No breakpoint `0x0020B95C` do carregador, definido temporariamente em RAM `0x00228970 = 0x1c` para acessar LOSE. Não foi alterado o conteúdo do disco nem exercitado o caminho de derrota pela jogabilidade.
- Capturas F12 de 20:44:00.592 e 20:44:05.460 em `tools/ymir-profile/screenshots` foram convertidas de 640×448 para 320×224 por vizinho mais próximo e comparadas em RGBA com `build-static/lose-hashes.txt`. Coincidiram byte a byte, respectivamente, com os quadros não vazios 31 (`aabea1344b7df0e75dc76d0de2bc562cfb60d5bcbdfd8ce90cc69ae13991e3d1`) e 93 (`1005d8174a6f2a9edad1376c828d341ab473bb56074ae5cf3e00ecd42e532d97`). O quadro 93 ocorre próximo ao fim dos dados de imagem; a captura correspondente é preta.
- Incorporados ambos os hashes a `test_duck_avi`. `cmake --build build-static --config Release` recompilou o teste, e `ctest --test-dir build-static -R '^test_duck_avi$' --output-on-failure` passou. O executável nativo de jogo não foi alterado; a última suíte completa continua sendo a de 42/42 anterior à alteração de apresentação da janela.
- A verificação agora cobre diretamente os quadros 31, 39, 41 e 93 no Ymir, não a totalidade do filme. Permanecem pendentes os controles no Ymir, a reprodução completa e a identificação dos logotipos anteriores a INTRO.

### 2026-11-08 — propagação de falhas de decodificação DK3

- Em `append_pcm`, erros de `avcodec_receive_frame` diferentes de `EAGAIN` e fim do decoder passam a rejeitar o AVI em vez de serem ignorados. A mudança afeta a abertura de recursos com áudio comprimido; a reprodução normal de áudio e vídeo não foi alterada.
- Acrescentado teste que copia o AVI de LOSE em memória e altera para `0xff` o índice de passo do primeiro bloco DK3. O carregamento rejeitou o bloco inválido. Esse caso já era rejeitado pelo envio ao decoder antes da alteração e não força especificamente o novo ramo de erro de recepção; ele amplia a cobertura de entrada inválida, mas não comprova isoladamente esse ramo.
- Recompilado `build-static` Release x64 com `vcvars64.bat`; `ctest --test-dir build-static --output-on-failure` passou 42/42 em 93,01 s, incluindo áudio com relógio real e recursos dos discos. `dumpbin /dependents` lista USER32, GDI32, WINMM, bcrypt e KERNEL32. SHA-256 de `mrbones.exe`: `c4c43e612fc245129f583c9a05750099ebc715907cbd97cb71cc67ec52bafa7`.
- Não houve nova comparação de vídeo com o Ymir nesta etapa. As verificações parciais anteriores e as pendências de fluxo de controle, logotipos iniciais e comparação audiovisual completa permanecem aplicáveis.

### 2026-11-08 — identificação dos logotipos de INTRO

- Correção das seções anteriores: os logotipos animados SEGA, Amoebae, ZONO e Angel Studios observados no início **não são recursos desconhecidos anteriores a `/29INTRO/20AVI`**. Estão no próprio AVI: amostras aproximadas em 3, 4–5, 8–9 e 15–16 segundos, respectivamente. O quadro de abertura estática de licenciamento do Saturn é uma tela distinta, observada antes do carregador, e não foi objeto desta identificação.
- A cópia `build-static/intro_reference.avi` usada para examinar as amostras tem SHA-256 `fb8446755ffa14b122c2afc8d4d719a1c8e9b7d909d11821d2e029934e77cf35`, idêntico ao conteúdo lido diretamente de `/29INTRO/20AVI` na faixa de dados do disco 1 (LBA 140561, 7.718.942 bytes). A prancha `build-static/intro-logos-timeline.png` foi produzida a partir dessa cópia por FFmpeg; as amostras após seeks podem conter artefatos por dependência entre quadros e servem apenas para identificar o conteúdo.
- Incorporadas ao `test_duck_avi` assinaturas dos quadros nativos 45, 120 e 240, correspondentes às regiões dos logotipos animados. Elas fixam a saída atual do decoder e não são comparação direta desses quadros específicos com o Ymir. Após `cmake --build build-static --target test_duck_avi --config Release`, o teste passou. O executável de jogo não precisou ser alterado nem recompilado: já reproduz o AVI completo como registrado nas seções anteriores.
- A tentativa de nova captura por controle de interface foi interrompida porque havia atividade do usuário em outro aplicativo. Permanecem pendentes a comparação audiovisual integral e o controle do jogo original no Ymir; as referências parciais já registradas continuam válidas.

### 2026-11-08 — validação dos formatos dos fluxos AVI

- O carregador agora examina os `strl` do cabeçalho AVI e aceita vídeo DUCK seguido opcionalmente de áudio estéreo DK3 ou PCM de 16 bits. Cabeçalhos ausentes, fora dos limites, formatos desconhecidos, ordem de fluxos incompatível e parâmetros inválidos são rejeitados com `duck_avi: unsupported or invalid stream header`, antes de tratar o conteúdo como vídeo ou PCM.
- Testes sobre cópias em memória de LOSE alteram separadamente o identificador de vídeo e o formato de áudio e verificam a rejeição com mensagem. Os nove arquivos `20AVI` não vazios do disco 1 e os onze do disco 2 foram inspecionados nos cabeçalhos: todos indicam DUCK/DK3 estéreo a 44.100 Hz. Essa inspeção de cabeçalho não é decodificação integral de todos os vídeos.
- Recompilado `build-static` Release x64 com `vcvars64.bat`; a suíte `ctest --test-dir build-static --output-on-failure` passou 42/42 em 93,25 s. `dumpbin /dependents` mostra USER32, GDI32, WINMM, bcrypt e KERNEL32. SHA-256 de `mrbones.exe`: `2b9f628748faf9c5d7fce4ceb837f321d239d9acd218591f77e216a465d2f730`.
- Não houve nova captura no Ymir nesta etapa: a interface do usuário estava em uso. A comparação audiovisual integral e a validação dos controles do original permanecem pendentes; as comparações parciais anteriores não foram substituídas pelos testes de cabeçalho.

### 2026-11-08 — comparação ampliada de LOSE com Ymir

- Com o disco 1 no Ymir 0.4.1, acessada LOSE duas vezes mediante breakpoint no carregador e alteração temporária de RAM `0x00228970 = 0x1c`; os arquivos do disco não foram modificados. Foram feitas 350 capturas F12 nas janelas 20:57–20:59 em `tools/ymir-profile/screenshots`. As 14 capturas sem correspondência ocorreram antes da apresentação do filme, durante a tela de licenciamento.
- As 336 capturas restantes, reduzidas de 640×448 para 320×224 por vizinho mais próximo, coincidiram em RGBA com quadros produzidos pelo decoder nativo. Elas cobrem 102 dos 106 índices de quadros não vazios, ou 91 das 95 imagens distintas: os índices 0, 23, 71 e 81 não foram amostrados. Quadros repetidos têm hash idêntico e não é possível distinguir sua posição temporal apenas pela imagem. Essa comparação não valida a temporização de cada quadro, o caminho normal de derrota ou o áudio simultâneo.
- Reexecutado `ctest --test-dir build-static -R '^test_duck_avi$' --output-on-failure`, com `MB_DUCK_LOSE_HASH_OUT` apontando para `build-static/lose-hashes-current.txt`: passou, e as 106 assinaturas são idênticas às de `build-static/lose-hashes.txt`. O SHA-256 do executável permanece `2b9f628748faf9c5d7fce4ceb837f321d239d9acd218591f77e216a465d2f730`; a última compilação estática e a suíte completa 42/42 constam na seção anterior. Não houve alterações no código nesta etapa.
- Permanecem pendentes as quatro imagens sem amostra, comparação audiovisual integral e validação dos controles e transições pelo percurso normal no original. A captura fornecida pelo usuário confirma visualmente o funcionamento da versão emulada, sem demonstrar por si só equivalência integral do port.

### 2026-11-08 — parâmetros de áudio a partir do fluxo AVI validado

- Corrigido `duck_avi.c`: taxa, canais, alinhamento e identificação DK3 são obtidos diretamente do `strf` do fluxo de áudio validado, em vez de procurar assinaturas de bytes por todo o arquivo. Antes disso, PCM aceitável pelo validador usava inadvertidamente os valores padrão de 22050 Hz e um canal. Agora PCM16 também exige alinhamento compatível com o número de canais e rejeita blocos incompletos. Os filmes originais inspecionados usam DK3; esta correção não é apresentada como solução de uma divergência sonora observada neles.
- Acrescentada regressão em `test_duck_avi` que muda o cabeçalho de uma cópia em memória de INTRO para PCM estéreo de 32000 Hz e verifica a leitura dos parâmetros. O conteúdo comprimido dessa cópia é interpretado como dados PCM apenas para testar metadados, sem alegação de validade sonora. A suíte completa `ctest --test-dir build-static --output-on-failure` passou 42/42 em 94,55 s, incluindo o teste de áudio em tempo real e os recursos dos discos.
- Recompilado `build-static` Release x64 com `vcvars64.bat` e `cmake --build build-static --config Release`. SHA-256 de `mrbones.exe`: `c0c0d34c78606d11be4c819c752d9cd3acf19c0157d70e684445cd00ced2e602`. `dumpbin /dependents` lista USER32, GDI32, WINMM, bcrypt e KERNEL32.
- A comparação audiovisual completa com Ymir e as transições do percurso normal continuam pendentes; permanecem válidas as limitações e capturas registradas na seção anterior.

### 2026-11-08 — comparação contínua do áudio de LOSE

- Reanalisadas as gravações existentes `build-static/ymir-lose-loopback.wav` e `build-static/port-lose-loopback.wav` contra os 13,509 s de `build-static/lose_audio.pcm`, a 44.100 Hz estéreo. Para cada canal foram calculadas correlações normalizadas em 13 janelas consecutivas de um segundo, com busca de alinhamento local por convolução FFT numa faixa de ±50 ms. A última meia segunda já fora examinada na seção de estéreo e relógio de LOSE.
- No Ymir, as correlações por janela variaram de 0,99767 a 0,99984; no port, de 0,99994 a 0,99999. O início estimado em todas as janelas variou de 16,02887 a 16,02912 s na gravação do Ymir e ficou em 0,14354 s na gravação do port. Esta estimativa para o port difere dos ~0,161 s aproximados registrados anteriormente; refere-se ao máximo da correlação usado nesta análise, e não estabelece a origem da diferença anterior. São gravações de loopback e os valores não medem sincronização entre imagem e áudio nem igualdade bit a bit.
- Nenhuma alteração de código ou nova compilação nesta etapa. A validação audiovisual completa, os quatro quadros de LOSE ainda não capturados e as transições pelo percurso normal permanecem pendentes.

### 2026-11-08 — remoção de imagens sintéticas sem decoder

- Removido de `duck_avi.c` o gerador aproximado de blocos usado quando o decoder TrueMotion1 falhava numa compilação sem FFmpeg. Sem o decoder, a abertura do AVI agora falha com `TrueMotion1 decoder unavailable`; dados DK3 sem decoder também não são tratados como áudio válido. A compilação Windows de produção já usava FFmpeg e não executava esse fallback; esta alteração evita resultados enganosos em outras configurações, sem atribuir a ela a correção anterior das listras.
- Criado `test_duck_no_ffmpeg`, compilado deliberadamente sem as bibliotecas FFmpeg, que apresenta um cabeçalho AVI DUCK válido e verifica a mensagem de erro. A suíte completa passou 43/43 em 95,64 s. Após ajuste apenas dos comentários da interface e nova recompilação, `test_duck_no_ffmpeg` e `test_duck_avi` passaram 2/2.
- Recompilado `build-static` Release x64 com `vcvars64.bat` e `cmake --build build-static --config Release`. SHA-256 de `mrbones.exe`: `5f68d8cc3602cb175c74240870d2bcdb823c0bf2dd93926804320519f174169d`. `dumpbin /dependents` mostra USER32, GDI32, WINMM, bcrypt e KERNEL32.
- Não foi feita nova captura no Ymir nesta etapa. As limitações de comparação audiovisual, quadros não amostrados e transições pelo percurso normal permanecem registradas nas seções anteriores.

### 2026-11-08 — resposta de Start no menu original

- Retomado no Ymir 0.4.1 o estado previamente usado para LOSE. Após a cena, o jogo apresentou a tela de título com START e OPTIONS. Uma tentativa curta com `G` não mostrou mudança imediata; na sequência de tentativas repetidas com `G`, a automação informou interação simultânea na janela e interrompeu a sequência. Observou-se então tela preta e a apresentação `GRAVE ESCAPE`.
- Esta observação mostra que a execução saiu do menu e alcançou uma apresentação da primeira fase, mas não identifica qual entrada provocou a transição nem prova se `/03MENU/20AVI` foi iniciado ou pulado: entradas repetidas e a interação simultânea impedem essa conclusão. A comparação do percurso com uma única entrada controlada segue pendente. As imagens foram observadas na interface, sem arquivo de captura salvo nesta tentativa.
- Não houve mudança de código ou compilação; o executável e os testes permanecem os da seção anterior. A atividade na área de trabalho impediu continuar a automação nesta tentativa.

### 2026-11-08 — reprodução integral dos FMVs de apresentação

- Em `special_phase.c`, removido o encerramento artificial aos 240 quadros de filmes diferentes de LOSE e removido o avanço adicional da imagem ao segurar a direção direita. A reprodução desses filmes segue agora o relógio do decoder até o último intervalo, sem adiantar apenas o vídeo em relação ao áudio. Os módulos protegidos `runway_phase.c`, `pan_scroll.c`, `stage_enemy.c` e `menu_phase.c` não foram alterados nesta etapa.
- Acrescentada regressão com `/27FINALE/20AVI` do disco 2: com o relógio externo parado, segurar a direita não adianta a imagem; com relógio simulado, todos os 5.658 intervalos de vídeo são decodificados até a conclusão, em vez do encerramento anterior no quadro 241. Os testes de campanha e progresso passaram a usar relógio simulado nos filmes de apresentação, para cobrir o fluxo completo sem exigir espera em tempo real. Esta verificação não comprova a fidelidade do final ao Ymir nem a sincronização ponta a ponta de todos os filmes.
- Recompilado `build-static` Release x64. O cache indica `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`, as regras de compilação usam `-MT`, e `dumpbin /headers` identifica 8664/x64, PE32+. As únicas DLLs importadas são USER32, GDI32, WINMM, bcrypt e KERNEL32. SHA-256 de `mrbones.exe`: `9617dd9727832c10f6c2c91875529c93e6f1343b1b8a73951620b89f56177fce`.
- `ctest --test-dir build-static --output-on-failure`: 43/43 passaram em 107,76 s, incluindo teste de áudio em tempo real, teste de apresentação integral e fluxo de campanha simulado. A comparação audiovisual completa com o original continua pendente.

### 2026-11-08 — decodificação integral dos FMVs dos dois discos

- Criado `test_fmv_all`, que percorre a árvore ISO dos dois discos, encontra cada arquivo `/20AVI` não vazio, decodifica todos os intervalos de vídeo e o fluxo de áudio, exige áudio estéreo e confere a diferença entre as durações nominais de imagem e som (até dois intervalos de quadro). Encontrados e decodificados 9 arquivos no disco 1 e 11 no disco 2, incluindo 5.658 intervalos de FINALE. Os dados originais não foram alterados.
- Executados `cmake --build build-static --config Release` e `ctest --test-dir build-static --output-on-failure`: 44/44 testes passaram em 118,49 s, incluindo o teste de áudio em tempo real e a nova varredura. O executável do jogo não precisou ser religado por alterações restritas ao teste; mantém SHA-256 `9617dd9727832c10f6c2c91875529c93e6f1343b1b8a73951620b89f56177fce`, compilação Release x64 com runtime estático da seção anterior.
- A leitura integral e a concordância de duração não demonstram que cada arquivo seja apresentado na cena correta nem equivalência integral de imagem, áudio ou sincronização com o Ymir. A integração de FMVs às fases que os contêm ainda requer avaliação, respeitando os módulos que o usuário pediu para preservar.

### 2026-11-08 — comparação independente do áudio dos 20 FMVs

- `test_fmv_all` passou a registrar SHA-256 do PCM16 intercalado decodificado pelo port em cada `/20AVI`. O script local `build-static/compare-all-fmv-audio.py` repetiu a extração dos 20 arquivos a partir dos discos e os decodificou separadamente com FFmpeg 5.1.2 de linha de comando, exclusivamente para teste. Os 20 hashes do PCM completo coincidiram byte a byte, incluindo os dois canais, INTRO, MENU, LOSE e FINALE. Os arquivos temporários foram removidos ao fim do script.
- Executado `cmake --build build-static --config Release` e o próprio `test_fmv_all` pelo script: 20/20 comparações passaram. A última suíte completa segue sendo 44/44 da seção anterior; a alteração nesta etapa acrescentou apenas saída de hashes ao teste e não alterou o executável. SHA-256 de `mrbones.exe` confirmado: `9617dd9727832c10f6c2c91875529c93e6f1343b1b8a73951620b89f56177fce`.
- Essa comparação cobre a decodificação integral do sinal, não a saída de áudio do dispositivo, o sincronismo audiovisual real ou a associação de todos os filmes às cenas originais. Nenhum dos quatro módulos de renderização indicados pelo usuário foi alterado.

### 2026-11-08 — relógio de áudio real em FINALE

- Expandido `test_movie_audio` para carregar `/27FINALE/20AVI` do disco 2, reproduzir o PCM pelo dispositivo `waveOut` e verificar a relação entre a posição de áudio e o quadro decodificado durante os primeiros 1,5 s, com a direção direita pressionada. Na execução isolada o relógio chegou a 1.514 ms e ao quadro 23 esperado. A verificação não mede latência de exibição nem o filme completo.
- A configuração do CTest fornece os dois discos a esse teste. Após a alteração, `ctest --test-dir build-static --output-on-failure` passou 46/46 em 127,12 s, incluindo o teste de áudio e a varredura dos 20 FMVs. A recompilação incorporou outras alterações que já estavam presentes no projeto, inclusive os testes `test_menu_options` e `test_aabb_hitchk`; esta etapa não alterou os quatro módulos protegidos.
- Executável Release recompilado: `build-static/mrbones.exe`, SHA-256 `b30be2c4e90f59f5792698e151307f0740b05432fc64d6707f69e4157eb30bda`. As regras usam `-MT`; `dumpbin` identifica PE32+ x64 e dependências USER32, GDI32, WINMM, bcrypt e KERNEL32. A comparação audiovisual integral com o Ymir e a associação de todos os filmes às respectivas cenas permanecem pendentes.

### 2026-11-08 — início do relógio de áudio antes do primeiro tick

- Em `mrbones.c`, separado o início do áudio do disparo de efeitos e chamado o início de áudio pendente antes de calcular o tempo do `WM_TIMER`. Antes, o primeiro tick de uma abertura ou cena especial podia acumular aproximadamente 16 ms no vídeo antes de iniciar o áudio; a alteração evita esse avanço prévio. Uma transição que cria um filme durante o passo continua iniciando o áudio no mesmo ciclo. Nenhum dos quatro módulos de renderização protegidos foi alterado.
- Recompilado `build-static` Release x64; a regra usa `-MT`, e `dumpbin` confirma PE32+ x64 com USER32, GDI32, WINMM, bcrypt e KERNEL32. SHA-256 de `mrbones.exe`: `a1ff4ba0454a33591ee98db5438a4acfb58ca44c1e82a0d5f2a386fee87162f3`.
- `ctest --test-dir build-static --output-on-failure`: 46/46 passaram em 123,22 s, incluindo os testes de áudio em tempo real e todos os 20 arquivos FMV. Esses testes não verificam diretamente a ordem dos eventos `WM_TIMER` na janela do executável e não substituem uma nova captura audiovisual simultânea; a fidelidade ponta a ponta continua pendente.

### 2026-11-08 — amostra audiovisual simultânea após ajuste inicial

- Executado `build-static/mrbones.exe` com o disco 1 e gravado loopback estéreo em `build-static/port-intro-sync-current.wav`, com início informado pelo gravador em 1794184373,639856 s. Durante a mesma execução, capturada a janela em `build-static/port-intro-sync-current.png`; a chamada de captura ocorreu entre 1794184396,155 e 1794184396,290 s. O binário usado tinha SHA-256 `a1ff4ba0454a33591ee98db5438a4acfb58ca44c1e82a0d5f2a386fee87162f3`.
- Comparadas janelas de 1 s iniciadas aos 5, 10, 15, 20, 25 e 30 s de `intro-reference.pcm`: em ambos os canais, as correlações arredondadas a seis casas foram 1,000000, com deslocamento de 0,204762 s no arquivo loopback. O alinhamento inclui início efetivo da reprodução e latências do dispositivo/captura.
- Reduzida a área do vídeo da captura e comparada, por diferença média RGB, com quadros do AVI extraídos independentemente pelo FFmpeg em torno do ponto observado. O melhor resultado foi o quadro de PTS 332/15 s = 22,133 s (`build-static/intro-sync-frames/frame-0332.png`), diferença média 2,88 em 255 na área ativa; a correspondência é visual aproximada, pois a escala da janela e o tratamento Saturn diferem do FFmpeg padrão. A hora da chamada, descontado o alinhamento de áudio, corresponde a aproximadamente 22,310–22,445 s de áudio: diferença aparente de 0,177–0,312 s em relação ao PTS da imagem.
- Não se pode atribuir essa diferença diretamente ao port: a captura de janela pode entregar um quadro anterior e o loopback e os relógios têm latências e incertezas não calibradas. Este resultado não comprova sincronização abaixo de um quadro e merece medição com captura audiovisual de relógio comum. Nenhum código ou módulo protegido foi alterado; a última suíte completa permanece 46/46 da seção anterior.

### 2026-11-08 — calibração parcial da captura e relógio interno

- Compilado em `build-static/fmv-capture-clock.exe` um relógio Win32 de diagnóstico, sem alterar o port. Duas capturas da sua janela mostraram 00:36:23,631 UTC dentro da chamada entre 23,577–23,680 e 00:36:27,692 UTC dentro da chamada entre 27,617–27,739. São duas amostras e não estabelecem limite geral para a latência do capturador em todas as condições.
- Compilado `build-static/fmv-trace.exe` a partir de uma cópia do host atual, adicionando apenas registro de diagnóstico ao fim de `paint`; não substitui `mrbones.exe`. Em `build-static/fmv-trace.log`, 162 registros cobrem aproximadamente os primeiros 4 s da abertura. Para registros com áudio ativo após 1 s, a posição de áudio menos o instante nominal do quadro estava entre 0,7 e 81,7 ms, média 38,4 ms; um intervalo de quadro é aproximadamente 66,7 ms. O relógio do áudio é consultado depois do desenho e pode avançar entre o tick e o registro. A janela deixou de produzir registros após esse trecho; a causa não foi determinada.
- O loopback simultâneo em `build-static/fmv-trace-loopback.wav` apresentou avisos de descontinuidade e correlações reduzidas; não serve para atribuir a diferença aparente de 0,177–0,312 s da medição anterior ao port ou à captura. A comparação audiovisual com relógio externo comum segue inconclusiva. Nenhum módulo protegido ou código do executável de produção foi alterado nesta etapa; a suíte anterior permanece 46/46.

### 2026-11-08 — repetição do relógio com loopback

- Repetido o executável de diagnóstico sem gravação: permaneceu em execução durante 12 s e produziu 475 registros de pintura até o quadro 174; foi encerrado ao final do teste. Isso mostra que a reprodução pode ultrapassar os quatro segundos registrados na tentativa anterior, mas não identifica por que aquela tentativa deixou de registrar quadros.
- Repetido com loopback de 12 s (`build-static/fmv-trace-retry-av.wav` e `.log`): o processo permaneceu ativo e o gravador não informou descontinuidades. Janelas do PCM em 1, 2, 3, 5 e 8 s tiveram correlação arredondada a 1,000000 e deslocamento uniforme de 0,164512 s no arquivo WAV.
- Nos registros de pintura próximos a 1, 2, 3, 5 e 8 s do relógio de áudio do processo, a posição calculada a partir do início de gravação mais esse deslocamento ficou aproximadamente 224–225 ms à frente da posição consultada pelo processo. A diferença é compatível em magnitude com a diferença aparente observada na captura anterior, mas não prova sua causa: início efetivo dos buffers de gravação, latência de dispositivo e instante de pintura ainda influenciam a comparação. O teste usa uma cópia instrumentada, não mede diretamente a saída audiovisual do binário distribuído.
- Não houve alteração de código de produção nem dos quatro módulos protegidos. A última compilação estática e a suíte 46/46 permanecem registradas nas seções anteriores.

### 2026-11-08 — impedir interrupção de FMV por teclas de outras janelas

- Em `mrbones.c`, a leitura global de `GetAsyncKeyState` agora só alimenta o controle quando a janela do jogo é a janela em primeiro plano. Antes, teclas usadas em outros programas podiam ser interpretadas pelo jogo e interromper a abertura em segundo plano. A perda de foco também libera os botões no estado do controle. Os quatro módulos de renderização protegidos não foram alterados.
- Teste manual: iniciada a abertura e enviadas 30 teclas `C` à janela auxiliar de relógio, em primeiro plano; o título da janela do port ainda indicava `MENU TITLE` depois da sequência. O processo de teste terminou antes da tentativa de confirmação de entrada na janela do jogo, portanto esse teste isolado não estabelece que os pulsos teriam sido detectados sem a correção nem valida a entrada em primeiro plano.
- Recompilado `build-static` Release x64 `/MT`, com importações USER32, GDI32, WINMM, bcrypt e KERNEL32. SHA-256 de `mrbones.exe`: `5c8346bf686849ec1b3927587cda407a4abc4796250a6ae55918b291ae7d4dc6`. A verificação posterior `cmake --build build-static --config Release -- -n` informou que não havia recompilação pendente.
- A suíte inicial passou em 46/47; falhou `test_sidescrl_physics` na expressão `a.y > prev_y`, fora do escopo de FMV. Enquanto a suíte rodava, esse teste foi atualizado externamente para aceitar deslocamento subpixel; recompilei somente o alvo existente e ele passou isoladamente. Os testes de áudio e dos 20 FMVs passaram na suíte inicial. Não foi repetida a suíte inteira após a atualização externa do teste, nem confirmado o controle em primeiro plano nesta etapa.

### 2026-11-08 — controle em primeiro plano e suíte completa

- Iniciado o executável estático com o disco 1; a janela indicou `MENU TITLE` durante a abertura. Após trazê-la ao primeiro plano e pressionar Enter, passou a `MENU START`. Esse teste complementa a observação anterior de que teclas enviadas à janela auxiliar não avançaram a abertura em segundo plano.
- Confirmado que a compilação não tinha trabalho pendente e repetido `ctest --test-dir build-static --output-on-failure`: 47/47 passaram em 120,94 s, incluindo áudio em tempo real, 20 FMVs e `test_sidescrl_physics` atualizado externamente. O SHA-256 de `mrbones.exe` permanece `5c8346bf686849ec1b3927587cda407a4abc4796250a6ae55918b291ae7d4dc6`.
- Sem alterações no código ou nos módulos protegidos nesta etapa. A validação da associação de todos os filmes às cenas e a comparação audiovisual integral com o Ymir permanecem pendentes.

### 2026-11-08 — análise do encaminhamento FMV em MAUSO

- O MCP do REA não estava disponível; `rea doctor --client codex --json` informou registro divergente e Node 24.18.0 abaixo do requisito reportado. Nenhuma configuração foi alterada. A análise abaixo foi feita diretamente no Ghidra headless, sem atribuí-la ao MCP REA.
- Extraído `/05MAUSO/03CODE` do disco 1, SHA-256 `013a6edf094b91c57d39a2390bd79a4f2056307958802fc3a7d92004cbe64d12`; a base `0x00200000` foi rejeitada após gerar instruções inválidas e a importação repetida na base de mapa `0x00232000`. A descompilação de `CEndLevel::Brain` em `0x0029d918`, `LoadNewDuckMovie` em `0x0029da8c` e `SetNewDuckMovie` em `0x0029d904` está em `docs/ghidra-fmv-mauso.txt`.
- Na ramificação padrão com resultado 0 e campos opcionais em -1, a função passa 6 a `_LoadLevel` (`0x0020b4ac`); quando `+0x4c` está configurado, chama `_PlayNewDuckMovie` (`0x0020b714`) com os dois campos. A pesquisa estática não encontrou referência direta ao invólucro `0x0029da8c`, mas não exclui chamadas indiretas ou código não analisado. O arquivo `docs/fmv-mauso-flow.md` registra evidências e limites: a presença de `/06VALLEY/20AVI` não basta para provar que ele é exibido ao concluir MAUSO.
- Não houve alteração de código ou de módulos protegidos nesta etapa. O executável estático e a suíte 47/47 permanecem os da seção anterior; falta observação em runtime dos campos e da chamada para estabelecer a sequência real.

### 2026-11-08 — transição VALLEY → FMV de GUITAR → GUITAR

- O Ghidra headless descompilou `CEndLevel::Brain` de `/06VALLEY/03CODE` em `0x002be75c` (base `0x00232000`, SHA-256 `98da3f5ccbd44012281db4c98d1dac60a9ec58ef8a5580e69f7ad35b8a7dc46c`). No resultado padrão 0, os literais e o mapa mostram chamada `_PlayDuckMovie(7)` em `0x0020b61c`. A análise do carregador mostra que, após executar o leitor de filme, o literal `0x0020b6d8` aponta para `_LoadLevel` em `0x0020b4ac`, chamado com o mesmo número. Evidências e limites em `docs/ghidra-fmv-valley.txt`, `docs/ghidra-fmv-loader.txt` e `docs/fmv-valley-flow.md`.
- Implementada no host a transição específica de vitória de VALLEY: apresentar `/07GUITAR/20AVI` como filme, depois iniciar GUITAR. `stage_runner_begin_movie` permite carregar o AVI como apresentação, e Start encerra esse modo; o host encerra o áudio ao observar a conclusão da cena. Nenhum dos quatro módulos de renderização protegidos foi alterado.
- O novo `test_fmv_transition` simula a vitória de VALLEY, percorre o filme integralmente com relógio simulado e verifica a entrada em GUITAR. `test_stage_runner` confirma que Start termina a apresentação. Os testes não reproduzem a passagem visual pelo Ymir nem medem a interrupção real do dispositivo de áudio. A fase GUITAR atual também usa esse AVI como fundo; se a repetição corresponde ao original continua sem validação.
- Após a compilação de produção, `ctest --test-dir build-static --output-on-failure` passou 49/49 em 135,69 s. Houve uma tentativa intermediária de link bloqueada por outro processo `test_fmv_all`; após o término desse processo a compilação concluiu. O build incorporou alterações de teste já existentes de outra frente, sem modificação nossa dos módulos protegidos.
- `build-static/mrbones.exe`: Release PE32+ x64, flags `-MT`; `dumpbin` lista USER32, GDI32, WINMM, bcrypt e KERNEL32. SHA-256 `b2f2cf35b1c6456741a7666cbe41342b2618e5001db38808a01d67a7f5a35bad`.

### 2026-11-08 — correção da origem dos FMVs em VALLEY e GUITAR

- **Correção da seção anterior:** a associação do AVI de GUITAR à vitória de VALLEY estava errada. Os literais do carregador mostram que `_LoadLevel` (`0x0020b4ac`) grava o nível solicitado em `0x0021d9e4` e zera `0x0021d9e8`; `_PlayDuckMovie` (`0x0020b61c`) copia o primeiro para o segundo antes de definir o primeiro com o argumento e carregar o leitor. `_PlayNewDuckMovie` define origem e destino separadamente. Assim, no caminho analisado, o filme vem da fase corrente e o argumento indica a próxima fase; os nomes/endereço de dados do mapa foram revalidados pelos literais em vez de usados diretamente.
- `CEndLevel::Brain` de VALLEY chama `_PlayDuckMovie(7)` no ramo padrão de vitória: `/06VALLEY/20AVI`, depois `/07GUITAR`. Em GUITAR, o prólogo real foi encontrado em `0x00261ed8` (mapa `0x00261ee8`); o ramo equivalente chama `_PlayDuckMovie(8)`: `/07GUITAR/20AVI`, depois `/08BAT1`. BAT3 usa `_LoadLevel(11)` nesse ramo. Evidências em `docs/ghidra-fmv-valley.txt`, `docs/ghidra-fmv-guitar.txt`, `docs/ghidra-fmv-bat3.txt`, `docs/ghidra-fmv-loader.txt` e `docs/fmv-valley-flow.md`.
- Corrigido o host para manter separados o diretório que fornece o filme e o da próxima fase. `test_fmv_transition` simula as duas vitórias, exige os diretórios corretos, decodifica cada filme até o fim com relógio simulado e verifica a entrada na fase seguinte. O uso do AVI como fundo durante a própria fase GUITAR, o salto e a passagem visual no Ymir continuam sem confirmação; não se atribui esse comportamento ao original.
- Recompilado `build-static` Release PE32+ x64 `/MT`; `dumpbin` lista somente USER32, GDI32, WINMM, bcrypt e KERNEL32. SHA-256 de `mrbones.exe`: `65ef7de4841f90c990a93eea4251a52ecdbd99343c2f808088d78228593aa734`. `ctest --test-dir build-static --output-on-failure`: 50/50 passaram em 136,77 s. A compilação incorporou alterações já existentes de outra frente; não alteramos `runway_phase.c`, `pan_scroll.c`, `stage_enemy.c` ou `menu_phase.c` nesta etapa.

### 2026-11-08 — rotas FMV nas demais fases

- Extraídos 22 binários de fase dos dois discos, conferidos os prólogos `CEndLevel::Brain` contra os mapas e descompilados diretamente no Ghidra. A análise dos ramos padrão de vitória identificou 13 chamadas a `_PlayDuckMovie` antes de FINALE: VALLEY→GUITAR, GUITAR→BAT1, LOGS→DRUM, DRUM→GLASS, BIGBNS→VORTEX, VORTEX→RIDE, RIDE→VILLAGE, VILLAGE→ICE1, SHADOW→MOAT, MOAT→STAING, STAING→GENRM, GENRM→LAFF e LAFF→FINALE. Os ponteiros foram lidos dos binários e correspondem a `0x0020b61c`. `docs/fmv-routes.md` registra endereços, SHA-256, discos e descompilações; campos opcionais e comportamentos em runtime podem produzir outros caminhos.
- Integradas essas 13 rotas ao host: o filme pertence à fase concluída, e a fase seguinte é carregada após a apresentação; em VORTEX o vídeo do disco 1 antecede a montagem do disco 2. O teste `test_fmv_transition` confirma origem, destino e áudio em todas as rotas, percorre integralmente os vídeos de VALLEY e GUITAR e usa Start nas demais, incluindo a troca de disco. `test_fmv_all` decodifica integralmente os arquivos de ambos os discos.
- FINALE também chama `_PlayDuckMovie(3)` no ramo padrão analisado, mas não foi acrescentada outra reprodução: a implementação atual da própria fase já usa o mesmo AVI, e a relação entre filme e jogabilidade de FINALE ainda requer reconstrução e comparação com o original. Nenhum dos módulos protegidos foi alterado nesta etapa.
- Recompilado `build-static` Release PE32+ x64 `/MT`; `dumpbin` lista USER32, GDI32, WINMM, bcrypt e KERNEL32. SHA-256 `1bf2917b4839a01603c928bef2476542809963982b98e3faa0e299f58b347f76`. `ctest --test-dir build-static --output-on-failure`: 50/50 passaram em 136,01 s, incluindo áudio em tempo real. As transições visuais e o salto ainda não foram comparados integralmente com o Ymir.

### 2026-11-08 — referência controlada do vídeo FINALE

- Em um perfil isolado do Ymir 0.4.1, com o disco 2, o carregamento foi interrompido no SH-2 em `0020B95C` e o valor em memória de fase `00228970` foi ajustado para 27 (`0x1B`). Após retomar, a execução exibiu a cena inicial de `/27FINALE/20AVI`: o rosto iluminado em azul, seguido de Mr. Bones na sala. Captura do emulador: `build-static/ymir-finale-profile/screenshots/mr Bones [MK-81016]-20261108T222432.706.png`.
- Para identificar o arquivo, extraído temporariamente `/27FINALE/20AVI` do disco 2 e gerada folha de quadros em `build-static/finale-contact.png` com FFmpeg apenas para comparação. A sequência visual corresponde à observada no Ymir; esta identificação não demonstra igualdade de pixels, áudio ou sincronização.
- A entrada foi forçada na memória durante a inicialização, não obtida por uma partida normal. Portanto, ela não comprova quando o filme deve aparecer em relação à jogabilidade de FINALE nem resolve o uso atual do AVI como cenário da fase. A implementação permanece inalterada até haver evidência suficiente para corrigir essa transição sem duplicar ou omitir conteúdo.
- Recompilação Release x64 concluída com `VsDevCmd.bat -arch=x64` e `cmake --build build-static --config Release`; SHA-256 atual de `build-static/mrbones.exe`: `3c7a0fdba2d53776c20d864028fb709ded67390dee5746fad34e489cd3d595c9`. Testes selecionados `test_movie_audio`, `test_fmv_transition`, `test_duck_avi` e `test_sidescrl_wall`: 4/4 aprovados (86,69 s). A suíte completa anterior foi executada antes das alterações simultâneas recentes.

### 2026-11-08 — apresentação do FMV de FINALE sem sobreposições artificiais

- A cena de `/27FINALE/20AVI` observada no Ymir no teste controlado descrito acima não exibia inimigos, barra ou texto sobrepostos; o port desenhava esses elementos sobre o vídeo. `special_phase_render` agora mantém a imagem decodificada sem essas sobreposições quando o vídeo está ativo; o comportamento sem vídeo foi preservado. Não foram alterados `runway_phase.c`, `pan_scroll.c`, `stage_enemy.c` ou `menu_phase.c`.
- `test_special_phase` verifica, com inimigos ativos, que o quadro apresentado de FINALE coincide byte a byte com o quadro decodificado. A nova asserção falhou antes da correção e passou depois, inclusive após atualizações da fase. Isso verifica a composição no port, não prova correspondência integral com o original.
- Release Windows x64 recompilado com `VsDevCmd.bat -arch=x64` e `cmake --build build-static --config Release`; SHA-256 de `build-static/mrbones.exe`: `f7c996354375783dda73da00836125f9e55fa7dd871207dfdae8b26cd3cdfea3`. `dumpbin /dependents` mostra somente USER32, GDI32, WINMM, bcrypt e KERNEL32. `ctest --test-dir build-static -C Release --output-on-failure`: 51/51 aprovados em 137,25 s, incluindo teste de áudio e decodificação dos 20 FMVs.
- A ordem de FINALE em uma partida normal, a fidelidade integral de vídeo/áudio e as demais transições ao vivo no Ymir permanecem por validar.

### 2026-11-08 — comparação pontual de FINALE com Ymir

- A captura `build-static/ymir-finale-profile/screenshots/mr Bones [MK-81016]-20261108T222432.706.png` (640×448) foi convertida para RGBA 320×224 por amostragem vizinha. Uma sonda ligada à mesma `mbcore.lib` do port decodificou os 400 primeiros intervalos de `/27FINALE/20AVI` e identificou correspondência exata RGB no intervalo 117 (índice iniciado em zero). Os buffers RGBA inteiros, inclusive alfa, têm SHA-256 `48152dd9d19d9944a53f223ecfb6ccb9dce27e8385b096e3f2312df733bf9b27`.
- O teste `test_special_phase` agora avança até esse quadro e compara o hash do quadro apresentado pelo port à captura do Ymir. Teste aprovado; recompilação Release x64 concluída. `test_fmv_all` também foi executado novamente e decodificou os 20 arquivos.
- A evidência cobre um quadro, após seleção forçada da fase no depurador. Não comprova fidelidade de toda a sequência, sincronização audiovisual percebida ou ordem das transições durante a partida normal.

### 2026-11-08 — AVI com vários quadros no mesmo registro

- `ingest_rec_list` guardava apenas o último quadro de vídeo de cada bloco `LIST rec `. Corrigido para adicionar todos os quadros na ordem em que aparecem; os blocos de áudio continuam sendo decodificados na mesma sequência. Os quatro módulos de renderização protegidos não foram alterados.
- `test_duck_avi` reúne dois registros consecutivos do AVI de LOSE em um registro válido, sem alterar os dados de áudio/vídeo, e compara a contagem, o PCM completo e os primeiros cinco quadros com o arquivo original. O teste passou; `test_fmv_all` também passou após decodificar os 20 arquivos reais dos dois discos (2/2 testes, 23,04 s).
- Release x64 `/MT` recompilado; SHA-256 de `build-static/mrbones.exe`: `34c57c609a36f7a9558bc0ce56f5cc17449b914e31959c8147a3032109d68689`. `dumpbin /dependents` lista USER32, GDI32, WINMM, bcrypt e KERNEL32. A suíte completa anterior foi 51/51, antes desta alteração; as transições e a fidelidade integral no Ymir permanecem por validar.

### 2026-11-08 — falhas de FMV não reiniciam LOSE

- Uma falha de decodificação no filme de LOSE era convertida em derrota pelo `stage_runner`; o host podia então entrar novamente em LOSE. `advance_after_stage` agora identifica o erro do decodificador, registra a causa e encerra a rota, sem tratar a falha como conclusão normal. Erros de decodificação no menu também são registrados.
- O carregamento de `special_phase` preserva a mensagem de erro após liberar os recursos; o `stage_runner` inclui o detalhe ao informar falhas de carregamento, incluindo AVI ausente. Nenhum dos quatro módulos de renderização protegidos foi alterado.
- `test_fmv_transition` injeta um quadro inválido em LOSE e verifica que a execução termina sem repetir a cena. `test_stage_runner` verifica a mensagem de arquivo ausente. Os testes selecionados de transição, stage runner e fases especiais passaram 3/3 em 22,19 s.
- Release x64 `/MT` recompilado incorporando também mudanças já existentes de outra frente; SHA-256 de `build-static/mrbones.exe`: `cd4fc4b754e3621238e092ae75e78cfeccf708d07a7c146415a225f36961d469`. `dumpbin /dependents`: USER32, GDI32, WINMM, bcrypt e KERNEL32. A suíte integral não foi repetida nesta etapa; a comparação completa das transições com o Ymir permanece pendente.

### 2026-11-08 — diagnóstico de falha do dispositivo de áudio FMV

- O host anteriormente ignorava falhas ao iniciar o áudio de um FMV ou consultar o relógio `waveOut`, prosseguindo sem informar a ausência de som. Agora registra o erro, indica `[FMV audio unavailable]` no título durante a cena, libera o dispositivo após erro assíncrono e usa o relógio do sistema para prosseguir sem áudio. O aviso é limpo ao mudar de cena ou ao sair da abertura do menu. Os módulos de renderização protegidos não foram alterados.
- `test_fmv_transition` força uma taxa PCM inválida e verifica que o erro de inicialização é registrado e a solicitação não fica pendente. `test_movie_audio` verificou o áudio e o relógio em dispositivo disponível; os dois testes passaram em 84,00 s. A falha assíncrona física do dispositivo não foi induzida.
- Recompilado Release x64 `/MT`; SHA-256 `build-static/mrbones.exe`: `57cfd91902b7b9b5972e77d7464f3eeb3c9e4ff23092efd373e3a420af74e172`. `dumpbin /dependents` lista USER32, GDI32, WINMM, bcrypt e KERNEL32. A comparação integral dos FMVs e transições com Ymir continua pendente.

### 2026-11-08 — quadro de GUITAR comparado ao Ymir

- Em perfil isolado Ymir 0.4.1 com disco 1, a execução foi interrompida no carregador `0020B95C` e o índice em `00228970` ajustado temporariamente para 7. A execução apresentou uma cena do filme e depois retornou à tela de título; esse procedimento não reproduz uma vitória normal em GUITAR e não comprova o comportamento da fase jogável.
- Captura F12 `build-static/ymir-guitar-profile/screenshots/mr Bones [MK-81016]-20261108T224233.836.png`: reduzida de 640×448 para RGBA 320×224 por vizinho mais próximo. A sonda vinculada à `mbcore.lib` encontrou igualdade byte a byte com o intervalo 99 (índice zero) de `/07GUITAR/20AVI`; ambos SHA-256 `c8d30f83e58d728e403dce6718cfb28ad710a88d9ff6870af1ca42acdb8d0ec8`.
- `test_stage_runner` agora percorre o filme até esse quadro, confere a imagem apresentada contra a captura e verifica a interrupção por Start. O teste passou. O build Release x64 foi atualizado; incluiu alterações já existentes em `sidescrl_motion` de outra frente. SHA-256 atual de `build-static/mrbones.exe`: `084102f2fff548126bfbd7fdf577f0d90f751cfc1494a5ded2cb599aed0dedf1`. Nenhum módulo protegido foi alterado nesta etapa.
- A amostra não valida a fidelidade integral do filme, a sincronização audiovisual nem a ordem das transições em partida normal.

### 2026-11-08 — runtime estático em configurações novas

- Uma configuração Release x64 nova de CMake, sem opções extras, gerava `-MD`, apesar de o build existente usar `-MT` por configuração anterior. `CMakeLists.txt` agora define o runtime MSVC estático: `/MT` em Release e `/MTd` em Debug; o README foi atualizado. Os quatro módulos protegidos não foram alterados.
- Criado `build-mt-validation` sem cache prévio: a regra inicialmente mostrava `-MD`; após a correção mostrou `-MT`. O executável foi compilado e `dumpbin /dependents` listou somente USER32, GDI32, WINMM, bcrypt e KERNEL32; `--version` e `test_fmv_transition` passaram. A validação usou as bibliotecas e a árvore configurada do FFmpeg já existentes em `build-static`, portanto não valida sua reconstrução do zero.
- O build principal não exigiu recompilação adicional, pois já usava `-MT`. `test_fmv_transition`, `test_fmv_all` e `test_duck_avi` passaram 3/3 em 36,25 s. SHA-256 de `build-static/mrbones.exe`: `084102f2fff548126bfbd7fdf577f0d90f751cfc1494a5ded2cb599aed0dedf1`; do executável no diretório novo: `ade4ceb69da7e9c6fc6961da90091ceb0c5ea437f2ce4f0f536fbbe6a8232f09`. A diferença dos hashes não foi investigada como build reproduzível byte a byte.

### 2026-11-08 — reconstrução independente do FFmpeg estático

- Verificado SHA-256 `733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1` do arquivo oficial FFmpeg 7.1.1 já presente. Extraída uma árvore nova em `build-mt-validation/ffmpeg-7.1.1` e recompiladas bibliotecas estáticas com MSVC x64, `--extra-cflags=-MT`, AVI, TrueMotion1 e ADPCM IMA DK3; a configuração informou LGPL 2.1 ou posterior. A primeira tentativa falhou por ausência de `/usr/bin` no PATH do shell MSYS; corrigido no ambiente da tentativa seguinte.
- Um port separado foi configurado com `MB_FFMPEG_ROOT` e `MB_FFMPEG_SOURCE` apontando para essa reconstrução. `test_fmv_transition`, `test_fmv_all`, `test_stage_runner`, `test_special_phase` e `test_duck_avi` passaram 5/5 em 41,91 s. A saída PCM completa dos 20 FMVs apresentou os mesmos hashes SHA-256 do build principal; os testes incluem amostras visuais de GUITAR, FINALE e LOSE. `dumpbin` lista apenas USER32, GDI32, WINMM, bcrypt e KERNEL32.
- O executável de teste `build-mt-validation/mrbones.exe` tem SHA-256 `a2d7fce43cf3620e807d26b6793a02f9c42f36ec0cefc299b7dd8972ab8b351c`. O build principal e suas bibliotecas não foram substituídos. O README registra os comandos de reconstrução. Nenhum módulo protegido foi alterado. Ainda falta validação audiovisual integral das transições no Ymir.

### 2026-11-08 — tempos de áudio e vídeo nos dois discos

- Inspecionados os cabeçalhos dos 20 arquivos `/20AVI`: todos declaram `dwScale=1`, `dwRate=15` e `dwMicroSecPerFrame=66666`. Para os dados efetivamente decodificados, a duração nominal do vídeo calculada pelo port supera a duração PCM em 1,721 a 60,266 ms. Assim, nesses arquivos e por essa comparação de durações, a conclusão nominal de vídeo não antecede o final do áudio.
- O intervalo inteiro de 66.666 µs é inferior ao período exato de 1/15 s por 2/3 µs; no filme mais longo, FINALE (5.658 intervalos), isso representa aproximadamente 3,772 ms acumulados. Este cálculo não mede a latência de apresentação, jitter ou sincronização percebida com Ymir.
- `test_fmv_all` agora falha se um dos arquivos suportados tiver áudio nominalmente mais longo que o vídeo, condição que o host atual ainda não trata sem possível corte. O teste passou com os 20 arquivos (18,06 s). O executável principal não exigiu nova vinculação; nenhum módulo protegido foi alterado.

### 2026-11-08 — áudio além do último quadro

- `duck_avi_tick` encerrava a apresentação após o último intervalo de vídeo, mesmo que o PCM durasse mais. Agora mantém o último quadro até a duração nominal do áudio, arredondada para cima ao microssegundo; a interrupção explícita por Start continua usando o caminho anterior. Os 20 arquivos dos discos fornecidos têm áudio mais curto, conforme a seção anterior.
- Acrescentada regressão temporal com vídeo de 300 ms e áudio de 450 ms: o teste falhou antes e passou após a correção, verificando que a conclusão só ocorre ao fim do áudio. A restrição anterior em `test_fmv_all` que rejeitava áudio nominalmente mais longo foi retirada. Testes de áudio real, transições, os 20 FMVs, menu, fase especial e AVI passaram 6/6 em 110,46 s. O caso de áudio mais longo foi simulado; não houve comparação desse caso com hardware ou Ymir.
- Recompilado `build-static` Release x64 `/MT`; SHA-256 de `mrbones.exe`: `d31bb1beb81120e0dd6d85f85fc0712cb769ed128f3f906575e7a6e68c963658`. `dumpbin /dependents`: USER32, GDI32, WINMM, bcrypt e KERNEL32. Os quatro módulos protegidos não foram alterados.

### 2026-11-08 — interrupção de FMV com dispositivo de áudio

- Ampliado `test_movie_audio` para carregar os FMVs de GUITAR e LOSE como apresentações, iniciar seus PCMs no dispositivo Windows, aguardar ao menos 100 ms de reprodução, interromper cada apresentação com Start e encerrar o áudio. O teste exige que a liberação retorne em até um segundo e que o dispositivo consiga iniciar o áudio seguinte.
- `test_movie_audio` passou em 68,36 s: abertura completa 50.905 ms/764 quadros; LOSE completo 13.509 ms/203 quadros; FINALE nos primeiros 1.512 ms/23 quadros; interrupção e troca aprovadas. O teste usa o backend real `waveOut`, porém não grava a saída para quantificar o silêncio após o corte nem reproduz os eventos da janela do executável.
- Apenas o teste foi alterado; o executável Release x64 `/MT` já estava atualizado e não exigiu nova vinculação. Nenhum dos módulos protegidos foi alterado.

### 2026-11-08 — GUITAR: fase distinta do FMV

- `LoadFirstLevel` (`docs/ghidra-loader-backup.txt`, `0020b91c`) distingue o campo de fase em `0022896c` do campo de filme em `00228970`. A seleção anterior de filme 7 retornava ao menu; não era entrada na fase. No Ymir 0.4.1 com disco 1 e perfil isolado, no breakpoint `0020b95c`, alterado temporariamente `0022896c` de 3 para 7 e mantido `00228970=0`. O original apresentou o título `GUITAR SOLO` e a fase com Mr. Bones tocando guitarra diante de uma plateia. Captura: `build-static/ymir-guitar-stage-profile/screenshots/mr Bones [MK-81016]-20261108T230327.246.png`.
- O FMV `/07GUITAR/20AVI`, observado em execução separada e comparado na seção anterior, mostra uma cena cinematográfica diferente. A descompilação de `CEndLevel::Brain` de GUITAR aponta `_PlayDuckMovie(8)` após a vitória padrão. A seleção forçada não é uma partida completa e não valida o evento de vitória ou o áudio da fase.
- O port abria o AVI e seu PCM também durante a própria fase GUITAR, antes de reproduzir o filme na transição. `special_phase_load` deixou de carregar esse AVI no modo de fase GUITAR; o carregamento explícito como filme de transição foi preservado. Os visuais e o áudio da fase nativa continuam incompletos; não se afirma fidelidade da jogabilidade. Nenhum dos quatro módulos protegidos foi modificado.
- A nova asserção de ausência de FMV durante a fase falhou antes da mudança e passou depois. `test_movie_audio`, `test_fmv_transition`, `test_stage_runner` e `test_special_phase`: 4/4 passaram em 88,84 s; os testes de transição reproduzem o AVI e verificam seu encaminhamento. Recompilado Release x64 `/MT`: `build-static/mrbones.exe` SHA-256 `736285c4957d849b8a186e1db5036dca4af8b40347f36683bddd7c5624dd7fb0`; `dumpbin` lista USER32, GDI32, WINMM, bcrypt e KERNEL32.

### 2026-11-08 — DRUM: fase distinta do FMV

- No Ymir 0.4.1 com disco 1 e perfil isolado, no breakpoint `0020b95c`, ajustado temporariamente o campo de fase `0022896c` para 14 (`0x0e`) e mantido o campo de filme `00228970=0`. A fase original mostrou Mr. Bones cercado por tambores e efeitos elétricos. Captura F12: `build-static/ymir-drum-stage-profile/screenshots/mr Bones [MK-81016]-20261108T230802.330.png`.
- Extraído `/14DRUM/20AVI` para diagnóstico e inspecionado o quadro 100 com FFmpeg: mostra uma sequência visual diferente. `CEndLevel::Brain` em `00249770` indica estaticamente `_PlayDuckMovie(15)` após a vitória padrão; a vitória em partida normal não foi observada.
- A fase nativa de DRUM não carrega mais o AVI e seu PCM como fundo simultâneo; o caminho explícito de filme de transição permanece. A asserção de ausência do filme durante a fase falhou antes e passou após a alteração. A fase nativa continua incompleta em gráficos e áudio; não se afirma fidelidade da jogabilidade. Os quatro módulos protegidos não foram alterados.
- Recompilado Release Windows x64 `/MT`; `test_fmv_transition`, `test_stage_runner` e `test_special_phase` passaram 3/3 em 20,57 s. SHA-256 `build-static/mrbones.exe`: `b934c249e26652614a1557d7a5534d4e3ca7b341a76f5e7e9af96e6ef15f6f78`; `dumpbin /dependents` lista USER32, GDI32, WINMM, bcrypt e KERNEL32. O teste audiovisual real completo não foi repetido nesta etapa.

### 2026-11-08 — BIGBNS: separação da fase e do FMV

- No Ymir 0.4.1, disco 1 e perfil isolado, no breakpoint `0020b95c`, alterado temporariamente `0022896c` para 16 e mantido `00228970=0`. A fase BIGBONES iniciou e mostrou personagem e cenário de céu azul. Captura: `build-static/ymir-bigbns-stage-profile/screenshots/mr Bones [MK-81016]-20261108T231116.459.png`.
- O quadro 100 de `/16BIGBNS/20AVI`, extraído e inspecionado com FFmpeg, apresenta conteúdo visual diferente; o mapa consultado não identifica os símbolos do leitor DUCK. A análise estática do cérebro de término indica `_PlayDuckMovie(17)` no caminho padrão de vitória. A seleção foi forçada, e uma vitória normal não foi observada.
- A fase BIGBNS nativa deixou de carregar seu AVI e PCM cinematográficos como fundo; o caminho de filme da transição permanece. A regressão falhou antes da alteração e passou depois. A reconstrução gráfica e sonora da fase continua incompleta; não houve comparação audiovisual completa. Nenhum dos quatro módulos protegidos foi modificado.
- Recompilado Release x64 `/MT`; `test_fmv_transition`, `test_stage_runner` e `test_special_phase` passaram 3/3 em 19,60 s. SHA-256 de `build-static/mrbones.exe`: `603e265ae181b89af23c8f2b5aa52d9edd0409048f6a895719ddc49c73ddb42f`; `dumpbin` lista USER32, GDI32, WINMM, bcrypt e KERNEL32.

### 2026-11-08 — GENRM: separação da fase e do FMV

- No Ymir 0.4.1 com disco 2 e perfil isolado, no breakpoint `0020b95c`, ajustado temporariamente `0022896c=25` e mantido `00228970=0`. A fase mostrou personagem, inimigos e cenário circular. Captura: `build-static/ymir-genrm-stage-profile/screenshots/mr Bones [MK-81016]-20261108T231530.937.png`.
- Extraído `/25GENRM/20AVI` e inspecionado o quadro 100 com FFmpeg (`build-static/genrm-frame100.png`): cena cinematográfica distinta. A busca no mapa `GENROOM/A.MAP` não encontrou `MFP_`, `DUCKPLAY`, `DUCKFILE`, `DXL_` ou `20AVI`. `CEndLevel::Brain` indica estaticamente `_PlayDuckMovie(26)` após a vitória padrão; a vitória normal não foi observada.
- A fase GENRM nativa deixou de carregar o AVI e seu PCM como fundo. O caminho explícito de filme de transição permanece. A asserção de regressão falhou antes da alteração e passou depois. Gráficos e áudio da fase nativa permanecem incompletos, sem comparação audiovisual completa. Nenhum dos quatro módulos protegidos foi alterado.
- Recompilado Release Windows x64 `/MT`; `test_fmv_transition`, `test_stage_runner` e `test_special_phase` passaram 3/3 em 20,32 s. SHA-256 `build-static/mrbones.exe`: `9e9cbd971b27e2024417682d0bea8f83260f500fb526823308a2d91ad108d429`. `dumpbin /dependents` lista USER32, GDI32, WINMM, bcrypt e KERNEL32. O teste audiovisual completo não foi repetido nesta etapa.

### 2026-11-08 — FINALE completo no relógio de áudio Windows

- `test_movie_audio` ganhou opção `MB_FULL_FINALE=1` para percorrer o filme inteiro, mantendo a execução curta como padrão. A verificação acompanha os quadros esperados pelo relógio `waveOut`, o término do PCM e o último quadro, com RIGHT pressionado; usa o relógio de parede após o fim do áudio, como o host. A reprodução foi feita em tempo real.
- Comando: `$env:MB_FULL_FINALE='1'; ctest --test-dir build-static -C Release -V -R '^test_movie_audio$'`. Passou em 443,85 s: abertura 50.905 ms/764 quadros; LOSE 13.509 ms/203 quadros; FINALE 377.155 ms/5.658 de 5.658 quadros; interrupção e troca aprovadas. O teste não captura a saída física de áudio nem compara perceptivamente com o Ymir.
- Foi recompilado somente o teste, sem alterações no executável de produção ou nos quatro módulos protegidos. O executável Release x64 `/MT` permanece com SHA-256 `9e9cbd971b27e2024417682d0bea8f83260f500fb526823308a2d91ad108d429`; `dumpbin /dependents` lista USER32, GDI32, WINMM, bcrypt e KERNEL32. As transições em partida normal e a fidelidade audiovisual integral perante Ymir ainda não foram demonstradas.

### 2026-11-08 — continuidade estéreo do vídeo narrativo no Ymir

- Reanalisada a captura existente `build-static/ymir-story-loopback.wav` (55 s, estéreo a 44,1 kHz) contra `build-static/menu_audio.pcm`, extraído de `/03MENU/20AVI`. O script `build-static/compare-ymir-story-all.py` correlaciona 40 janelas consecutivas de um segundo, por canal, buscando alinhamento local de ±50 ms; os resultados individuais estão em `build-static/ymir-story-audio-full-comparison.csv`.
- As correlações normalizadas variaram de 0,997558 a 0,999961 no canal esquerdo e de 0,996755 a 0,999954 no direito. O deslocamento estimado entre fonte e gravação ficou entre 14,455986 e 14,456848 s, variação de aproximadamente 0,86 ms nas 40 janelas. O primeiro script precisou trocar SciPy indisponível por correlação FFT de NumPy; a segunda execução foi concluída.
- A medição cobre os primeiros 40 segundos do áudio do filme forçado no Ymir, não o filme inteiro nem a saída física do port nesta execução; também não mede sincronização simultânea de imagem e áudio. Nenhum código de produção ou módulo protegido foi alterado; mantém-se o Release x64 `/MT` documentado na seção anterior.

### 2026-11-08 — comparação estendida do áudio de abertura

- Reanalisada a gravação existente `build-static/ymir-opening-full.wav` (72 s, estéreo, 44.100 Hz) contra o PCM original de `/29INTRO/20AVI`. O script `build-static/compare-ymir-opening.py` alinhou 38 janelas consecutivas de um segundo, por canal, usando correlação FFT; o detalhe está em `build-static/ymir-opening-audio-comparison.csv`.
- Correlação normalizada: 0,996545–0,999754 no canal esquerdo e 0,996604–0,999766 no direito. O deslocamento estimado de 33,278413–33,279229 s variou cerca de 0,82 ms. Uma busca global por correlação bruta em trechos 5 e 15 s inicialmente localizou outras regiões de maior amplitude; a comparação registrada usa busca local em torno do alinhamento confirmado em 25 e 35 s. Os primeiros 38 s são cobertos; os últimos aproximadamente 13 s não cabem nesta captura.
- A gravação não mede imagem e áudio simultaneamente e não demonstra a sequência inteira nem o comportamento de entrada. Os logotipos animados já foram identificados no próprio AVI (seção de identificação anterior); a tela de licenciamento do Saturn é anterior e distinta. Nenhum código de produção ou módulo protegido foi alterado.

### 2026-11-08 — áudio completo da abertura original

- Gravados 110 s de loopback estéreo a 44,1 kHz em `build-static/ymir-opening-complete-loopback.wav`, com Ymir 0.4.1, disco 1 e perfil isolado `build-static/ymir-opening-capture-profile`; o emulador foi encerrado após a captura. A inicialização original consumiu parte da gravação; a abertura inteira coube nela.
- `build-static/compare-ymir-opening-complete.py` alinhou o PCM de `/29INTRO/20AVI` à captura, por janela de um segundo e canal. As 47 janelas com sinal tiveram correlações de 0,994027–0,999908 no esquerdo e 0,994338–0,999915 no direito; os deslocamentos ficaram entre 28,202585 e 28,203605 s. Resultados individuais: `build-static/ymir-opening-complete-audio-comparison.csv`.
- O filme contém 50,905488 s de PCM. Os 3,905488 s finais são silêncio em ambos os canais, tanto na fonte como na captura, conforme `build-static/opening-final-silence.py`. O alinhamento é aproximado e inclui as latências do dispositivo e do loopback; a correlação não demonstra igualdade amostra a amostra.
- A captura comprova esta comparação do áudio da abertura inteira, não a sincronização simultânea com a imagem nem a sequência de vídeo completa quadro a quadro. Nenhum código de produção ou módulo protegido foi alterado; o Release estático permanece o documentado anteriormente.

### 2026-11-08 — calibração do relógio waveOut e loopback

- O sinal senoidal gerado e capturado pela biblioteca SoundCard (`build-static/loopback-calibration.py`, saída `loopback-calibration.wav`) apareceu no arquivo de loopback aproximadamente 27 ms antes do instante da chamada de reprodução, com resolução de detecção de 10 ms. Essa medição não calibra a latência acústica do fone.
- Recompilada uma cópia de diagnóstico do host atual (`build-static/fmv-trace-current.exe`) para registrar instante de pintura, quadro e posição `waveOut`; o executável distribuído não foi alterado. Na segunda captura de 12 s, sem avisos de descontinuidade, a correlação de PCM foi 0,9999998. Em quatro pontos, a posição estimada pelo loopback estava cerca de 242–243 ms à frente do relógio `waveOut` relatado; a primeira tentativa teve descontinuidades e foi descartada.
- Para isolar a reprodução do decodificador e dos quadros, `build-static/clock-signal.exe` usou o mesmo `host_audio` com 6 s de PCM pseudoaleatório conhecido, enquanto `clock-signal-capture.py` capturou 8 s sem descontinuidades. Correlação arredondada a 1,0 nas quatro janelas analisadas; `clock-signal-compare.py` encontrou diferença de 234–235 ms entre a posição inferida no loopback e o relógio `waveOut`, reproduzindo quase toda a diferença do teste do filme. Os dados estão em `clock-signal.log`, `clock-signal-loopback.wav` e `clock-signal.pcm`.
- As medições relacionam o relógio consultado e a captura digital neste dispositivo Bluetooth; o loopback não mede quando o som chega fisicamente ao ouvido nem estabelece o sincronismo percebido. Portanto, não se aplicou uma compensação arbitrária ao port. Falta medição audiovisual externa/acústica antes de alterar o relógio de reprodução. Nenhum módulo protegido ou código de produção foi alterado.

### 2026-11-08 — relógio racional dos filmes AVI

- O leitor usava somente `dwMicroSecPerFrame=66666`, arredondado para baixo, apesar de os fluxos do jogo declararem `dwScale=1` e `dwRate=15`. `duck_avi` agora lê a razão do `strh` e distribui a fração de microssegundo entre intervalos; quando essa razão não está disponível usa o intervalo do cabeçalho principal. A duração para manter o último quadro enquanto o áudio termina também usa a razão exata.
- Acrescentadas regressões para 15 intervalos completos em exatamente 1.000.000 µs e para áudio mais longo do que o vídeo com taxa racional. Ajustados relógios simulados e expectativas dos testes de áudio ao novo período; os testes com quadros e áudios do disco continuam usando os dados originais. A primeira execução revelou testes simulados que avançavam apenas 66.666 µs por vez; foram atualizados para cobrir o intervalo arredondado para cima.
- `ctest --test-dir build-static -C Release --output-on-failure`: 52/52 passaram em 137,16 s, inclusive abertura e LOSE com dispositivo de áudio e os 20 FMVs. A regressão adicional de áudio mais longo foi compilada depois dessa suíte e `test_duck_avi` passou separadamente em 4,27 s. A execução anterior do FINALE inteiro em tempo real precedeu esta alteração; o filme completo continua coberto por relógio simulado.
- Recompilado Release Windows x64 `/MT` em `build-static/mrbones.exe`; SHA-256 `4a7e7b0c39b7d75bb472b544e861410e8d530996ddf7a14c2b34314cdd489a9a`. `dumpbin /dependents` lista USER32, GDI32, WINMM, bcrypt e KERNEL32. Nenhum dos quatro módulos protegidos foi alterado. A comparação audiovisual externa e as transições normais pelo jogo original permanecem pendentes.

### 2026-11-08 — FINALE completo após ajuste de tempo racional

- Acrescentada em `test_duck_avi` uma regressão de 5.658 intervalos a 15 fps: aos 377.199.999 µs foram consumidos 5.657 intervalos e, no microssegundo seguinte, o 5.658º. O teste passou em 4,69 s. É um relógio simulado com quadros vazios, destinado ao limite temporal, não à fidelidade visual.
- Reexecutado `MB_FULL_FINALE=1` no dispositivo `waveOut` após a alteração do temporizador: `test_movie_audio` passou em 443,71 s; abertura 50.905 ms/764 quadros, LOSE 13.509 ms/203 quadros e FINALE 377.155 ms/5.658 quadros, além de interrupção e troca de áudio. O teste confere posição do áudio e quadro decodificado, não saída audiovisual capturada ou percepção acústica.
- Somente o teste foi alterado nesta etapa. O executável estático permanece o da seção anterior; não houve modificações nos quatro módulos protegidos. As comparações externas de sincronização e transições normais ainda não estão comprovadas.

### 2026-11-08 — ressalva sobre a rota de FINALE

- No Ymir com disco 2 e perfil `build-static/ymir-finale-stage-profile`, a execução chegou ao menu original. Não foi obtida entrada controlada na fase: houve interação simultânea na janela, portanto a observação foi interrompida e a sessão deixada intacta. A captura anterior em que `00228970=27` selecionava o filme, não a fase; sua descrição como seleção direta da fase em uma seção anterior deve ser lida com essa correção.
- Extraído `/27FINALE/03CODE` para `build-static/finale-route-code.bin`: SHA-256 `19962c17f9ca3c467b05229956e98058df72d05bf083e6c30006a05f8f59528b`. Lidos os ponteiros SH-2 big-endian com base `00232000`: literal `0028ce00` contém `0020b61c` (PlayDuckMovie), `0028cdf8` contém `0020b714`, e `0028cdfc` contém `0020b4ac`. A descompilação em `docs/ghidra-fmv-brains/27FINALE.txt` indica o primeiro no ramo padrão de vitória. O mapa FINALE contém cérebro de `CFBones`, controles e `RingDefinitionTable`, e não apresentou os símbolos MFP_/DUCKPLAY/DUCKFILE/DXL_/20AVI na busca.
- Esses dados reforçam que a fase final e o filme posterior devem ser tratados separadamente, mas não são captura de sua transição nem demonstram o visual ou o áudio da fase. O port atual ainda usa o filme durante FINALE; não foi alterado sem validação da fase ou reconstrução de sua jogabilidade. Não houve alteração do executável ou dos módulos protegidos.

### 2026-11-08 — validação do build a partir de C:

- Os comandos de CTest usavam `F:/MrBonesPort` para os discos, manifesto e dados de teste. Substituídos por `${CMAKE_SOURCE_DIR}` e `${CMAKE_BINARY_DIR}` no CMake; quatro testes passaram a usar diretórios de teste relativos ao diretório de execução do CTest, sem depender da unidade F:. Os caminhos relativos se aplicam a dados de teste, não aos saves do usuário.
- Configurada uma árvore nova com `cmake -S C:\Users\jneom\Neomaster\mr-bones-port -B C:\Users\jneom\Neomaster\mr-bones-port\build-cpath-validation -G Ninja -DCMAKE_BUILD_TYPE=Release`. O cache e o `CTestTestfile.cmake` apontam para C:, e as regras de compilação usam `-MT`. `test_fmv_transition`, `test_fmv_all`, `test_nvm_shadow`, `test_menu_phase`, `test_stage_progress` e `test_duck_avi` passaram 6/6 em 46,37 s. `test_movie_audio` passou separadamente em 67,64 s, com abertura e LOSE completas, FINALE por 1,5 s e interrupção; não houve SKIP de dispositivo.
- Recompilado também `build-static`; `test_fmv_transition` e `test_duck_avi` passaram 2/2 em 18,62 s. SHA-256 de `build-static/mrbones.exe`: `1c69691ae794e869ce1bb3b5f0a4065d179b8bfe11c067d294c0ef22db3eaabe`. O build novo tem SHA-256 `7670b84a1518c799022a2de6d13bb1deb96f98616543c11e2f88ca43689234cd`; `dumpbin` nele lista USER32, GDI32, WINMM, bcrypt e KERNEL32. A recompilação incorporou alterações já presentes na árvore compartilhada; a diferença de hash não foi analisada como compilação reproduzível.
- As bibliotecas FFmpeg já existentes foram reutilizadas; esta execução não é reconstrução do FFmpeg do zero nem instalação Windows limpa. A tentativa de observar a fase FINALE original foi interrompida por interação simultânea na janela do Ymir; a associação do filme à transição normal continua pendente. Nenhum dos quatro módulos protegidos foi alterado nesta etapa.

### 2026-11-09 — falha explícita para FMV inválido em FINALE

- Corrigido o carregamento de FINALE: enquanto a cena atual depender de um AVI, um arquivo ausente ou inválido encerra o carregamento com diagnóstico; `stage_runner` não tenta substituí-lo por uma apresentação alternativa. A correção não reconstrói a fase final nem comprova a ordem original da transição.
- Criada uma cópia de teste da faixa de dados do disco 2, `build-static/finale-invalid-fixture.bin`, com o cabeçalho RIFF de `/27FINALE/20AVI` alterado; o disco original não foi modificado. A regressão `MB_DISC2_CORRUPT_FINALE` falhou antes da correção e passou depois, exigindo erro e diagnóstico `not RIFF/AVI`.
- Em 09/11, `cmake --build build-static --config Release` informou `ninja: no work to do`: o executável já havia sido vinculado depois da tentativa interrompida, anteriormente impedida por arquivo em uso. Com a fixture de FINALE inválido configurada, `test_fmv_transition`, `test_fmv_all`, `test_stage_runner`, `test_special_phase` e `test_duck_avi` passaram 5/5 em 49,25 s; o teste dos 20 filmes usou os discos válidos.
- Confirmados PE32+ x64, compilação `/MT` e importações USER32, GDI32, WINMM, bcrypt e KERNEL32. SHA-256 de `build-static/mrbones.exe`: `d0796d3274b7bfd3c92939a07d5eb11293e07f44863013dd02c43a9a51993006`. Não houve alteração dos quatro módulos protegidos nesta etapa. A transição normal de FINALE no original e a sincronização audiovisual percebida permanecem sem validação completa.

### 2026-11-09 — observação direta da fase FINALE

- No Ymir 0.4.1, disco 2, perfil isolado `build-static/ymir-finale-review`, ativado rastreamento desde a inicialização. No breakpoint `0020b95c`, alterado o campo de fase `0022896c` para 27 (`0x1b`) e mantido o campo de filme `00228970=0`. Essa seleção forçada mostrou Mr. Bones com guitarra numa arena circular de caveiras e efeitos; captura F12 `build-static/ymir-finale-review/screenshots/mr Bones [MK-81016]-20261109T133600.304.png`.
- Sem comandos de jogo, após o trecho observado a tela escureceu e a execução retornou ao menu. Não se observou vitória nem foi identificada com segurança cada tela intermediária; o retorno ao menu não prova a rota de vitória. A seleção direta não equivale a uma partida completa. O carregamento isolado anterior de `/27FINALE/20AVI` é referência do filme, não da arena.
- Diferença confirmada: `special_phase` trata FINALE como apresentação de AVI e o port entra diretamente no filme, sem reproduzir a arena, lógica, controles ou áudio da fase. O cérebro original `CFBones` e a condição de vitória ainda não foram reconstruídos; não foi alterado o fluxo para uma implementação genérica que simulasse vitória. O teste de campanha com relógio simulado não valida essa parte.
- Reexecutado `cmake --build build-static --config Release`: Ninja informou que não havia trabalho pendente. Com a fixture de AVI FINALE inválido, `test_fmv_transition`, `test_stage_runner` e `test_special_phase` passaram 3/3 em 20,84 s. O executável estático permanece SHA-256 `d0796d3274b7bfd3c92939a07d5eb11293e07f44863013dd02c43a9a51993006`. Nenhum módulo protegido ou código de produção foi modificado nesta etapa; a correção da fase e a comparação da transição de vitória seguem abertas.

### 2026-11-09 — fluxo de áudio declarado sem amostras

- `duck_avi_open` aceitava um AVI com cabeçalho de áudio, mas sem nenhum bloco decodificável, e a reprodução seguia sem som. O carregador agora rejeita esse caso com `audio stream has no samples`; um AVI que não declara fluxo de áudio continua aceito como vídeo silencioso. A alteração não trata áudio válido contendo amostras de silêncio como erro.
- A regressão em `test_duck_avi` usa uma cópia em memória do AVI de INTRO, substitui identificadores de blocos de áudio e verifica o erro; antes da correção, falhou porque o arquivo era aceito. Em seguida remove o cabeçalho de áudio da cópia e confirma que o vídeo sem fluxo de áudio é aceito. O disco original não foi alterado.
- Recompilado `build-static` Release x64 `/MT`; `test_fmv_transition`, `test_duck_no_ffmpeg`, `test_fmv_all`, `test_special_phase` e `test_duck_avi` passaram 5/5 em 42,32 s. A segunda execução de `test_duck_avi`, incluindo o caso sem fluxo declarado, passou em 4,97 s. SHA-256 de `build-static/mrbones.exe`: `70da3606d009c12f2efc1e218d8048d062b3e9e9e9bbbd106780b6fe82d6d50f`; `dumpbin` confirma PE32+ x64 e importações USER32, GDI32, WINMM, bcrypt e KERNEL32. Nenhum dos quatro módulos protegidos foi alterado.
- Tentado preparar no Ymir um breakpoint no cérebro de término de FINALE para investigar seu ramo padrão. O controle da janela não retornou a instância alvo e a área de trabalho estava em uso; a manipulação da memória e a observação do ramo não foram realizadas. A transição de vitória continua sem validação em execução.

### 2026-11-09 — ramo de vitória de FINALE forçado no Ymir

- No Ymir 0.4.1 com disco 2 e perfil isolado `build-static/ymir-finale-winref`, selecionada a fase 27 no breakpoint `0020b95c`, mantendo o campo de filme em zero. Sem entrada de jogo, o breakpoint em `CEndLevel::Brain` (`0028ccd0`) foi atingido com `R4=06026a9c`: resultado em `06026ae0` era `00000001`, e os campos `+0x48` e `+0x4c` eram `ffffffff`.
- Alterado apenas o resultado em RAM de 1 para 0 e retomada a execução: o original apresentou o filme de FINALE. Captura F12 `build-static/ymir-finale-winref/screenshots/mr Bones [MK-81016]-20261109T134500.548.png`, convertida de 640×448 para 320×224 por vizinho mais próximo, corresponde ao intervalo 288 (índice zero) do `/27FINALE/20AVI` decodificado pelo port, com MSE RGB zero e SHA-256 RGBA de ambos `4a00986965f279613fabd9567a0e462176ba08bfc128fe903377df1cd1b9e446`. A captura e os arquivos de comparação são evidências locais e não são incluídos no produto.
- `test_special_phase` agora verifica também essa imagem do filme, além da captura anterior. `test_fmv_transition`, `test_special_phase` e `test_duck_avi` passaram 3/3 em 24,91 s. Apenas o teste foi compilado; o executável estático permanece SHA-256 `70da3606d009c12f2efc1e218d8048d062b3e9e9e9bbbd106780b6fe82d6d50f`. Nenhum módulo protegido foi alterado.
- O experimento confirma o ramo de filme sob resultado forçado, não uma vitória jogada nem seu gatilho. O port ainda inicia o AVI diretamente ao entrar em FINALE; a arena e a transição normal continuam pendentes. A amostra não valida a duração ou sincronização audiovisual completa do original.
