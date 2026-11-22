# Mr. Bones Port — Documento de Game Design e Plano de Evolução

> **Propósito deste documento:** mapear com precisão o que o port C já implementa em termos de jogabilidade, quais mecânicas clássicas de *Mr. Bones* ainda faltam em cima da infraestrutura pronta (VDP1/VDP2, `15PAN`, `04STXT`, `05SND1`, CD-DA, `duck_avi`, `NvmShadow`), quais restrições de testes precisam ser preservadas e em **quais etapas práticas** transformar o port de "demonstração navegável" em **jogo completo e coeso**.

---

## 1. Diagnóstico Atual: Onde o Port Está Hoje

A base técnica do port (leitura ISO 2 discos, catálogo `DirNameTable`, vídeo TrueMotion `20AVI`/`19DUCK`, música CD-DA + SFX `05SND1` PCM/SCSP, rolagem VDP2 `07ACDB..14DTIL` + `06AMAP`, colisão `15PAN`, sprites `04STXT`, save `nvmshadow.bin` e campanha de 24 fases) já está funcional (`docs/coverage.md`, `src/tools/mrbones.c`).

Do ponto de vista de **Game Design**, porém, quase todas as fases hoje compartilham um loop mínimo de travessia:

1. **Poucos verbos ativos:** o jogador basicamente **anda (`LEFT`/`RIGHT`)** e **pula (`B`/`X`)**. Os botões **A (`Z`)** e **C (`C`)** ainda não possuem ataque Blues, disparo ou ação ofensiva em `runway_phase`, `sidescrl_phase` ou `SPECIAL_MODE_ACTION` (embora `runway_phase.c` já registre os eventos BBS `MSG_C_A = 0x00010600` / `MSG_C_B = 0x00020600` / `SFX_C`).
2. **Inimigos sem HP decrementado nem conversão:** `stage_enemy` inicializa `ent->hp` (`1`, `3` em `LORD`/`RED`/`ZOMBIE`, `5` no boss de `FINALE`), mas **nenhuma função atual causa dano a inimigos** nem os converte.
3. **Condições de vitória curtas demais e homogêneas:**
   - `runway_phase.c`: `RUNWAY` usa `START_X = 40px` até `FINISH_X = 280px` (`240px`) com `stage_enemies_init_runway_rom`, mas os outros 6 estágios `generic_pan` (`LOGS`, `ICE1`, `ICE2`, `MOAT`, `RIDE`, `STAING`) terminam em apenas **`160px`** (`START_X + 120px`) e recebem `stage_enemies_init_runway` (vários inimigos em `182..238px` ficam além da linha de chegada). Além disso, `stage_enemies_step(..., 0, 0)` roda com **`apply_hits = 0`** (só as 3 zonas de `runway_hazards` aplicam dano; `CThrown` e corpos de inimigos não tiram Skeletism).
   - `sidescrl_phase.c`: `goal_px` é truncado em no máximo **`580px`** (`clamp(scroll_w - 60, 260, 580)`), mesmo quando `strip_w` vai até `1280px` (e mapas CDB chegam a `1864+` px); todas as 10 fases terminam apenas cruzando `finish_x`, inclusive o chefe `26LAFF`.
   - `special_phase.c` (`SPECIAL_MODE_ACTION` — `16BIGBNS` e `25GENRM`): `START_X = 40px` e `FINISH_X = START_X + 120px = 160px`; basta caminhar `120px` para vencer, sem precisar lidar com `BUMPER` ou neutralizar `CRed`/`CZap`.
4. **Skeletism sem cura em campo:** `runway_skel_add()` existe em `src/core/runway_skel.c`, mas não é chamado em nenhuma fase, apesar de os sprites de `HEALTH` já estarem localizados em `04STXT` de múltiplas fases.

---

## 2. Pilares de Design para *Mr. Bones*

| Pilar | Núcleo no Jogo | Tradução para o Port C |
|---|---|---|
| **1. Skeletism (`runway_skel`)** | Os ossos são ao mesmo tempo barra de vida (`14` segmentos, `RUNWAY_SKEL_MAX = 0x20000`) e risco constante de desmembramento. | Dano coerente de inimigos/projéteis (`apply_hits`), knockback + invulnerabilidade (`RUNWAY_HIT_TIMEOUT = 20`), queda em buracos e cura real com pickups `HEALTH`. |
| **2. Poder do Blues (A / C)** | Mr. Bones derrota e converte esqueletos hostis (vermelhos) usando música/Blues azul, em vez de armas convencionais. | Ação ativa no botão **C** (e variação direcional/Blues em **A**): pulso/raio curto/médio que reduz `ent->hp`, gera feedback `poof_blue` + SFX e converte/desativa inimigos. |
| **3. Identidade por Fase** | Quase nenhuma fase repete exatamente as regras da anterior (corrida em profundidade, plataforma de cemitério, ritmo FMV, pinball, magnetismo, chefes). | Especializar regras sobre os 3 motores existentes (`runway_phase`, `sidescrl_phase`, `special_phase`) usando tabelas por fase em vez de criar 22 engines separados. |
| **4. Ritmo & Espetáculo Integrados** | Fases musicais (`GUITAR`, `DRUM`) e arenas climáticas (`BIGBNS`, `GENRM`, `FINALE`) marcam a progressão e ligam as FMVs `20AVI`. | Combo/streak e precisão de janela no ritmo, arenas com objetivos claros de sobrevivência/conversão e duelo final integrado em `27FINALE`. |

---

## 3. Controles: Mapeamento Atual vs. Alvo

Mapeamento físico definido em [`saturn_pad.h`](file:///F:/MrBonesPort/src/core/saturn_pad.h), [`saturn_pad.c`](file:///F:/MrBonesPort/src/core/saturn_pad.c) e [`host_input.c`](file:///F:/MrBonesPort/src/platform/host_input.c):

| Saturn | Teclado (Host) | Bit Host | Máscara Saturn | Uso Atual | Alvo de Game Design |
|---|---|---|---|---|---|
| `LEFT` / `RIGHT` | `Setas` / `A` `D` | `1<<0` / `1<<1` | `0x4000` / `0x8000` | Mover em todas as fases; `RIGHT` também avança `RHYTHM`/`PRESENT` em testes | Movimento e mira direcional (esquerda/direita). No `RHYTHM`, manter `RIGHT` como strum ou compatibilidade controlada. |
| `UP` / `DOWN` | `Setas` / `W` `S` | `1<<2` / `1<<3` | `0x1000` / `0x2000` | `DOWN` ativa *Tall/Crouch* (`MSG_DOWN`) em `runway_phase`; mira vertical no `FINALE`; navegação em `MENU` | `DOWN`: agachar/esticar (*Tall/Duck*) também em `sidescrl_phase` e `ACTION`; `UP`: olhar/mirar para cima / interagir com portas/escadas onde aplicável. |
| `A` | `Z` | `1<<4` | `0x0400` | Nota vermelha no `RHYTHM`; tiro azul no `FINALE`; seleção em `MENU` | Habilidade secundária / Blues de área ou acorde especial (gasto moderado ou cooldown curto); mantém nota `A` no `RHYTHM`. |
| `B` | `X` | `1<<5` | `0x0100` | **Pulo** em `runway_phase`, `sidescrl_phase` e `ACTION`; nota ouro no `RHYTHM`; tiro no `FINALE` | **Pulo padrão** em todas as fases de plataforma/corrida/arena (com controle variável de altura ou coyote/buffer leve); mantém nota `B` no `RHYTHM`. |
| `C` | `C` | `1<<6` | `0x0200` | Posta `MSG_C_A`/`MSG_C_B` + `SFX_C` em `runway_phase` (sem efeito no mundo); nota ciano no `RHYTHM`; tiro no `FINALE` | **Ataque principal (Blues Bolt / Guitar Chord)** em `runway_phase`, `sidescrl_phase` e `ACTION`; no `FINALE`, disparo principal direcional. |
| `START` | `Enter` | `1<<7` | `0x0800` | Pausa gameplay (`RUNWAY`, `SIDESCRL`, `RHYTHM`, `ACTION`); pula FMV/intro | Pausa com menu rápido (Continuar / Reiniciar Fase / Voltar ao Menu em etapas futuras). |
| `X` `Y` `Z` `L` `R` | `V` `B` `N` `Q` `E` | `1<<8..12` | `0x0040..0x0080` | Mapeados no pad sem uso no gameplay | Reserva para atalhos de câmera/debug ou seleção direta de nota no modo ritmo de 6 botões, sem exigir 6 botões do jogador casual. |

---

## 4. Matriz Completa da Campanha (24 Fases + Meta)

Baseada em `k_dir_names` e `build_campaign` em [`stage_catalog.c`](file:///F:/MrBonesPort/src/core/stage_catalog.c), [`stage_progress.h`](file:///F:/MrBonesPort/src/core/stage_progress.h), [`docs/coverage.md`](file:///F:/MrBonesPort/docs/coverage.md) e [`FRONTES-RE.md`](file:///F:/MrBonesPort/build-static/ymir-visual-ref/probe-out/FRONTES-RE.md):

### 4.1 Meta-fases e Transições

| Diretório ISO | Papel Atual | Gap de Design | Alvo de Design |
|---|---|---|---|
| `/03MENU` | Title (`20AVI`/`02TITL`), Start, Options (Music/Stereo/Volume), Level Select (`menu_phase_level_selectable`: 13 fases com IDs `04..10` e `18..23`, filtradas das 18 entradas em `docs/ghidra-menu-level-table.txt`). | Falta feedback visual de conclusão (`+1` / disco 1 vs 2), indicador claro de `Continue` a partir do último estágio salvo (`NvmShadow +0x1c`), e som/cursor mais responsivo. | Exibir progresso por disco, confirmar seleção de nível com nome/ícone da fase e manter opções persistidas em `mrbones.ini` sem sujar `nvmshadow.bin`. |
| `/28LOSE` | `SPECIAL_MODE_PRESENT`: exibe `20AVI` + `19DUCK` de derrota e retorna `STAGE_ROUTE_MENU`. | Transição correta; falta transição imediata do jogador com sensação de Game Over (freeze/desmontagem breve antes de chamar `/28LOSE`). | Pré-animação curta de queda de ossos (`skel.died`) por ~45 frames antes do corte para `/28LOSE`. |
| `/29INTRO` | Abertura carregada pelo `menu_phase` (`20AVI`/`02TITL`/`19DUCK`). | Completa para abertura; mantida. | Preservar skip seguro com `START` (`check_mode` verifica que `A` não pula abertura). |
| `/30MAP` | Listado em `k_dir_names` como `STAGE_KIND_META`, sem tela própria ativa. | Mapa/resumo de campanha não exibido entre blocos de fases. | (Opcional em etapa tardia) Tela de progresso visual entre fases usando recursos de `/30MAP` ou resumo de blocos. |

### 4.2 As 24 Fases da Campanha (`04RUNWAY` .. `27FINALE`)

| # | Fase | Motor Atual (`stage_kind`) | O que Existe Hoje | Lacuna Principal de Design | Alvo de Gameplay |
|---:|---|---|---|---|---|
| 01 | `04RUNWAY` | `runway_phase` (`RUNWAY`) | `15PAN` + colisão live 40×112, `Tall` (`DOWN`), 6x `GANG`/`LORD`/`POOF` visuais (`04STXT`), `3` zonas em `runway_hazards`, percurso `40..280px`, **`apply_hits=0`**. | Inimigos e ossos arremessados (`CThrown`) não ferem Bones; percurso muito curto; botão `C` não contra-ataca. | Ligar `CThrown`/colisão física calibrada, usar `C` (Blues) para repelir/quebrar `CThrown` e estender desafio até saída real da pista. |
| 02 | `05MAUSO` | `sidescrl_phase` (`SIDE`) | `scrl_map` multi-plano com `base_y {0,-90,0,-100}`, `06AMAP`, `LORD` (`0x2c240`) + `GANG`, **`goal_px <= 580px`**. | Apenas `LORD` + `GANG` genéricos até `580px`; sem ataque nem variação vertical real do cemitério. | Usar largura completa do mapa (`strip_w`), spawn variado sobre relevo `floor_py`, combate Blues com `C` e pickup `HEALTH`. |
| 03 | `06VALLEY` | `sidescrl_phase` (`SIDE`) | `valley_skel[13]` (`04STXT`), parallax `0x10000/0x06000/0x03000/0x10000`, corte para FMV `/07GUITAR`. | Inimigos reusam mesma IA de patrulha curta; sem risco de abismos nem ação de `C`. | Encontro escalonado com esqueletos da colina (`valley_skel`), degraus/buracos reais e combate com `C`. |
| 04 | `07GUITAR` | `special_phase` (`RHYTHM`) | Vídeo `20AVI`, 32 notas `k_guitar_notes` (período 16), 4 trilhas (`A/B/C/RIGHT`), ganha com `hits >= 8`, perde com `misses >= 12`. | `8` acertos terminam o solo rápido demais; sem combo/streak; errar tira metade do dano padrão mas não há cura rítmica. | Sistema de combo, bônus de Skeletism em sequências perfeitas, janela visual clara (*Note Highway*) e opção de meta de notas compatível com solo mais longo. |
| 05 | `08BAT1` | `sidescrl_phase` (`SIDE`) | `STAGE_ENEMY_BAT` + `STAGE_ENEMY_ZOMBIE` (`04STXT` exclusivos), `base_y=0`, corte para `BAT2`. | Morcegos só patrulham horizontalmente (`patrol()`) em Y fixo visual com hover simples; não mergulham sobre Bones. | IA de mergulho (*swoop*) para `BAT` quando Bones se aproxima em X, forçando `Tall`/agachar ou pulo/Blues no timing certo. |
| 06 | `09BAT2` | `sidescrl_phase` (`SIDE`) | Mesmo motor e inimigos de `BAT1`. | Idêntica a `BAT1` na prática, mudando apenas o cenário `scrl_map`. | Aumentar cadência/densidade de mergulhos, usar faixas em desnível e incluir poofs azuis/vermelhos diferenciados (`poofs.o`). |
| 07 | `10BAT3` | `sidescrl_phase` (`SIDE`) | Mesmo motor de `BAT1/2` (MAP cita `CBlueSparkle`). | Idêntica a `BAT1/2`, sem clímax de saída da floresta. | Variação de velocidade de morcegos + encontro final antes da ponte/rio (`11LOGS`), exigindo combate + esquiva. |
| 08 | `11LOGS` | `runway_phase` (`RUNWAY`) | `15PAN` + sprites `bluepoof`/`buttrfly` + zonas em `70..155px`; `finish_x = 160px`; `04STXT` tem `HEALTH` em `0x0A090` (não carregado). | Percurso de só `120px` (`generic_pan`); sem física de troncos/correnteza. | Física de plataforma móvel / correnteza (deriva leve ou zonas de água que exigem saltos em sequência), percurso estendido aos quadros de `15PAN` + pickup `HEALTH` `0x0A090`. |
| 09 | `12TROLL1` | `STAGE_KIND_EMPTY` | Sem `03CODE` real (stub); pulada por `stage_progress_skip_empty`. | Não é jogável no disco (stub original). | Manter como `EMPTY` ignorada automaticamente no avanço de campanha. |
| 10 | `13TROLL2` | `STAGE_KIND_EMPTY` | Sem `03CODE` real (stub); pulada por `stage_progress_skip_empty`. | Idem `12TROLL1`. | Manter como `EMPTY` ignorada automaticamente no avanço de campanha. |
| 11 | `14DRUM` | `special_phase` (`RHYTHM`) | 48 notas reais de `_DrumSequence` (`0x0026b58c`, período 12), vídeo `20AVI`, `hits >= 8` vence. | Só usa 8 notas para vencer; mesmo HUD/regras simples do `GUITAR`. | Distinguir leitura/cadência de bateria (`period=12`, acordes simultâneos vistos em `{0u,0u}/{0u,3u}` na tabela real), contagem de combo e feedback por tambor. |
| 12 | `15GLASS` | `sidescrl_phase` (`SIDE`) | `glass_skull[16]` (`04STXT`), parallax `{0x10000,0,0,0x10000}`, corte pós-`DRUM`. | Caveiras flutuantes se comportam igual a soldados em terra; sem desafio aéreo nem pisos quebradiços/estreitos. | Movimento senoidal amplo das caveiras (`glass_skull`), plataformas estreitas e necessidade de ataque Blues aéreo (pulo + `C`). |
| 13 | `16BIGBNS` | `special_phase` (`ACTION`) | Backdrop `scrl_map` + dome `15PAN` (Bank 1), `bigbns_bumper` + `redpoof` + `LORD`, **`FINISH_X = 160px`**, `apply_hits = 1`. | **Basta andar 120px para direita para vencer**; `BUMPER` não rebate o jogador; `HEALTH` `0x0BFE0` ainda fora do gameplay. | Transformar em arena fechada/pinball-esqueleto: `BUMPER` repele Bones (`vx`/`vy`), e a saída só abre após destruir/converter os alvos ou sobreviver ao contador, com `HEALTH` `0x0BFE0`. |
| 14 | `17VORTEX` | `sidescrl_phase` (`SIDE`) | `vortex_blue[8]`/`vortex_red[8]` (`04STXT`), corte para `18RIDE`. | Pares azul/vermelho são apenas cosméticos (`GANG` vs `LORD`). | Diferenciar polaridade: `vortex_red` persegue/causa mais dano; `vortex_blue` energiza/cura ou exige pulso `C` específico; ajuste de inércia de vórtice opcional. |
| 15 | `18RIDE` | `runway_phase` (`RUNWAY`) | `15PAN` com zonas `55..155px`; transição FMV para `19VILLAGE` (D2 ou swap). | Anda a pé em velocidade baixa idêntica a `RUNWAY`, sem sensação de veículo/descida rápida. | Modo **auto-scroll / impulso alto** (velocidade base maior ao avançar, freada leve com `LEFT`, foco total em timing de pulo e `Tall`). |
| 16 | `19VILLAGE` | `sidescrl_phase` (`SIDE`) | `vil_mtrspid` + `vil_spider`, `04STXT` completo no D2 contém também `BRIDGE`, `HAND`, `HOUSE`, `INDGUYS`, `LILGUYS`, `HEALTH` `0x103F0`. | Só 2 tipos de aranhas usados; `BRIDGE`/`HAND`/`HOUSE`/`LILGUYS`/`HEALTH` do `04STXT` ociosos; limite `580px`. | Carregar props/obstáculos da vila (`HAND` bloqueador/perigo, `LILGUYS` enxame pequeno, `HEALTH` `0x103F0`) e percurso estendido. |
| 17 | `20ICE1` | `runway_phase` (`RUNWAY`) | `15PAN`, CDB até `8192` suportado no loader, zonas leves (`d2`) em `60..155px`. | Nenhuma física de gelo (fricção zero / derrapagem ao soltar o direcional). | Implementar **atrito reduzido** no chão (desaceleração gradual de `vel[0]` em vez de zerar instantaneamente) + esquiva de obstáculos. |
| 18 | `21ICE2` | `runway_phase` (`RUNWAY`) | Mesmo motor de `20ICE1`. | Idêntica a `20ICE1`. | Combinação de derrapagem no gelo + rajadas/escorregões mais longos e janelas de salto mais exigentes que `ICE1`. |
| 19 | `22SHADOW` | `sidescrl_phase` (`SIDE`) | `scrl_map` com parallax `0x10000` em A..D, inimigos `LORD`/`GANG` genéricos, FMV para `MOAT`. | Sem mecânica de luz/sombra nem inimigos específicos de sombra. | Zonas de sombra/visibilidade limitada ou emboscadas de poofs que aparecem perto de Bones e exigem revelação com acorde Blues (`C`/`A`). |
| 20 | `23MOAT` | `runway_phase` (`RUNWAY`) | `15PAN`, `moat_wingnut[16]` + `moat_redpoof[9]`; `04STXT` tem `HEALTH` `0x041D0` (ainda não ligado). | Inimigos alados não ameaçam verticalmente e `apply_hits=0`; `HEALTH` `0x041D0` sem uso. | Ataque aéreo rasante de `wingnut` (exigindo `DOWN`/abaixar ou Blues `C`), perigos do fosso e pickup `HEALTH` `0x041D0`. |
| 21 | `24STAING` | `runway_phase` (`RUNWAY`) | `15PAN`, zonas `65..158px`, corte para `25GENRM`. | Mesmo loop curto de `RUNWAY` sem clímax pré-chefe. | Corredor de alta pressão antes do Generator Room (maior cadência de arremessos `CThrown` e obstáculos sequenciais). |
| 22 | `25GENRM` | `special_phase` (`ACTION`) | `gen_red`, `gen_blue`, `gen_zap` (`TimingZapMax = 0x10` frames!), **`FINISH_X = 160px`**, `apply_hits = 1`. | **Vitória trivial andando `120px`**; `CRed`/`CBlue` só andam na direção de Bones sem polaridade ou objetivo de limpeza. | **Arena de Sobrevivência/Conversão:** objetivo é neutralizar/converter todos os `CRed` com Blues (`C`) enquanto evita pulsos `CZap` e usa `CBlue` como catalisador/bônus. |
| 23 | `26LAFF` | `sidescrl_phase` (`SIDE`) | `laff_clown[10]` (palhaço de Da Laff), FMV final para `27FINALE`. | **Da Laff apenas patrulha `±20px` enquanto o jogador passa andando** até `finish_x <= 580px` e vence sem derrotá-lo! | **Arena de Chefe:** travar a saída enquanto `LAFF` estiver ativo; boss multi-padrão (salto/investida/risada arremessada) derrotável via golpes Blues `C` (`hp` dedicado). |
| 24 | `27FINALE` | `special_phase` (`PRESENT`) | `CRing` 16 slots + quads `112×30` + `RingDefinitionTable` + `SoundEvent` SCSP (`0x29..0x33`) + `CFBones`/`CBolt` fx (`apply_hits=0`) + `PlayDuckMovie(3)`. | No modo interativo, `CFBones`/`CBolt` ainda não aplicam dano real ao jogador nem interagem diretamente com os tiros de anel; e em headless ganha apenas esperando o filme. | **Chefe Final Completo:** jogador desvia dos `CBolt` / descargas de `CFBones` (`apply_hits = 1` no modo arena interativa) enquanto limpa todos os 16 slots do anel com tiros direcionais (`UP/DOWN/LEFT/RIGHT + A/B/C`) para disparar o ending `PlayDuckMovie(3)`. |

> **Nota sobre `runway_phase.c` `15PAN`:** em `runway_phase.c:335`, `RUNWAY` usa `FINISH_X = 280px`, enquanto `generic_pan` (`LOGS`, `ICE1`, `ICE2`, `MOAT`, `RIDE`, `STAING`) usa `START_X + 120px = 160px`. Como `pan_scroll_update` interpola quadros de `15PAN` entre `START_PX` e `FINISH_PX`, e vários spawns de `stage_enemies_init_runway` ficam entre `182..238px`, estender os estágios `15PAN` (Etapa 3) exige ajustar o mapeamento de progresso para `phase->pan.count` além de `280px` e `generic_pan`.

---

## 5. Especificação das Mecânicas Centrais Faltantes

### 5.1 Combate Blues (Botões `C` e `A`) e Vida de Inimigos (`ent->hp`)
- **Estado atual:** `stage_enemy` possui `hp` (`1`, `3` ou `5`), `active` e `anim`, mas não há API de dano ao inimigo em `stage_enemy.h`.
- **Alvo:**
  - Adicionar struct de projétil/onda curta do jogador (ex.: `blues_shot` com `x, y, vx, ttl, power, kind`), disparada ao pressionar **`C`** (`0x0200`) em `runway_phase`, `sidescrl_phase` e `SPECIAL_MODE_ACTION`.
  - Cooldown curto (ex.: `12–16` frames) para evitar spam trivial; ao atingir um `stage_enemy` ativo na faixa X/Y, decrementar `ent->hp`.
  - Quando `ent->hp <= 0`: marcar `ent->active = 0`, incrementar contador de derrotados/convertidos, acionar efeito visual (`poof_blue` / flash) e SFX (`SFX_C` / evento `05SND1`).
  - **Botão `A` (`0x0400`):** reservado para pulso Blues radial de curto alcance (útil quando cercado por `ZOMBIE`, `LILGUYS` ou `CRed` em `GENRM`), com cooldown maior (`~45` frames).
  - **Compatibilidade com `runway_phase`:** o disparo em `C` deve coexistir com o log BBS `MSG_C_A`/`MSG_C_B` já testado em `test_runway_phase.c` (`last_message = 0x00020600`).

### 5.2 Saúde, Dano Consistente e Pickups `HEALTH`
- **Ligação de dano em `runway_phase`:** hoje `stage_enemies_step(..., 0, 0)` desativa `apply_hits` e não passa `&phase->hazards`. Em modo jogável real:
  - Passar `&phase->hazards` para os arremessos `CThrown` existirem fisicamente na pista.
  - Diferenciar colisão de altura (permitir pular por cima de projéteis rasteiros ou abaixar/esticar com `DOWN` quando o ataque vier por baixo/cima, comparando `bones_y` / `phase->tall` com `ent->y`).
- **Pickups `HEALTH`:**
  - `build-static/ymir-visual-ref/probe-out/FRONTES-RE.md` já mapeou os offsets exatos de `HEALTH` em `04STXT` (banco `CMDCOLR = 0`, ~28 frames):
    - `16BIGBNS`: `0x0BFE0`
    - `11LOGS`: `0x0A090`
    - `19VILLAGE`: `0x103F0`
    - `23MOAT`: `0x041D0`
  - Ao encostar em um pickup `HEALTH` ativo, chamar `runway_skel_add(&phase->skel, RUNWAY_DAMAGE_DEFAULT)` e desativar o item com feedback sonoro.
- **Morte por abismo no `sidescrl_phase`:** validar se `floor_py[col]` possui marcadores de buraco/limite baixo e aplicar perda de Skeletism (`skel.died = 1`) se cair abaixo da tela (`> 224 + 24 px`), equivalente ao `CheckFallHole` de `runway_phase.c`.

### 5.3 Escala Real de Fases e Chefe por Objetivo
- **`sidescrl_phase` além de `580px`:**
  - Hoje `build_scroll_from_map` limita `strip_w` a `1280px` (mesmo `SCRL_STRIP_MAX` sendo `2048px`), e `goal_px` é travado em `580px`.
  - Alvo: permitir `strip_w` até `min(planes[0].pixel_w, SCRL_STRIP_MAX)` e posicionar `finish_x` perto do fim real do cenário (`strip_w - 64px`), ajustando `stage_enemies_populate_sidescrl_strip` para distribuir encontros até o final da fase.
- **Vitória por Objetivo (`arena_mode`):**
  - Para **`16BIGBNS`**, **`25GENRM`** e **`26LAFF`**, desvincular a vitória de simplesmente cruzar `120px` ou `580px`: a vitória exige **limpar/derrotar o conjunto de alvos ou chefe** (`enemies_remaining == 0` ou `boss->active == 0`), mantendo uma barreira/arena visível na borda direita até o objetivo ser cumprido.

### 5.4 Refinamento de `07GUITAR`, `14DRUM` e `27FINALE`
- **Ritmo (`GUITAR` / `DRUM`):**
  - Introduzir `combo`, `max_combo` e pontuação de precisão na `special_phase`; acertos seguidos (ex.: combo de 4) restauram uma fração de Skeletism via `runway_skel_add`.
  - Avaliar calibração de `hits_need` diferenciada entre jogo interativo e teste automatizado, ou manter `hits_need` progressivo por frase.
- **`27FINALE`:**
  - No modo interativo (quando o vídeo `20AVI` inicial está diferido para a arena — `special_phase_finale_defer_ending`), habilitar dano evitável de `STAGE_ENEMY_RING` / `STAGE_ENEMY_BOLT` (`apply_hits = 1`) com indicador visual claro de perigo para os projéteis de `CFBones`.

---

## 6. Restrições Técnicas & Convivência com Testes Automatizados (Crítico)

Qualquer alteração de gameplay **precisa respeitar os testes determinísticos e fluxos já validados**, sob risco de quebrar `ctest` e o `--check`:

1. **Bots atuais só andam para a direita (`MB_PAD_RIGHT`):**
   - `play_native_stage` em [`src/tools/mrbones.c`](file:///F:/MrBonesPort/src/tools/mrbones.c) segura `MB_PAD_RIGHT` (e `MB_PAD_A` no ritmo) por até `STAGE_MAX_FRAMES = 5000` frames em todas as 22 fases nativas (`--full-campaign` / `--check`).
   - `tests/test_stage_runner.c` segura `MB_PAD_RIGHT` até `5000` frames esperando `STAGE_RS_FINISHED` em `RUNWAY`, `MAUSO`, `LOGS`, `RIDE` e `LAFF`.
   - `tests/test_sidescrl_phase.c` (`play_to_finish`) e `tests/test_special_phase.c` (`play_action`) também usam apenas `MB_PAD_RIGHT` até `5000` frames.
   - **Consequência prática de engenharia:**
     - Ou os testes de campanha (`play_native_stage`, `play_to_finish`, `play_action`) passam a usar um **bot mínimo capaz de pular e apertar `C`**, **ou** fases com chefes/arena bloqueada expõem entrada compatível (atualizando os bots dos testes na mesma etapa — preferível e honesto, pois garante que até o `--check` teste o combate e pulos reais!).
     - Se `finish_x` crescer para `1280..2000px` no `sidescrl_phase`, garantir que a caminhada contínua ainda termine bem abaixo de `5000` frames (`2000px / 3px_per_frame ≈ 667` frames — folga enorme!).
2. **Hashes e contratos de infraestrutura intocáveis:**
   - Frame 100 de `/07GUITAR` (`test_stage_runner.c:100`) tem SHA-256 exato validado; não alterar decodificação de `duck_avi` nem renderização pura de FMV em `SPECIAL_MODE_PRESENT`.
   - `test_runway_phase.c` valida explicitamente `hit.top_right_y == -8` em Tall, `last_message` BBS (`0x00020A00`, `0x00010700`, `0x00020600`), `sound_event_id == 0xc` e contagem `anim_bank_count`.
   - `g_headless_finale_movie = 1` em `--check` / `--full-campaign` precisa continuar funcional para a verificação automática do ending FMV de `FINALE`.
3. **Regra permanente de verificação silenciosa durante esta sessão:**
   - Sempre validar mudanças via build duplo (MSVC `build-static` e Clang `build`) + CTest silencioso:
     `ctest --test-dir <dir> -E "test_movie_audio|test_mrbones_paths|test_full_campaign|test_fmv_transition" --output-on-failure`.

---

## 7. Plano Prático em Etapas

### Etapa 1 — Combate Blues (`C`/`A`), Saúde (`HEALTH`) & Dano Calibrado (Fundação Jogável)
- **Objetivo:** Dar ao jogador capacidade real de atacar, sofrer dano justo dos inimigos e recuperar ossos em todos os motores (`runway_phase`, `sidescrl_phase`, `special_phase`).
- **Mudanças principais:**
  1. `stage_enemy.{h,c}`: adicionar projétil/pulso Blues do jogador, função `stage_enemies_attack(...)` com redução de `ent->hp`, derrota com `poof_blue`/flash e suporte ao item `STAGE_ENEMY_HEALTH` (carregando `HEALTH` `04STXT` em `LOGS`, `MOAT`, `BIGBNS`, `VILLAGE` -> `runway_skel_add`).
  2. `runway_phase.c`: conectar disparo Blues no evento `MSG_C_B` (botão `C`), passar `&phase->hazards` para materializar `CThrown` e verificar altura (`bones_y`/`tall`) no impacto.
  3. `sidescrl_phase.c` & `special_phase.c` (`ACTION`): ligar `C` (disparo horizontal), `A` (pulso curto), `DOWN` (agachar/abaixar hitbox) e atualização dos bots em `test_stage_runner.c`, `test_sidescrl_phase.c`, `test_special_phase.c` e `mrbones.c` se necessário.
- **Critério de aceite:** testes unitários novos validando redução de `hp`, cura com `runway_skel_add` e dano por `CThrown`; build MSVC+Clang e `ctest -E ...` verdes; captura PNG demonstrando projétil Blues + `HEALTH` em tela.

### Etapa 2 — Regras Reais das Fases Especiais (`25GENRM`, `16BIGBNS`, `07GUITAR`, `14DRUM`)
- **Objetivo:** Acabar com vitórias triviais em `ACTION` e aprofundar o modo `RHYTHM`.
- **Mudanças principais:**
  1. `16BIGBNS`: física de repulsão para `BUMPER` (`0x041D0`), objetivo de limpar/desarmar inimigos da arena antes de liberar a saída.
  2. `25GENRM`: objetivo explícito de neutralizar/converter os `STAGE_ENEMY_RED` enquanto evita pulsos periódicos de `STAGE_ENEMY_ZAP`.
  3. `07GUITAR` / `14DRUM`: sistema de combo/streak, barra visual de precisão e recuperação parcial de Skeletism em sequências perfeitas.
- **Critério de aceite:** testes dedicados em `test_special_phase.c` e `test_rhythm_enemies.c` comprovando bloqueio de saída antes do objetivo e vitória após cumprir o objetivo; `ctest -E ...` verde.

### Etapa 3 — Identidade & Extensão dos Corredores (`15PAN`) e Side-Scrollers (`MAUSO..SHADOW`)
- **Objetivo:** Fazer cada uma das 17 fases de travessia se comportar e escalar de verdade.
- **Mudanças principais:**
  1. `sidescrl_phase.c`: expandir `strip_w` até `SCRL_STRIP_MAX` e `finish_x` até o fim real do mapa; ativar risco de queda em buracos reais; ligar props extras mapeados em `VILLAGE` (`HAND`, `LILGUYS`, `BRIDGE`, `HOUSE`).
  2. `runway_phase.c`: tabela por estágio (`stage_name`) com física distinta — atrito baixo em `ICE1`/`ICE2`, ritmo/impulso rápido em `RIDE`, pressão de arremessos em `STAING`, ameaça aérea de `wingnut` em `MOAT`, deriva/saltos em `LOGS`.
  3. `stage_enemy.c`: IA de mergulho aéreo (*swoop*) para `STAGE_ENEMY_BAT` / `STAGE_ENEMY_WINGNUT` e movimento aéreo em `GLASS` / `VORTEX` / `SHADOW`.
- **Critério de aceite:** testes de regressão de física (ex.: deslizamento mensurável em `ICE1`, mergulho em `BAT1`, fase > `640px` em `MAUSO`/`VILLAGE`) + dumps visuais + `ctest -E ...` verde.

### Etapa 4 — Chefes (`26LAFF` e `27FINALE`)
- **Objetivo:** Dar clímax real ao meio/fim de campanha.
- **Mudanças principais:**
  1. `26LAFF` (sobre `sidescrl_phase` + `stage_enemy`): IA de chefe em fases (patrulha rápida, projéteis/ataques de palhaço, `hp` alto de chefe), exigindo derrota para concluir a fase.
  2. `27FINALE` (`special_phase`): em modo arena interativa (sem vídeo headless ativo), habilitar dano real de `CFBones`/`CBolt` (`apply_hits = 1`), proteção/desvio com `D-pad` e vitória condicionada a zerar os 16 slots de `CRing`, disparando em seguida o ending `PlayDuckMovie(3)`.
- **Critério de aceite:** simulação de luta completa contra `LAFF` e arena `FINALE` em `test_special_phase` / `test_rhythm_enemies`, mantendo intactos os hashes `--check` do `FINALE` movie.

### Etapa 5 — Polimento de Fluxo, HUD e Meta-Campanha (`03MENU` / `30MAP` / Feedback)
- **Objetivo:** Coesão da experiência de ponta a ponta ao jogar no teclado/controle.
- **Mudanças principais:**
  1. HUD unificado e limpo (indicador de objetivo restante em arenas/chefes, ícone discreto de disco/progresso, animação breve antes de `/28LOSE` na morte).
  2. Melhorias visuais no `menu_phase.c` (estado de desbloqueio claro no `LevelSelect`, feedback de continuação).
  3. Investigação opcional de `/30MAP` para exibição entre capítulos se houver assets `04STXT`/`CDB` decodificáveis.
- **Critério de aceite:** revisão visual completa de telas de menu, HUD e fim de jogo + suíte silenciosa `ctest -E ...` 100% verde.
