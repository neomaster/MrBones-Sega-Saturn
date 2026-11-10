# Progressão, shadow NVM e troca de disco: `stage_progress` e `stage_catalog`

Auditoria da lógica de progressão do port contra os dumps do carregador e do menu. O que foi
conferido, o que estava errado e foi corrigido, e o que continua sem evidência. **Fonte de verdade:**
[`nvm_shadow.h`](../src/core/nvm_shadow.h) e [`stage_progress.h`](../src/core/stage_progress.h); se este
texto divergir do código, o código vence.

## 1. O que os dumps mostram

- **`LoadFirstLevel` (`0020b91c`, [`ghidra-loader-backup.txt`](ghidra-loader-backup.txt)).** Quando o
  backup está vazio: zera os bytes `[0..0x1b]` do `_NvmShadow`, grava `byte[4] = 1`, `u32@0x1c = 3`
  e `u32@0x20 = 0`. Depois lê o par de palavras do carregador (`0021d9e4` nível, `0021d9e8` origem
  do filme): com origem 0 e nível diferente de 3 chama `LoadLevel(nível)`; com nível 3 chama
  `PlayNewDuckMovie(0x1d, 3)` (filme de `29INTRO`, depois `03MENU`); com origem definida chama
  `PlayNewDuckMovie(origem, nível)`. Esse par não faz parte do `_NvmShadow`.
- **`BackupLoadNvmShadow` / `BackupSaveNvmShadow` (`0020bce0` / `0020bd28`).** Copiam `0x28` bytes
  (`mov #0x28, r7`). O dump não recupera o corpo da função de salvar.
- **`UpdateLevels_CMenuSelector` (`0023c5a8`, [`ghidra-menu.txt`](ghidra-menu.txt)).** Para cada
  entrada da seleção de fases lê **um byte** em `tabela + *(entrada + 8)` e grava o resultado em
  `entrada + 4` (desbloqueada). A tabela e os deslocamentos por entrada não foram recuperados.

Nada nos dumps liga a troca de disco ao `_NvmShadow`: o carregador fica residente em `0x00200000`
e o shadow fica em RAM entre as fases; o port tem de reproduzir isso não tocando na imagem na troca.

## 2. Layout usado pelo port

Uma leitura dos fatos acima, **não uma estrutura decodificada**:

| Bytes | Uso no port | Evidência |
|---|---|---|
| `0..3` | cabeçalho, preservado | zerado pelo `LoadFirstLevel`, nunca lido |
| `4..0x1b` | 24 bytes de fase, um por diretório `04RUNWAY..27FINALE` na ordem da `DirNameTable`; 0 = travada | `byte[4] = 1` no save vazio = RUNWAY aberta; 24 bytes cabem exatamente em `4..0x1b`; o menu lê um byte por entrada |
| `0x1c..0x1f` | `u32` BE: índice de campanha da próxima fase a jogar | valor inicial 3 vem do `LoadFirstLevel`; o significado original não está nos dumps |
| `0x20..0x27` | dois `u32`, preservados | `0x20 = 0` no save vazio; `0x24` recebe algo em tempo de execução no `BackupLoadNvmShadow` |

A ordem dos 24 bytes é escolha do port: o menu original usa deslocamentos por entrada que não
foram lidos.

## 3. O que estava errado e foi corrigido

1. **Byte 4 contava duas vezes.** O layout anterior punha os bytes de fase em `[0..27]` e, ao mesmo
   tempo, gravava `byte[4] = 1` como "flag" do `LoadFirstLevel`. Resultado: `mb_nvm_level_unlocked(4)`
   era 1 num save novo, e `08BAT1` (índice 4) aparecia desbloqueada no menu desde o início. Agora os
   bytes de fase começam em 4 e `byte[4]` **é** a RUNWAY.
2. **Palavra de nível apontava para um TROLL.** `stage_progress` gravava `índice atual + 1`, que depois
   de `11LOGS` dá 8 (`12TROLL1`, vazio), enquanto `menu_phase` grava o índice da fase selecionada.
   Agora grava o índice da **próxima fase jogável** (depois de `11LOGS`, 10 = `14DRUM`), a mesma
   unidade do menu.
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

## 6. Limites

- A ordem dos bytes de fase e o significado de `0x1c` e `0x20` não foram confirmados no original.
- `stage_progress_begin(NULL)` ignora a palavra de nível e começa na primeira fase aberta, que é
  sempre a RUNWAY.
- `mb_nvm_load` devolve -1 para um arquivo curto e volta aos padrões; `mrbones.c` e `menu_phase.c`
  ignoram o retorno, então um arquivo corrompido reinicia o progresso em silêncio.
- `menu_phase` indexa pelas suas próprias entradas (ordem da ISO com a RUNWAY em primeiro);
  coincide com o índice de campanha enquanto a ordem da ISO for a numérica.
