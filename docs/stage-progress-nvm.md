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
| `0` | desconhecido, preservado; **não** é opção de áudio (seção 8) | zerado no save vazio |
| `1` | campanha concluída (FINALE) | `ShootRing` |
| `2..3` | desconhecidos, preservados; **não** são preset de botões nem volume (seção 8) | zerados no save vazio |
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
- As duas opções originais são **MUSIC** (`0x0021d9dc`) e **STEREO** (`0x0021d9e0`), pela tela
  OPTIONS no Ymir e pela decompilação do carregador (seção 8). Não há interruptor de efeitos; a linha
  SFX do port não tem correspondente no original.

## 8. Tela de opções, StartScreen e attract

Dump: [`ghidra-menu-options.txt`](ghidra-menu-options.txt), só palavras de dados, gerado por
`tools/DumpMenuOptions.java`.

**A hipótese de áudio, preset de botões e volume em `+0x00`, `+0x02` e `+0x03` do shadow não se
confirma.** O que o binário mostra:

- **`OptionsScreen` (`0023f650`)**: 3 entradas em `0023f644`, uma coluna, tela anterior
  `StartScreen`. Os tipos (`entrada + 0`) são 3, 4 e 5: abrir a `LevelSelect` e dois interruptores.
  Não há linha de modo de áudio, de preset de botões nem de volume.
- **`InitOptions_CMenuSelector` (`0023c608`)**: lê duas palavras de 32 bits, `0x0021d9dc` (literal
  `0023c66c`) e `0x0021d9e0` (literal `0023c67c`), e escolhe o sprite ligado/desligado de cada
  interruptor (`00236294`/`00236014` e `00236c1c`/`0023699c`). Os meios-literais `0023c668` e
  `0023c66a` valem `0x0300` e `0x00c0`: o valor gravado em `+0x34` de cada objeto e o deslocamento
  do segundo objeto dentro do seletor.
- **`SelectMenuEntry`, casos 4 e 5 (`0023cbc4`)**: invertem as mesmas duas palavras (literais
  `0023cd58` e `0023cd68`). O caso 5 também grava `0x80` ou `0` no byte `0x25a00483`, na RAM do
  driver de som.
- **Onde ficam.** `0x0021d9dc` e `0x0021d9e0` são dados do carregador, fora de
  `0x00228950..0x00228977`. Valores iniciais na imagem: 0 e 1. O carregador as relê (literais
  `00204100` e `00203fd4`, este junto de `0x25a00483`) e não as cita perto das rotinas de backup,
  que só copiam os `0x28` bytes. **As opções não são gravadas no backup**: valem até desligar.
- **Referências ao shadow em todo o `03MENU/03CODE`** (varredura de palavras): a base em `0023c600`
  (`UpdateLevels`) e em `0023d264`, e `0x00228962` em `0023cd38`, que é `+0x12`, o byte de `18RIDE`,
  lido pelo caso 1 quando o disco é o 2. Nenhuma aponta para `+0`, `+2` ou `+3`.

**Leitura no carregador.** Dump: [`ghidra-loader-options.txt`](ghidra-loader-options.txt), gerado
por `tools/DumpLoaderOptions.java` sobre o `/02` (SHA-256 `02faf276…33704`, base `0x00200000`). Os
literais iguais às duas palavras e ao byte do driver aparecem só em `00203fd4` (`0x0021d9e0`),
`00203fd8` (`0x25a00483`) e `00204100` (`0x0021d9dc`), e todas as cargas deles estão numa única
função, `00203db0`, a carga de som da fase: ela pede o recurso 5 da fase (`002030f0(5, 0)`, o
`05SND1` pela numeração dos arquivos), envia seus blocos à RAM de som e monta a fila de
`SoundEvent`:

- **`0x0021d9e0` (segundo interruptor, caso 5, inicial 1)**: logo depois da carga,
  `*(byte *)0x25a00483 = palavra ? 0x80 : 0`. É o mesmo byte e o mesmo valor que o caso 5 grava ao
  alternar no menu. Esse interruptor age, portanto, só sobre uma variável do driver 68K na RAM de
  som; não passa por `SoundEvent`.
- **`0x0021d9dc` (primeiro interruptor, caso 4, inicial 0)**: depois de `SoundEvent(0)` (sempre), a
  função chama `SoundEvent(1)` (`00204128`, literal `00204104`) **só se a palavra for 0**. Com o
  valor inicial, o evento 1 de cada fase é disparado ao carregá-la; ligado o interruptor, ele é
  omitido. Os dois casos do menu chamam `SoundEvent(2)` depois de alternar.

**Rótulos no jogo (Ymir 0.4.1, 10/11).** Disco 1, perfil isolado, entrada só pelo controle
emulado: abertura saltada com Start, START → OPTIONS. A tela mostra três linhas, na ordem das
entradas de `OptionsScreen` (tipos 3, 4 e 5): **LEVEL SELECT**, **MUSIC: ON** e **STEREO: OFF**.
Selecionar a segunda linha a mudou para **MUSIC: OFF**; selecionar a terceira, para **STEREO: ON**.
Capturas em `build-static/ymir-options-labels/` (fora do controle de versão).

Com a decompilação, o significado fica fechado:

| Palavra | Caso | Opção | Valor inicial | Efeito |
|---|---|---|---|---|
| `0x0021d9dc` | 4 | MUSIC | 0 = ON | em 1 (OFF), a carga de som da fase omite `SoundEvent(1)` |
| `0x0021d9e0` | 5 | STEREO | 1 = OFF | `0x25a00483` recebe `0x80` com STEREO OFF (mono) e `0` com STEREO ON |

As polaridades vêm de o boot mostrar ON e OFF com as palavras nos valores da imagem (0 e 1), que
nenhum outro código do carregador escreve, e de cada seleção inverter a linha correspondente. O
driver de som não foi analisado, então o uso interno do byte `0x25a00483` é inferido do rótulo. Os
ensaios foram feitos no menu; o efeito audível na fase não foi medido.

**Não existe opção de efeitos sonoros no original.** O port chama o caso 5 de SFX
(`menu_phase.sfx_on`, chave `sfx` do `mrbones.ini`) e o usa para suprimir efeitos; isso é extensão
do host. O original oferece ali saída estéreo ou mono.

Consequência no port: nenhuma macro de opções em `nvm_shadow.h`, e `menu_options` não foi ligado ao
shadow. Ligar gravaria significado inventado em bytes que o jogo trata de outro modo. O módulo
`menu_options` (modo de áudio, 6 presets, volume 0..15) é extensão do host.

**Persistência no host.** As opções do menu ficam no `mrbones.ini`, nunca no `nvmshadow.bin`:

- chaves `music` e `sfx` (`on`/`off`, também `true`/`false`, `yes`/`no`, `1`/`0`) e `volume`
  (`0..15`); padrões ligado, ligado e 15; valor inválido mantém o padrão e volume acima de 15 vira 15;
- `mrbones.c` aplica `music`, `sfx` e `volume` depois de cada carga interativa do menu e chama
  `config_save_options` quando o jogador muda qualquer uma das três; o volume muda com Left/Right
  na linha `VOLUME: n/15` da tela de opções (`menu_phase.volume`, `0..MENU_VOLUME_MAX`);
- a cada quadro do menu, `host_audio_set_volume` publica o nível e as saídas CD-DA, PCM8 de efeitos
  e PCM16 dos filmes o aplicam por `waveOutSetVolume` quando ele muda, sem reiniciar o som; `sfx`
  desligado suprime os efeitos das fases e `music` desligado para a faixa do menu;
- `config_save_options` reescreve só essas três chaves e mantém as outras linhas, para que um
  caminho vindo de variável de ambiente não fique gravado no arquivo.

**`StartScreen` (`0023e0f4`)**: 2 entradas em `0023e0ec`, uma coluna, sem tela anterior.

| Entrada | Tipo | `+0x0c` | `+0x10` | Ação em `SelectMenuEntry` |
|---|---|---|---|---|
| 0 | 1 | 4 | 3 | disco 1: `00239ae8(3, 4)`, lido como filme de origem 3 e fase 4 (`04RUNWAY`); disco 2: `00239ae8(3, 0x12)` (`18RIDE`) se `byte[0x12]` estiver aberto, senão `00239abc(4)` |
| 1 | 2 | 0 | 0 | abre a `OptionsScreen` |

**Attract e inatividade.** `Brain_CMenuSelector` (`0023cda0`) não tem contador de tempo:

1. se `*0x002143f0 == 0`, chama `DoLeagal`, que marca a palavra com 1 e espera 200 quadros
   (`CObject_Delay`, literal `0023c594` = `0x00c8`): a tela legal aparece uma vez por ligação;
2. se `*0x0021dabc != 0`, mostra a tela de erro de backup;
3. abre a `StartScreen` e entra num laço sem saída. A cada quadro chama `002084ea`, que só avança o
   gerador pseudoaleatório do carregador, e `ObjYeild` (`002050b0`), e lê o controle em `0x0022fb0c`:
   `0x40`/`0x80` movem na horizontal, `0x10`/`0x20` na vertical, `0x0e` seleciona, `0x01` ou os bits
   `0x70` do segundo byte voltam uma tela.

**Não existe timeout que reinicie a demonstração.** O filme de `29INTRO` toca uma vez, na partida,
quando a palavra de nível vale 3 (`LoadFirstLevel`, seção 1). Parado no menu, o jogo fica no menu.
O port também não tem timeout em `menu_phase`, o que está de acordo.

## 9. Rota de `/28LOSE` depois da própria reprodução

Auditoria de `docs/fmv-routes.md` seção 8: `stage_progress_on_outcome` tratava o fim normal do filme
de `/28LOSE` (um resultado `WIN` daquele estágio só-de-filme, não uma vitória de campanha) do mesmo
jeito que a vitória da FINALE — as duas devolviam `STAGE_ROUTE_COMPLETE`. Em `mrbones.c`, o desvio que
manda a FINALE ao menu só testava `STAGE_KIND_FINALE`, então o fim de `/28LOSE` não caía nesse desvio e
chamava a tela de "créditos" do host (`enter_credits`, `credits_build_frame`) — o oposto do original,
cujo resultado 1 (derrota) vai direto a `03MENU` sem nenhum passo de créditos (`fmv-routes.md` seção 3).

**Corrigido:** `STAGE_KIND_LOSE` agora devolve `STAGE_ROUTE_MENU` diretamente, igual ao abandono
(`STAGE_OUTCOME_LOSE`/`ABORT`, que já tinha essa rota). `28LOSE` está fora de `04..27`, então nunca
esteve em `in_campaign` e o bloco de desbloqueio já deixava a nvm intocada nos dois casos; só a rota
mudou. Com isso `enter_credits()` fica inatingível em `mrbones.c` por qualquer caminho real: a vitória
da FINALE (único caso que ainda gera `STAGE_ROUTE_COMPLETE`) já era desviada ao menu antes desse
`if`. Teste em `tests/test_stage_progress.c`: `on_outcome("/28LOSE", STAGE_OUTCOME_WIN)` dá
`STAGE_ROUTE_MENU`, `next_iso` vazio, nvm byte a byte intocada.
