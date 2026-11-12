# RUNWAY brains — fatos, inferências e não resolvidos

Fonte estática: `/04RUNWAY/03CODE` SHA-256 `7532e4b8…9dc216`;
**load base `0x00232000`**; dump `docs/ghidra-runway-brains.txt`; sessão REA `docs/rea-runway-session.md`.

## Fatos observados

| Símbolo | Endereço | Observação |
|---|---|---|
| `_Stand_Brain` | `0x0025ff98` | Laço infinito; flags objeto+0x1c; probes de mensagem |
| `_BonesWalk` | `0x00260370` | `param_3` máscaras; probes + yield |
| `_BonesYeild` | `0x0025d27c` | Yield específico do personagem |
| InputBrain | `0x0026a614` | L/R assimétricos; bits 0x40/0x80/0x20 |
| Coll map | 4480 B | 40×28×4; topo sólido ≈ ty 13–17 |

## Inferências usadas no nativo

- Passo **L=3932** / **R=1966** (InputBrain `0x26a6ac` / `0x26a6b0`) em `bones_x` (host).
- Tall L/R: 1310 / 2621 (`abs(-2621)`).
- Projeção centro ≈ `(cam-world)*28>>16` (`runway_projection`).
- BBS: post + peek/consume com `(id & mask)==value`; boot consume `0x20A00`.
- Chão = `ground_ty`; parede ≈ degrau > 4 tiles.
- `05SND1` span4 PCM8 experimental.

## Resolvido (§12f)

- Tall writer = `TallSet_26D784` (brain ∈ crouch set → `0x28DB8C`).
- `0x204128(0xc)` = enqueue scheduler table slot `0xc`.
- `WalkMove_25DB1C` = interpola ushort em direção a alvo (passo=`param_1`); não é world-X direto.
- `0x25DFD8(0x2858BC)` = ring-buffer de 3 words (histórico), não aplica posição.

## Resolvido (§12g) — mapa objeto Bones X/Y → writers

Dumps: `ghidra-bones-world.txt`, `ghidra-world-writers.txt`, `ghidra-world-xform-loader.txt`, `ghidra-bones-pos-link.txt`, `ghidra-inv-xform.txt`.

### Layout que RWorldSprite lê

`RWorldSprite_268A64(param_1, …)`:

| Campo | Expressão | Papel |
|---|---|---|
| world X | `*(int *)*param_1` | posição |
| world Y | `*(int *)(*param_1 + 4)` | posição |
| ângulo | `*(ushort *)(*param_1 + 0xc)` / `+0xe` | lookup escala |
| câmeras | `0x228FA4` (X), `0x2297B4` (Y) | `(*cam - world)*7>>14` → screen |

O bloco em **`0x2858BC`** casa com esse layout:

| Offset | Célula | Uso |
|---|---|---|
| +0 | `0x2858BC` | world X |
| +4 | `0x2858C0` | world Y |
| +8 | `0x2858C4` | world Z |
| +0xC | `0x2858C8` | copiado/usado em `BonesYeild` |

Grade de colisão (ints de tile, não o que o sprite lê direto):

| Célula | Papel |
|---|---|
| `0x2858D4` | grid X (`WalkAnim`/`CollProbe`/`Fn_25C7D4` usam `0x2858D8-4`) |
| `0x2858D8` | grid Y |

### Writers (Walk / yield / input)

1. **`InputBrain_26A614`** — soma o passo em `*(int*)0x2858BC` (só X): L nontall +3932, L tall +1310, R tall −2621, R nontall **no-op**, idle nontall +1966.
2. **`NearVel` (`0x25B1C8` / add em `0x25B380`)** — integra velocidade em `0x2858BC[+0/+4/+8]` (pool `PTR=0x2858BC`).
3. **`WalkAnim_25C694`** — em colisão chama **`WorldXform_206AB0(gridX, gridY, Z)`** e grava o vec3 em `0x2858BC`.
4. **`BonesYeild_25D27C`** — a cada yield:
   - `InvXform_2078C0(worldXYZ)` → escreve grade em **`0x2858D4`**;
   - clampa grade; se saiu do range, **`WorldXform_206AB0`** devolve o mundo em **`0x2858BC`**.
5. **`WorldXform_206AB0`** (loader) — `grid→world` em buffer `0x220108`, offsets `0x228FA4`/`0x2297B4`.
6. **`InvXform_2078C0`** (loader) — `world→grid` (subtrai os mesmos origins, escala por Z).

`WalkMove_25DB1C` continua só no ushort de facing (`0x2858E6`→`0x2858EA`). `WalkStep_25EE58` troca brain / não é o adder de world-X.

### Elo e decisão de host walk

- **Lido pelo sprite:** world em `0x2858BC` (+Y/+ângulos), não a grade.
- **Atualizado pelo walk path:** InputBrain + NearVel + WalkAnim/BonesYeild (xform).

## Resolvido (§12h) — vel `0x285904`/`928` → NearVel → `0x2858BC`

Dumps: `ghidra-vel-chain.txt`, `ghidra-vel-writers2.txt`.

| Célula | Papel |
|---|---|
| `0x2858FC`..`904` | **velocity** vec3 (KillVel segundo bloco; `904` = vel.Z = velA do BonesWalk) |
| `0x285908`.. | **acceleration** vec3 (KillVel primeiro bloco) |
| `0x285928` | **velB** (BonesWalk speed; `VelFn_262FD0` soma `904+928`) |

Fluxo:

1. `VelThrust_25E48C` — lê world `0x2858BC`, escreve impulso em vel Y/Z (`0x2858FC+4/+8`); pode **negar** `0x285904` conforme facing `0x2858E6`.
2. `NearVel_25B1C8` — `vel += accel` (`0x2858FC += 0x285908`); depois projeta e toca world `0x2858BC`.
3. `CMover_Move_259D28` — `pos+=vel; vel+=accel` no objeto contigous.
4. `KillVelAccel_25E088` — zera accel@908 e vel@FC.
5. `VelFn_262FD0` — grava `904+928`, zera vel/accel; `VelFn_2636F8` — path anim/velB.

**Nativo (§12h):** `bones_x` ≡ `input_cell` ≡ `0x2858BC`. Pad L/R preenche `vel`/`vel_a`/`vel_b`; integra `vel+=accel` e `bones_x+=vel_x` (R nontall via vel, não InputBrain). InputBrain só em scratch para posts Down/C.

## Resolvido (§12l) — HitChkScrn* + bancos + flip-X + CPanScroll

Dumps: `ghidra-hitchk-config.txt`, `ghidra-hitchk-writers.txt`, `ghidra-stride38.txt`; CPanScroll em `ghidra-bones-pos-link.txt`.

| Peça | VA / fato | Nativo |
|---|---|---|
| `BonesSetConfig` | `0x25d75c` → pool `0x25d904..920` → `@0x285A6C..88` | `pan_hit_offsets_*` |
| bnscfg AABB | `0x2873EC` (+0x50..0x6c) = (±5,−8)/(±5,+12) | defaults + center lift 12 |
| Mid HitChk | `@0x285A5C..68` | sem writer neste path (ficam 0) |
| Banco morph | `0x23204C`×13 stride `0x38` | walk |
| Banco full | `0x253858`×20 | stand / air / tall |
| H-flip | facing L → VDP1 | `blit_sprite_ex` |
| `CPanScroll` | `0x257320` loop scroll shorts | §12m `pan_scroll` |

## Resolvido (§12m) — TallSet + 15PAN stream + Mplaynext

| Peça | VA / fato | Nativo |
|---|---|---|
| `TallSet_26D784` | `*0x28589C` ∈ crouch8 → `*0x28DB8C` | `bones_tall_set` |
| Crouch slots | `0x2873EC…0x288084` (8) | `bones_crouch_slots[]` |
| `0x204128(0xc)` | enqueue tabela slot `0xc` | `sched_enqueued_0c` |
| `BrainSwitch_25d534` | instala config/brain | `brain_switch` |
| `TallBrain_26D68C` | avança tabela crouch | hold Down mantém slot |
| `15PAN` | 2651 records BE32 (~38 MB) | `pan_scroll_seek` visual |
| Coll física | — | §22 live pandcomp; rec0 absolute, >0 viewport `coll_probe_px` |
| `Mplaynext`/`Mnext` | cue stride `0xc`, dur≈8 | `mplay_seq` no walk |

## Refino host (pós-FINALE PCM) — NearVel / TallSet / HitChk

| Peça | Nativo |
|---|---|
| `bones_near_vel_integrate` / `_apply` | `vel+=accel` então `world@0x2858BC+=vel`; fase aplica com gate de colisão |
| `bones_kill_vel_accel_vec` | KillVelAccel em vec3 da fase |
| Stand→Walk msgs | posta `0x11001`/`0x11000` na borda STAND→WALK |
| `brain_switch` | TallSet + BonesSetConfig HitChk a partir do template `0x2873EC` (AABB idêntico nos crouch8) |

## Não resolvidos (bloqueiam fidelidade byte-a-byte; não bloqueiam jogatina RUNWAY)

- SCSP; cue table Bones literal no 03CODE; Mid HitChk; coll 15PAN por frame alinhada ao scroll.
- TallBrain timing/thresholds byte-fieis.
- Captura Ymir RUNWAY-001.
- NearVel matriz/câmera completa (nativo usa integrate + colisão de coluna).

## Adaptação nativa

`src/core/runway_phase.c` implementa um loop de fase jogável mínimo que
**não** afirma equivalência byte-a-byte com Stand/Walk.
