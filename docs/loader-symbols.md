# Loader symbol revalidation (`/02`)

Source binary: `Neomaster\mr-bones-disc1-02.bin`  
SHA-256: `02faf276ff9892d82c4b41dbfd8e6515f79b87f8883c731fa3d8aa1610d33704`  
Load base used for flat analysis: `0x00200000`  
Ghidra dump: `docs/ghidra-objyeild.txt` (headless SuperH:BE:32:SH-2)

Do **not** apply `LOADER/LOADER.MAP` addresses blindly. Revalidate each symbol.

| Symbol (map name) | Map address | Observed start | Status |
|---|---|---|---|
| `_ObjYeild` | `0x002050b0` | `0x002050b0` | **Match.** Prologue `sts.l pr` + r8–r14 saves; PC literal `0x00205058` is ring cursor; body saves SP, loads next SP, advances cursor by two words. |
| `__ObjNewBrain` | `0x00205168` | `0x00205168` | **Match.** Resets object word0 from `+0x28`, installs brain continuation. |
| `_Delay__7CObjecti` | `0x002051e4` | `0x002051e4` | **Match.** Loops calling yield `count` times. |
| `_FirstCall__7CObject` | `0x00205238` | `0x00205238` | **Match** by contiguous analysis. |
| `_LastCall__7CObject` | `0x00205214` | `0x00205214` | **Match** by contiguous analysis. |
| `_SMPCgetPad` | `0x0020bf34` | **`0x0020bf08`** | **Map mid-function.** Real function prologue at `0x0020bf08`; `0x0020bf34` is an interior instruction. RUNWAY literal `0x002697e0` → `0x0020bf08`. |
| `_CreateLoader` | `0x0020b860` | **`0x0020b834`** | **Map late.** Plausible full prologue at `0x0020b834`; map address falls after early setup. |

No single global offset reconciles every divergence.

Native adaptations: `src/core/obj_scheduler.c`, `src/core/joypad_brain.c`, `src/core/saturn_pad.c`.
