# Cobertura

Estados: **não testado**, **parcial**, **validado com referência**. Compilar não marca nada como concluído.
Referência RUNWAY-001: ainda não criada. Ymir e BIOS existem na área temporária;
a inicialização de RUNWAY não foi observada. Discos com faixas CD-DA estáveis em
`F:\MrBonesPort` (raiz do projeto; junção em `Neomaster\MrBonesPort`).

**Host 1.0.0 (§25):** campanha nativa + DirNameTable/DirTable/DirIDTable literais (28 folhas ISO,
VA `@0x0022da00`) + MENU title `20AVI`/`02TITL`/`19DUCK` + créditos 360×6 pós-FINALE +
TM1/19DUCK + coll 15PAN live + hazards×7 + `scrl_map` + `rhythm_table` + `stage_enemy`.
Linhas abaixo marcam fidelidade SH-2 vs host jogável — “parcial” na jogabilidade host **não** impede
a campanha ponta a ponta.

| Fase | Inicialização | Jogabilidade | Imagem | Áudio | Transição | Save | Diferenças conhecidas | Evidência |
|---|---|---|---|---|---|---|---|---|
| (infra) identidade de disco | validado | n/a | n/a | n/a | n/a | n/a | manifesto dos dumps do maintainer | `test_cdimage`; `mbverify --quick` |
| (infra) leitura ISO/faixas | validado | n/a | n/a | n/a | n/a | n/a | Disc1 9/9 áudio; Disc2 11+dados | `F:\MrBonesPort\disco*` |
| (infra) teclado→pad | parcial | n/a | n/a | n/a | n/a | n/a | layout host | `host_input`; `test_host_input` |
| (infra) composição | parcial | n/a | parcial | n/a | n/a | n/a | blit RGBA ≠ VDP1/VDP2 | `runway_compose`; `mbvisual` |
| (infra) ObjYeild | parcial | n/a | n/a | n/a | n/a | n/a | Fibers ≠ ring SH-2 | `test_obj_scheduler` |
| (infra) catalog/progress/swap | **parcial** | n/a | n/a | n/a | **parcial** NEED_DISC | **parcial** auto nvm | 24 campaign; stub≤20k; INSERTCD host | `test_stage_*`; `test_full_campaign` |
| (infra) full campaign §19–§25 | **parcial** | **parcial (nativo)** | MENU+HUD+AVI | CD-DA+SND1+PCM | COMPLETE→credits 360×6 | nvmshadow.bin | 22 native; DirNameTable; TM1+scrl+enemies | `mrbones --full-campaign`; `--check` |
| (infra) DirNameTable §25 | **parcial** | n/a | n/a | n/a | n/a | n/a | BSS GFS runtime → literais ISO+verify | `test_stage_catalog` |
| (infra) duck_avi §20/§22 | **parcial** | n/a | TM1+19DUCK | 01wb→PCM8 | present step | n/a | decode simplificado ≠ FFmpeg bit-exact | `test_duck_avi` |
| (infra) scrl_map §23 | **parcial** | n/a | ACDB..DTIL+AMAP | n/a | n/a | n/a | tilecache heap/flip/banco CRAM; VRAM/prioridade aberto | `test_scrl_map` |
| (infra) rhythm/enemies §24 | **parcial** | n/a | n/a | n/a | n/a | n/a | DrumSequence 48 notes; enemy AI soft | `test_rhythm_enemies` |
| MENU | parcial | **parcial (§25)** | STXT+title AVI | soft PCM | → stage_runner | nvmshadow | 20AVI/02TITL/19DUCK; font SH-2 aberto | `test_menu_phase` |
| RUNWAY | parcial | **parcial (nativo)** | parcial + HUD | parcial CD-DA+PCM8 | EndLevel / pause | via progress | gang/lord/poofs+throws; Ymir aberta | `mbrunway --check`; `test_runway_phase` |
| pan 15PAN (LOGS/ICE/MOAT/RIDE/STAING) | **parcial** | **parcial (nativo §16/§22)** | parcial + HUD | soft SND1 | EndLevel curto | unlock | coll live + hazards por fase | `test_stage_runner`; `mrbones --check` |
| side-scroll (MAUSO…LAFF) | **parcial** | **parcial (nativo §17/§23/§24)** | multi-layer DrawMap | soft SND1+CD-DA | EndLevel curto | unlock | scrl_map+poofs; DecompTile aberto | `test_sidescrl_phase`; `test_scrl_map` |
| GUITAR / DRUM | **parcial** | **parcial (nativo §18/§20/§24)** | parcial STXT/TITL | soft SND1+CD-DA | EndLevel ritmo | unlock | DrumSequence+guitar table; lick full aberto | `test_special_phase`; `test_rhythm_enemies` |
| BIGBNS | **parcial** | **parcial (nativo §18/§24)** | parcial STXT+PAN | soft SND1 | EndLevel ação | unlock | lord/gang soft enemies | Disc1 + pan~15MB |
| GENRM | **parcial** | **parcial (nativo §18/§24)** | parcial STXT+PAN | soft SND1 | NEED_DISC→D2 | unlock | reds/blues/zap TimingZapMax | Disc2 |
| FINALE | **parcial** | **parcial (nativo §18/§22/§24)** | duck_avi+19DUCK | soft + PCM | COMPLETE | unlock | RingDefinitionTable fx; Brain full aberto | Disc2 CODE 600k |
| LOSE / DUKMOVIE | **parcial** | **parcial (nativo §18/§22)** | TM1+19DUCK + STXT | 01wb PCM | present→LOSE | n/a | tables_from_duck=1; soft fallback | `test_duck_avi`; `/28LOSE` |
| TROLL1/2 | EMPTY skip | skip | n/a | n/a | skip | unlock | Sem 03CODE real | catalog EMPTY |
| INTRO | parcial | n/a | n/a | n/a | n/a | n/a | Sem CODE | — |

### RUNWAY / pan — diferenças conhecidas (fase nativa)

- Stand/Walk **não** reproduzem a árvore completa; BBS nativo: post/peek/consume + máscara (`rea-runway-session.md` §12e).
- Tall = `TallSet_26D784` (brain ∈ crouch set). Vel→NearVel em `0x2858BC`.
- Colisão §12k/§12l: mapa fino 40×112; HitChk AABB bnscfg. §22 `15PAN` seek visual + coll live: record-0 world-absolute; pandcomp>0 viewport via `coll_probe_px`.
- §12n: CD-DA + `05SND1` PCM8 host (**não** SCSP). §12o/§16/§22: skelbar 14 segs + PAUSE HUD; hazards em todas as 7 fases pan.
- §13–§25: MENU STXT+title AVI + NvmShadow + campanha 24 + pan/sidescrl/special + duck_avi/19DUCK + scrl_map + rhythm/enemies + DirNameTable + créditos + host **1.0.0**.
- Ymir RUNWAY-001 **não capturada**.
- REA MCP Windows: SuperH raw **não admitido**; Ghidra headless local.

### §15–§25 — notas de integração

- Campanha = 24 dirs DirNameTable (RUNWAY…FINALE). Win → `mb_nvm_unlock_level` current+next + `mb_nvm_save`. Lose → `/28LOSE`. FINALE win → COMPLETE → CREDITS (360 frames × 6 pages).
- DUKMOVIE não é pasta ISO; player MAP `DUCKPLAY`/`mfp_avi`; host `duck_avi` + `19DUCK` em `/28LOSE` 20AVI (LIST/rec + DUCK).
- §16 pan nativo: RUNWAY + LOGS + ICE1 + ICE2 + MOAT + RIDE + STAING.
- §17/§23 side nativo: MAUSO + VALLEY + BAT1–3 + GLASS + VORTEX + VILLAGE + SHADOW + LAFF (`sidescrl_phase` + `scrl_map`).
- §18 special nativo: GUITAR + DRUM (RHYTHM) + BIGBNS + GENRM (ACTION) + FINALE + LOSE/DUKMOVIE (PRESENT).
- §19 `mrbones --full-campaign`: MENU visual → 22 fases nativas (skip TROLL) → INSERTCD → FINALE → credits; rejeita smoke; `nvmshadow.bin` 0x28.
- §20 `duck_avi` soft TM1 + PCM; floor TITL; beat_period por fase.
- §21 host **0.21.0** campanha final.
- §22 host **0.22.0**: `duck_tm1` + 19DUCK; coll 15PAN live; hazards×7.
- §23 host **0.23.0**: `scrl_map` LoadCDB/DrawMap/SCRLCOLL + AMAP floor.
- §24 host **0.24.0**: `rhythm_table` + `stage_enemy` (gang/lord/poofs/reds/blues/zap/ring/bolt).
- §25 host **1.0.0**: DirNameTable/DirTable/DirIDTable literais + verify ISO; MENU `20AVI`/`02TITL` opening; créditos multi-page.
- §26 host **1.0.0+**: `_DecompByte@0x002a0b4c` (`scrl_decomp_byte`) + `docs/ghidra-vdp2-tiles.txt` + `docs/ghidra-decompbyte.txt`; fonte bitmap 8×8 (`mb_font`) em MENU, HUD, PAUSE e créditos; `stage_enemies_render` visível em todas as fases (pan×7, sidescrl×10, GENRM, BIGBNS, FINALE); rolagem multi-telas em `sidescrl_phase` + células VDP2 8×8; vídeo `20AVI` + pista de notas (*Note Highway*) em GUITAR/DRUM + TM1 `4×4`/`2×4` refinado. Suite: CTest **41/41**; `mrbones --full-campaign --disc2` / `--check --disc2` / `mbrunway --check` EXIT=0.
