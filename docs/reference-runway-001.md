# Referência RUNWAY-001 (emulador)

**Estado:** preparada; **captura RUNWAY ainda não obtida.**

## Ambiente

| Item | Valor |
|---|---|
| Emulador | Ymir (`tools/ymir/ymir-sdl3.exe`, ~22.9 MB) |
| BIOS / IPL | `tools/ymir/roms/ipl/mpr-17933.bin` (+ cópia no perfil) |
| BIOS SHA-256 | `96E106F740AB448CF89F0DD49DFBAC7FE5391CB6BD6E14AD5E3061C13330266F` |
| Disco 1 | `F:\MrBonesPort\disco1\Mr. Bones (USA) (Disc 1).cue` |
| Perfil | `tools/ymir-profile` (`Ymir.toml` ConfigVersion=6) |
| CLI | `ymir-sdl3.exe -d <cue> -p <profile> [-P]` |

## Tentativas

1. `-d <cue> -p <profile> -P` → processo sobe; sem screenshot/savestate novo no perfil.
2. Working directory = `disco1` (FILE relativos do cue) → idem.
3. Args posicionais com cue → idem.

**Bloqueio:** sem evidência de RUNWAY em execução (imagem/áudio/savestate). Comparação nativo↔Ymir adiada; continuar com evidência estática.

## Checklist manual (quando o disco montar)

1. BIOS → idioma → título → MENU → RUNWAY.
2. Screenshot + SHA-256; savestate; notas de CDDA vs SFX.
3. Sequência de botões até clear/fail.
4. Preencher esta ficha e comparar com `mbrunway`.

## Nativo sem esta referência

A fase nativa foi validada por carregamento real do disco + controle + saída,
com diferenças listadas em `docs/coverage.md`. A ausência da captura Ymir é
uma diferença/risco de fidelidade explícito, não um sucesso silencioso.
