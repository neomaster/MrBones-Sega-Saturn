# REA + Ghidra session — RUNWAY brains (2026-11-07)

## REA MCP (Windows)

| Operação | Resultado | Evidence |
|---|---|---|
| `binary_session` | Ghidra 12.1.4 disponível | — |
| `open_binary` SuperH raw | **recusado** — `architecture` só admite `6502\|x86\|x86_64\|arm\|arm64\|m68k` | invalid_request |
| `inspect_byte_entropy` em `03CODE.bin` | entropy **5.9903**, 690052 B | `ev_8e3ae51b…` / parent `ev_d2a9a9bc…` |
| SHA-256 | `7532e4b8e804c6908ce03c987587612876a65663e56fa32b2174d022da9dc216` | mesmo digest |

**Conclusão REA:** no Windows P0 o Ghidra via MCP **não** abre SuperH/SH-2. Entropia/hash offline funcionam. Decompilação SH-2 ficou no **analyzeHeadless local**.

## Disco referência `D:\`

`D:\Emulation\roms\saturn\Mr. Bones (USA)\…\Track 01.bin` SHA-256 **idêntico** ao de `F:\MrBonesPort\disco1` (`1EDF45A5…`). Mesmo dump; não há bytes novos. Ymir-001 continua bloqueado no mount CLI.

## Achado de carga (crítico)

`/04RUNWAY/03CODE` **não** mapeia em `0x00200000`. Prólogos Stand/Walk/Input batem com:

**load base = `0x00232000`** (= `runway_entry`)

`file_offset = VA - 0x00232000`

## Literais InputBrain (`0x0026a614`)

| Slot | VA | Valor | Uso nativo |
|---|---|---|---|
| L+tall | `0x0026a6aa` | 1310 | (ainda não) |
| **L** | `0x0026a6ac` | **3932** (`0x0F5C`) | `WALK_STEP_LEFT` |
| R+tall | `0x0026a6ae` | -2621 | (ainda não) |
| **R** | `0x0026a6b0` | **1966** (`0x07AE`) | `WALK_STEP_RIGHT` |
| yield | `0x0026a6d4` | `0x002050B0` | `_ObjYeild` (loader) |

Bits BonesInput (já documentados): `0x40` L, `0x80` R, `0x20` Down.

## Stand_Brain — IDs de mensagem (3º arg dos probes)

| VA literal | ID |
|---|---|
| `0x0025ffe8` | `0x00020A00` |
| `0x00260268` | `0x00020800` |
| `0x00260274` | `0x00020600` |
| `0x0026027c` | `0x00010700` |
| `0x00260298` | `0x00020A00` |
| `0x002602a0` | `0x00030A00` |
| `0x002602c4` | `0x00010900` |
| `0x002602c8` | `0x00011001` |
| `0x002602d4` | `0x00011000` |
| `0x002602dc` | `0x00010A00` |
| `0x00260354` | `0x00023000` |

Flags objeto `+0x1c`: `0x80`, `0x08`, `0x04`, `0x02`, `0x01`, `0x40`.

## BonesWalk — IDs

`0x00020800`, `0x00010700`, `0x00020600`, `0x00011000` (e máscaras em `param_3`).

## Loader message board (02 @ `0x00200000`)

| VA | Papel | Match | Nativo |
|---|---|---|---|
| `0x0020b264` | **post** — free-slot scan, append tail `{id,value}` | — | `bones_bbs_post` |
| `0x0020b2e8` | **peek** — `(id & mask)==value`, sem unlink | `0x20b35c` | `bones_bbs_peek` |
| `0x0020b308` | **consume** — match + unlink (clear id) | `0x20b35c` | `bones_bbs_consume` / `probe` |
| `0x0020b2a4` | **unlink** nó conhecido | — | (interno) |
| `0x0020b35c` | **match** cursor walk | — | (interno) |

Dumps: `docs/ghidra-message-probe.txt`, `docs/ghidra-bbs-match.txt`, `docs/ghidra-bbs-peek.txt`.
Nativo: pool fixo 32 + links por índice (FIFO).

### Stand — qual API por ID

| ID | Fn Stand | Efeito observado |
|---|---|---|
| `0x00020A00` boot | **consume** `0x20b308` (`DAT_0025fff0`) | path flag `+0x1c & 0x80` |
| `0x00020800` | consume `0x20b308` | switch via `0x25d534` |
| `0x00020600` | **peek** `0x20b2e8` | com flag `+0x1c & 4` / `& 0x40` |
| `0x00010700` | **peek** `0x20b2e8` | sob flag `& 2` → `0x204128(0xc)` + `0x25d534` |
| `0x00011001` / `0x00011000` | peek | chama **BonesWalk** `0x260370` |

`0x204128(0xc)` (loader): enfileira nós da tabela índice `0xc` no ring do scheduler — **não** escreve tall direto.
Tall em InputBrain = célula `*(0x28DB8C)` (`PTR` em `0x26a6b8`); quem escreve ainda não isolado.

### InputBrain → célula `0x2858BC` (não é world-X confirmado)

| Condição | Delta em `*0x2858BC` |
|---|---|
| L nontall | +3932 |
| L tall | +1310 |
| R tall | -2621 |
| R nontall | **no-op** |
| idle nontall | +1966 |

Nativo aplica L/R em `bones_x` (host-facing). R-nontall no original **não** move essa célula; idle nontall **soma** 1966 — evidência de que `0x2858BC` ≠ posição mundo simples. Aproximação R host **preservada** até haver caminho world-X reconstruído.

## TallSet (`0x0026d784`) — writer de `0x28DB8C`

```
tall = 1  iff  *current_brain_slot(0x28589C) ∈
  { 0x2873EC, 0x2874E4, 0x2876D4, 0x2877CC,
    0x2879BC, 0x287AB4, 0x287F8C, 0x288084 }
else tall = 0
```

Literal sites: `0x26a6b8` (InputBrain read), `0x26d768` (TallBrain read), `0x26d7f4` (TallSet write).  
Caller pool @ `0x26d504` → TallSet + TallBrain `0x26d68c`.

## Fase nativa (§12f → §12m)

- `runway_input` na fase: posts Down/C + `input_cell`.
- §12m tall: `MSG_DOWN` → `0x204128(0xc)` flag + BrainSwitch crouch slot → `TallSet_26D784` (sem proxy Down-held).
- C edge → `0x10600`/`0x20600`; boot consume `0x20A00`.
- Walk L/R host-facing; R-nontall diferença preservada.
- SFX: walk / tall-enter / C (PCM8 experimental).
- §12m: `15PAN` 2651 frames (`pan_scroll` visual; coll=frame0); `mplay_seq` cue `0xc`.

## Artefatos

- Dump C/listing: `docs/ghidra-runway-brains.txt`, `docs/ghidra-message-probe.txt`, `docs/ghidra-bbs-peek.txt`, `docs/ghidra-stand-switch.txt`
- Scripts: `tools/DumpRunwayBrains.java`, `tools/DumpProbe.java`, `tools/DumpBbsMatch.java`, `tools/DumpBbsPeek.java`, `tools/DumpStandSwitch.java`
- Binários: `tools/ghidra-work/03CODE.bin`, `02LOADER.bin`
