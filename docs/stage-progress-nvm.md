# Progressão, shadow NVM e troca de disco: `stage_progress` e `stage_catalog`

Auditoria da lógica de progressão do port contra os dumps do carregador e do menu. O que foi
conferido, o que estava errado e foi corrigido, e o que continua sem evidência. **Fonte de verdade:**
[`nvm_shadow.h`](../src/core/nvm_shadow.h) e [`stage_progress.h`](../src/core/stage_progress.h); se este
texto divergir do código, o código vence.

## 1. O que os dumps mostram

- **`LoadFirstLevel` (`0020b91c`, [`ghidra-loader-backup.txt`](ghidra-loader-backup.txt)).** O literal
  `0020b9c0` vale `0x00228950`: é esse bloco que ele trata. Backup vazio → zera `[0..0x1b]`,
  `byte[4] = 1`, `u32@0x1c = 3`, `u32@0x20 = 0`. Depois lê o par em `0x228970` (literal `0020b9c8`),
  isto é, **`+0x1c` = nível a carregar e `+0x20` = origem do filme, dentro do próprio shadow**: com
  origem 0 e nível ≠ 3, `LoadLevel(nível)`; com nível 3, `PlayNewDuckMovie(0x1d, 3)` (`29INTRO`,
  depois `03MENU`); com origem definida, `PlayNewDuckMovie(origem, nível)`.
- **`BackupLoadNvmShadow` (`0020bce0`).** `r6 = 0x00228950` (literal `0020bd1c`), copia `0x28` bytes
  e grava `"BONE"` (`0x424f4e45`, literal `0020bd20`) em `+0x24`.
- **`UpdateLevels_CMenuSelector` (`0023c5a8`, [`ghidra-menu.txt`](ghidra-menu.txt),
  [`ghidra-menu-level-table.txt`](ghidra-menu-level-table.txt)).** Tabela em `0x00228950` (literal
  `0023c600`), o mesmo shadow. A tela de seleção (`0023f4a8`) tem 18 entradas; o deslocamento de cada
  uma (`entrada + 8`) é 4, 5, 6, 7, 7, 8, 8, 9, 0xa na coluna da esquerda e 0x12, 0x13, 0x13, 0x14,
  0x14, 0x15, 0x16, 0x17, 0x17 na da direita: **o ID do diretório** (`04RUNWAY..10BAT3` e
  `18RIDE..23MOAT`). As entradas com `+0 = 6` repetem o byte da anterior e trazem `ID-1` em `+0x10`.
  LOGS, DRUM, GLASS, BIGBNS, VORTEX, STAING, GENRM, LAFF e FINALE não aparecem na seleção.
- **`ShootRing` de FINALE** ([`finale-ring-analysis.md`](finale-ring-analysis.md)) grava 1 no byte
  `0x00228951` (`+1`) ao apagar o anel.

O endereço `0x228980` que o port usava vinha do `.MAP`; os três literais do código dizem `0x228950`.
Nada liga a troca de disco ao shadow: o carregador fica residente e o bloco fica em RAM.

## 2. Layout

| Bytes | Uso | Evidência |
|---|---|---|
| `0` | desconhecido, preservado | zerado no save vazio |
| `1` | campanha concluída (FINALE) | `ShootRing` |
| `2..3` | desconhecidos, preservados | zerados no save vazio |
| `4..0x1b` | **um byte por ID de diretório**, `byte[ID]` para `04RUNWAY..27FINALE` = `byte[4 + índice de campanha]`; 0 = travada | deslocamentos do menu = IDs; `byte[4] = 1` no save vazio = RUNWAY |
| `0x1c..0x1f` | `u32` BE: ID do nível a carregar na partida (3 = MENU) | `LoadFirstLevel` lê e decide por ele |
| `0x20..0x23` | `u32` BE: ID da origem do filme (0 = nenhum) | idem |
| `0x24..0x27` | `"BONE"`, gravado pelo carregador ao ler; preservado | `BackupLoadNvmShadow` |

O port indexa por índice de campanha (`MB_NVM_LEVEL_ID(i) = i + 4`), que é o ID do diretório. A
palavra `0x1c` recebe o ID da próxima fase (3 = MENU quando a campanha acaba); `menu_phase` grava o
ID da fase escolhida.

## 3. O que estava errado e foi corrigido

1. **Byte 4 contava duas vezes.** O layout anterior punha os bytes de fase em `[0..27]` e, ao mesmo
   tempo, gravava `byte[4] = 1` como "flag" do `LoadFirstLevel`. Resultado: `mb_nvm_level_unlocked(4)`
   era 1 num save novo, e `08BAT1` (índice 4) aparecia desbloqueada no menu desde o início. Agora os
   bytes de fase começam em 4 e `byte[4]` **é** a RUNWAY.
2. **Palavra de nível em unidade errada e apontando para um TROLL.** `stage_progress` gravava
   `índice atual + 1` (depois de `11LOGS`, 8 = `12TROLL1`, vazio) e `menu_phase` gravava o índice da
   fase escolhida, enquanto o `LoadFirstLevel` lê ali um **ID de diretório** (3 = MENU). Agora os dois
   gravam o ID: a próxima fase jogável depois de uma vitória (14 = `14DRUM` depois de LOGS, 3 = MENU ao
   fim da campanha) e a fase escolhida no menu.
3. **`stage_progress_apply_nvm` era um laço vazio** sem chamadores. Removida.

## 4. O que foi conferido e está certo

- **Troca de disco.** O `mb_nvm_shadow` nunca é passado a `stage_runner`; `NEED_DISC → resume` e
  `remount_disc` em `mrbones.c` não o tocam; cada vitória grava o arquivo inteiro (`0x28` bytes)
  antes de a próxima fase começar, então o que RIDE encontra é byte a byte o que VORTEX deixou.
  `stage_catalog` marca `18RIDE` como o primeiro diretório que prefere o disco 2 e `17VORTEX` o
  último do disco 1.
- **`stage_progress_skip_empty`.** Pula `12TROLL1`/`13TROLL2` em `begin`, em `on_outcome` e no
  desbloqueio, dando `11LOGS → 14DRUM`, o mesmo salto que o cérebro de LOGS faz com
  `PlayDuckMovie(14)` ([`fmv-routes.md`](fmv-routes.md)). Os bytes dos TROLLs ficam 0 para sempre.
  `NULL` e o fim da campanha continuam `NULL`.
- **Derrota e abandono** vão para `/28LOSE`, e `/28LOSE` vai ao MENU, sem escrever no shadow. É a
  rota do resultado 1 dos cérebros; o resultado 2 (menu direto) não tem equivalente.
- **Bytes fora do modelo** (`0..3`, `0x20..0x27`) sobrevivem a vitórias, gravação e leitura.

## 5. Testes

- `test_nvm_shadow` (sem disco): imagem inicial byte a byte, só a RUNWAY aberta, desbloqueios só em
  `[4..0x1b]`, índices fora da tabela ignorados, bytes não modelados preservados em gravação e
  leitura, acessores não cruzam o fim, arquivo curto devolve -1 e os padrões.
- `test_stage_progress`, parte sem disco: catálogo sintético com os 24 diretórios, 22 vitórias,
  TROLLs pulados, byte de fase e palavra de nível conferidos a cada vitória, marcadores intactos, o
  arquivo gravado na vitória de VORTEX igual à memória com que RIDE começa, derrota/abandono sem
  escrita.
- `test_stage_progress`, parte com discos: `NEED_DISC → resume` em GENRM não altera a imagem.

## 6. Feito depois da confirmação

- **Byte `+1`:** `stage_progress_on_outcome` grava 1 em `MB_NVM_COMPLETED_OFF` na vitória de FINALE,
  antes de salvar, como o `ShootRing` ao apagar o último anel.
- **`"BONE"` em `0x24`:** `mb_nvm_init_default` e `mb_nvm_load` gravam `MB_NVM_TAG_VALUE` em
  `MB_NVM_TAG_OFF`, por cima do que o arquivo tiver, como o `BackupLoadNvmShadow`.
- **Partida pela palavra de nível:** `stage_progress_begin(NULL)` começa na fase cujo ID está em
  `0x1c` quando ele vale 4..0x1b (um TROLL avança para a próxima jogável); 3 (MENU), 0, 28 ou lixo
  caem na primeira fase aberta.
- **Seleção do menu:** `menu_phase` só lista as 13 fases da `LevelSelect` original (IDs 4..10 e
  18..23), guarda o `dir_id` de cada uma, lê o desbloqueio por `byte[dir_id]` e grava o ID escolhido
  em `0x1c`.

## 7. Limites

- Os bytes `0`, `2..3` e a origem do filme (`0x20`) são preservados, não interpretados.
- `mb_nvm_load` devolve -1 para um arquivo curto e volta aos padrões; `mrbones.c` e `menu_phase.c`
  ignoram o retorno, então um arquivo corrompido reinicia o progresso em silêncio.
- A `LevelSelect` original tem 18 entradas para 13 fases (as de `+0 = 6` repetem o byte da anterior);
  o port mostra uma linha por fase.
