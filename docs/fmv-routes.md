# Rotas de FMV: os cérebros de término das 22 fases

O que decide, ao fim de cada fase, se o jogo carrega a próxima direto ou passa antes por um filme é o
`CEndLevel::Brain` do `03CODE` de cada fase. Este documento junta: a tabela das rotas de vitória, o que
mais os mesmos cérebros mostram (derrota, saída, campos opcionais), como o carregador trata cada chamada,
o que o port faz com isso e o que foi visto no Ymir. **Fonte de verdade:** os dumps em
[`ghidra-fmv-brains/`](ghidra-fmv-brains/); se este texto divergir deles, os dumps vencem.

## 1. De onde vêm os dumps

Cada `03CODE` foi extraído do disco indicado e importado como `SuperH:BE:32:SH-2` com base `0x00232000`
(a base `0x00200000` gera instruções inválidas; ver [`fmv-mauso-flow.md`](fmv-mauso-flow.md)). O endereço
do cérebro é o prólogo revalidado no binário, e o `.MAP` pode divergir em alguns bytes (GUITAR: 16 antes;
BAT3: 12 depois). O SHA-256 da tabela identifica o `03CODE` analisado.

O script que reproduz um dump é [`tools/DumpFmvBrain.java`](../tools/DumpFmvBrain.java), com
`analyzeHeadless … -postScript DumpFmvBrain.java <fase> <cérebro> [saída]`. Além do C descompilado, ele
imprime um cabeçalho de procedência (programa, linguagem, base, SHA-256), o prólogo e os literais que a
função lê, com o valor de cada um. Os dumps atuais não têm esses blocos: o script que os gerou não está
no repositório, e `DumpFmvBrain.java` produz o mesmo corpo e acrescenta os três. O carregador e o menu
têm o seu próprio script, [`tools/DumpMovieFlow.java`](../tools/DumpMovieFlow.java).

## 2. Rota de vitória

Condição: resultado em `+0x44` igual a 0, campos opcionais `+0x48` e `+0x4c` iguais a -1. O argumento é
o ID da próxima fase, o prefixo numérico do diretório (`04RUNWAY` = 4). O filme, quando há, é o da fase
que terminou (§4).

| Fase | Disco | Cérebro | Literal | Destino | Próximo ID | SHA-256 do 03CODE |
|---|---:|---|---|---|---:|---|
| 04RUNWAY | 1 | `00256280` | `002563d0` | `LoadLevel` | 5 | `7532e4b8e804c6908ce03c987587612876a65663e56fa32b2174d022da9dc216` |
| 05MAUSO | 1 | `0029d918` | `0029da44` | `LoadLevel` | 6 | `013a6edf094b91c57d39a2390bd79a4f2056307958802fc3a7d92004cbe64d12` |
| 06VALLEY | 1 | `002be75c` | `002be8a0` | `PlayDuckMovie` | 7 | `98da3f5ccbd44012281db4c98d1dac60a9ec58ef8a5580e69f7ad35b8a7dc46c` |
| 07GUITAR | 1 | `00261ed8` | `00262008` | `PlayDuckMovie` | 8 | `ef567508e6b7b6988177c068853cca150735f51c52d73a59f014598eb0ed8e3a` |
| 08BAT1 | 1 | `002b2ed8` | `002b3018` | `LoadLevel` | 9 | `9dab1056057a645c550d5955403c8c6d7d3c2216bfe61fcbf2e8601e106ca80e` |
| 09BAT2 | 1 | `002b4774` | `002b48b4` | `LoadLevel` | 10 | `6bd9dd0250a8ccd4517386c80533768667b2f7028ee891ecf5b5e5dce0dbd839` |
| 10BAT3 | 1 | `002b5858` | `002b5998` | `LoadLevel` | 11 | `b3f97ad8c892fc01f73629994ff85992ac15aa04fdcb43dd1f19a203b77c73b0` |
| 11LOGS | 1 | `002a711c` | `002a7270` | `PlayDuckMovie` | 14 | `6a14ae591f9e4adc92f8accc20de7092bce924151460f2a50260734be3b8acc5` |
| 14DRUM | 1 | `00249770` | `00249850` | `PlayDuckMovie` | 15 | `a0d40d03eee16f6d119c553c4f8857df555f7cd62d3ce98a9651cd594f7b515d` |
| 15GLASS | 1 | `002ac120` | `002ac24c` | `LoadLevel` | 16 | `d5761817384ab29544c0d0959ddf06ddd7863fc2f12abfbe30019b11a560c253` |
| 16BIGBNS | 1 | `0023d7a0` | `0023d8f4` | `PlayDuckMovie` | 17 | `ce9fcd3d430487142ece55a89ee23932f2510bfe3b1c33b79655b04aaf145993` |
| 17VORTEX | 1 | `002676e4` | `00267814` | `PlayDuckMovie` | 18 | `8bff6726a3d8fa4cc0f26507764dea7cbeb02c9f2e6b2bfbf05fa752b3fae84e` |
| 18RIDE | 2 | `0023f49c` | `0023f5f0` | `PlayDuckMovie` | 19 | `10725ba33fa0d9a24e9ef9c8aa2531514d06d4a6e2302366e575e1690ca0cf8b` |
| 19VILLAGE | 2 | `0029ce94` | `0029cfc4` | `PlayDuckMovie` | 20 | `a9ab85f19ea925905004e111c1752c0b3b004edde32fe65fababdf31f26c479d` |
| 20ICE1 | 2 | `0029dcb0` | `0029de14` | `LoadLevel` | 21 | `2e60302a51da27b76b0eb5d0f041df74fdfe4bb22726f3130e074fd7466c29dc` |
| 21ICE2 | 2 | `00290958` | `00290abc` | `LoadLevel` | 22 | `f343894aac96114199fc0716e34b4ca05210e55cd53d10657fb26417aaa81a99` |
| 22SHADOW | 2 | `0029e528` | `0029e658` | `PlayDuckMovie` | 23 | `449dcd9410090035912438b06f4a02e8637df18d832a5ee360f2f282b24bf1b4` |
| 23MOAT | 2 | `002ae82c` | `002ae980` | `PlayDuckMovie` | 24 | `a66291ea2c05bf6a1146fa16fa73db71c637999f24e736a6c47dd4c5d9e9e12a` |
| 24STAING | 2 | `0023f7e0` | `0023f934` | `PlayDuckMovie` | 25 | `75f22dc9c9f04486d150025786467ed06afce43c134ae0a1740af5b39d4ea31d` |
| 25GENRM | 2 | `0023e0bc` | `0023e210` | `PlayDuckMovie` | 26 | `670ad213889949d2cfe51777d4d5800ca203be0ebee2c74648322f53cf951c01` |
| 26LAFF | 2 | `002951e8` | `00295318` | `PlayDuckMovie` | 27 | `ab073acb0bcd93dc84b56900241ccec8484f6af30519faf51601263b04931018` |
| 27FINALE | 2 | `0028ccd0` | `0028ce00` | `PlayDuckMovie` | 3 | `19962c17f9ca3c467b05229956e98058df72d05bf083e6c30006a05f8f59528b` |

Oito fases carregam a próxima direto e catorze passam por um filme. LOGS pula para 14 porque `12TROLL1` e
`13TROLL2` não têm `03CODE` real; FINALE volta a 3 (`03MENU`) depois do seu filme.

**O que está verificado e o que está inferido.** O valor do literal foi lido no binário em cinco fases:
MAUSO (`0029da44` → `0020b4ac`), VALLEY, GUITAR, BAT3 ([`fmv-valley-flow.md`](fmv-valley-flow.md)) e
FINALE (`0028ce00` → `0020b61c`, `0028cdfc` → `0020b4ac`, `0028cdf8` → `0020b714`). Nas outras
dezessete, o destino é inferido da forma da função, idêntica nas 22 (§3): quando o ramo do resultado 0
troca o ponteiro antes de chamar, o alvo é o terceiro literal do trio e o destino é `PlayDuckMovie`;
quando não troca, é o segundo, `LoadLevel`. A inferência se confirma rodando `DumpFmvBrain.java`, que
imprime os valores.

## 3. O que mais os mesmos cérebros mostram

Os 22 têm a mesma forma. 14DRUM é a única diferença de corpo: não tem os dois laços de espera de oito
iterações nem a chamada com `0x18`/`0x19`, mas a decisão final é a mesma. Depois da limpeza da fase, a
decisão lê três campos do objeto:

| Condição | Chamada | Efeito |
|---|---|---|
| `+0x4c` ≠ -1 | `PlayNewDuckMovie(+0x4c, +0x48)` | filme da fase `+0x4c`, depois carrega `+0x48` |
| `+0x4c` = -1, `+0x48` ≠ -1 | `LoadLevel(+0x48)` | carrega a fase pedida, sem filme |
| ambos -1, resultado 0 | `LoadLevel(próximo)` ou `PlayDuckMovie(próximo)` | a tabela do §2 |
| ambos -1, resultado 1 | `PlayNewDuckMovie(0x1c, 3)` | filme de `28LOSE`, depois `03MENU` |
| ambos -1, resultado 2 | `LoadLevel(3)` | `03MENU` direto, sem filme |
| ambos -1, outro resultado | nenhuma | a função retorna |

Os três alvos vêm de um trio de literais consecutivos, na mesma ordem em todas as fases:

| Fase | `PlayNewDuckMovie` | `LoadLevel` | `PlayDuckMovie` |
|---|---|---|---|
| 04RUNWAY | `002563cc` | `002563d0` | — |
| 05MAUSO | `0029da40` | `0029da44` | — |
| 06VALLEY | `002be898` | `002be89c` | `002be8a0` |
| 07GUITAR | `00262000` | `00262004` | `00262008` |
| 08BAT1 | `002b3014` | `002b3018` | — |
| 09BAT2 | `002b48b0` | `002b48b4` | — |
| 10BAT3 | `002b5994` | `002b5998` | — |
| 11LOGS | `002a7268` | `002a726c` | `002a7270` |
| 14DRUM | `00249848` | `0024984c` | `00249850` |
| 15GLASS | `002ac248` | `002ac24c` | — |
| 16BIGBNS | `0023d8ec` | `0023d8f0` | `0023d8f4` |
| 17VORTEX | `0026780c` | `00267810` | `00267814` |
| 18RIDE | `0023f5e8` | `0023f5ec` | `0023f5f0` |
| 19VILLAGE | `0029cfbc` | `0029cfc0` | `0029cfc4` |
| 20ICE1 | `0029de10` | `0029de14` | — |
| 21ICE2 | `00290ab8` | `00290abc` | — |
| 22SHADOW | `0029e650` | `0029e654` | `0029e658` |
| 23MOAT | `002ae978` | `002ae97c` | `002ae980` |
| 24STAING | `0023f92c` | `0023f930` | `0023f934` |
| 25GENRM | `0023e208` | `0023e20c` | `0023e210` |
| 26LAFF | `00295310` | `00295314` | `00295318` |
| 27FINALE | `0028cdf8` | `0028cdfc` | `0028ce00` |

As fases que carregam direto não têm o terceiro literal. A identificação do primeiro como
`PlayNewDuckMovie` tem o valor lido em MAUSO (`0029da40` → `0020b714`) e FINALE; nas demais, é a mesma
chamada com os mesmos argumentos (`0x1c, 3` e `+0x4c, +0x48`). O que os cérebros chamam com `0x18`
(resultado 0) e `0x19` (resultado 1) antes da decisão não foi identificado.

## 4. O carregador

Do `/02` (SHA-256 `02faf276ff9892d82c4b41dbfd8e6515f79b87f8883c731fa3d8aa1610d33704`, base `0x00200000`,
[`ghidra-fmv-loader.txt`](ghidra-fmv-loader.txt)):

- `_LoadLevel` (`0020b4ac`): grava a fase pedida em `0021d9e4` e zera `0021d9e8`.
- `_PlayDuckMovie(n)` (`0020b61c`): copia `0021d9e4` (a fase corrente) para `0021d9e8`, grava `n` em
  `0021d9e4`, roda o leitor e chama `_LoadLevel(n)`. **O filme é o da fase que terminou**; `n` é só a
  próxima. VALLEY chama `_PlayDuckMovie(7)`: toca `/06VALLEY/20AVI` e depois carrega `/07GUITAR`.
- `_PlayNewDuckMovie(de, para)` (`0020b714`): a origem do filme e a próxima fase são argumentos
  separados. É o que o resultado 1 usa: filme de `28LOSE`, depois `03MENU`.

## 5. No port

- [`src/tools/mrbones.c`](../src/tools/mrbones.c): `g_fmv_routes[]` guarda os treze pares (fase com filme
  → próxima) anteriores a FINALE, e `check_mode` repete a lista com o disco de cada um para simular as
  treze transições (`test_fmv_transition`). As duas listas são cópias uma da outra e precisam andar juntas.
- [`stage_catalog.c`](../src/core/stage_catalog.c) e [`stage_progress.c`](../src/core/stage_progress.c): a
  ordem da campanha é a dos diretórios 04 a 27; `stage_progress_skip_empty` pula `TROLL1`/`TROLL2`, o
  que dá LOGS → DRUM. `STAGE_OUTCOME_LOSE` e `STAGE_OUTCOME_ABORT` levam a `/28LOSE` e daí ao MENU, o
  equivalente do resultado 1. Não há equivalente do resultado 2 (menu direto, sem filme).
- FINALE: o port ainda inicia o AVI diretamente ao entrar na fase, sem reconstruir a arena original.
  Após o filme, `mrbones.c` encaminha `STAGE_ROUTE_COMPLETE` ao menu, conforme o destino do cérebro
  original e a referência de filme selecionado no Ymir. A vitória jogada e a transição normal da arena
  para o filme continuam pendentes; veja [`finale-ring-analysis.md`](finale-ring-analysis.md). A música
  do menu aguarda 4,54 s adicionais, calibrados pela comparação de áudio digital com essa referência.

## 6. Observado no Ymir (09/11)

Capturas citadas ficam em `build-static/ymir-finale-*/screenshots/`, fora do controle de versão.

- **FINALE, bytes do binário.** O literal `0028ce00` contém `0020b61c` (`PlayDuckMovie`), usado no ramo
  do resultado 0. O mapa identifica o cérebro `CFBones`, seus controles e `RingDefinitionTable`. Uma
  captura anterior com `00228970=27` selecionou o filme, não a fase.
- **FINALE, fase forçada.** Disco 2, `0022896c=27`, `00228970=0`: a arena apareceu com Mr. Bones e uma
  guitarra (`ymir-finale-review/…133600.304.png`). Sem comandos, a execução voltou ao menu; não houve
  vitória.
- **FINALE, cérebro.** Breakpoint em `0028ccd0` com `R4=06026a9c`, resultado `+0x44=1` e
  `+0x48`/`+0x4c=-1`: o resultado 1 é a rota de derrota do §3, coerente com a volta ao menu acima. Trocar
  o resultado de 1 para 0 em RAM levou ao filme de FINALE; a captura `ymir-finale-winref/…134500.548.png`
  coincide em RGBA com o intervalo 288 do AVI decodificado pelo port. Isso confirma o ramo sob resultado
  forçado, não a condição de vitória normal.
- **FINALE, filme até o menu.** Em nova execução da arena com resultado 1 alterado para 0 em RAM,
  o filme completo terminou no menu START. Três capturas dos créditos coincidiram em RGBA com os
  intervalos 2358–2418, 3963–4008 e 5366 decodificados pelo port. A captura de áudio exclusiva do
  processo Ymir mediu 4,771 s entre o fim do PCM do filme e o início da faixa 02 do disco 2;
  o port medido anteriormente apresentou 4,712 s. Evidências e análise em
  `build-static/ymir-finale-win-boundary/`. A vitória continua sendo forçada pelo depurador.
- **GUITAR** (`0022896c=7`, `00228970=0` em `0020b95c`): a fase Guitar Solo, personagem com guitarra
  diante da plateia. `/07GUITAR/20AVI` é uma cena diferente; sua chamada depois de GUITAR vem da
  descompilação, não de uma vitória observada.
- **DRUM** (`0022896c=14`): personagem entre tambores; `/14DRUM/20AVI` é outra sequência. Mesma ressalva.
- **BIGBNS** (`0022896c=16`): fase jogável com céu azul; um quadro de `/16BIGBNS/20AVI` é diferente. O
  mapa não mostra símbolos do leitor DUCK. Mesma ressalva.
- **GENRM** (`0022896c=25`, disco 2): fase jogável com inimigos e cenário circular; o quadro 100 de
  `/25GENRM/20AVI` é uma cena distinta. O mapa GENROOM não tem `MFP_`, `DUCKPLAY`, `DUCKFILE`, `DXL_` nem
  `20AVI`. Mesma ressalva.

## 7. Limites
## 8. Créditos finais pós-FINALE

Auditoria pedida: a "tabela de créditos finais" em `/27FINALE` e a chamada a `DirNameTable` que
`mrbones.c` faz depois da FINALE, para ver se a rolagem e os nomes da equipe no port correspondem
aos dados originais da ROM. **Não correspondem, e não podem**, pelo que os dumps mostram:

- **Não existe tabela de créditos como dado isolado.** Nenhum dump deste repositório (`ghidra-endgame.txt`,
  `ghidra-finale-detail.txt`, `finale-ring-analysis.md`, `finale-handoff-27676.md`) cita uma rotina ou
  estrutura de créditos/equipe. `_DirNameTable` (`stage_catalog.c`, base `0022da00`) guarda só os 28
  nomes de diretório do disco (`04RUNWAY`..`30MAP`); nenhum é nome de pessoa.
- **O conteúdo de encerramento real está dentro do filme.** Na seção 6 acima, três capturas do Ymir em
  créditos coincidiram em RGBA com os intervalos 2358–2418, 3963–4008 e 5366 do AVI decodificado pelo
  port — ou seja, **os créditos da Sega Saturn são quadros do próprio `/27FINALE/20AVI`** (36.680.552
  bytes no disco 2), não uma tabela de texto. O `/27FINALE` do disco 1 tem todos os recursos "extra"
  vazios (hash de string vazia no manifesto); só o disco 2 carrega a FINALE completa.
- **A "chamada DirNameTable pós-FINALE" em `mrbones.c` é `credits_build_frame`.** Ela usa
  `stage_catalog_dir_name()`/`stage_catalog_dir_count()` — isto é, o `_DirNameTable` — para listar
  `"STAGE NN: /DIRNAME"` numa tela própria do host, rotulada "CAMPAIGN STAGES (DIRNAMETABLE)" e
  "CREDITS". É uma tela inventada que reaproveita nomes de diretório como se fossem crédito de equipe;
  não há como ela corresponder à ROM, porque a ROM não expõe nomes de equipe por essa tabela nem por
  nenhuma outra achada até aqui.
- **Essa tela estava, além disso, inalcançável na vitória e alcançável só na derrota.** Antes desta
  auditoria, `stage_progress_on_outcome` tratava o fim da reprodução de `/28LOSE` (um resultado WIN
  daquele estágio só-de-filme) do mesmo jeito que a vitória da FINALE, devolvendo
  `STAGE_ROUTE_COMPLETE` nos dois casos. Em `mrbones.c`, o desvio para o menu só testava
  `STAGE_KIND_FINALE`, então: a vitória real da FINALE caía no desvio e ia direto ao menu (sem nunca
  chamar `enter_credits()`), e o fim do filme de `/28LOSE` **não** caía no desvio e acionava
  `enter_credits()` — a tela de "créditos" aparecia depois de perder, nunca depois de vencer. O cérebro
  original não tem passo de créditos no resultado 1 (derrota): filme de `28LOSE` e direto a `03MENU`
  (§3 acima). Corrigido em `stage_progress.c` (`docs/stage-progress-nvm.md` seção 9): `STAGE_KIND_LOSE`
  agora devolve `STAGE_ROUTE_MENU` diretamente, igual ao abandono. Com isso `enter_credits()` fica
  inatingível em qualquer jogo real (comentário em `advance_after_stage`, `src/tools/mrbones.c`).
- **Não decodifiquei os quadros do AVI para comparar pixel a pixel com a tela do host.** Extrair
  quadros do filme para checar a equipe exigiria ler o disco além do que os testes sintéticos cobrem,
  contra a restrição de não extrair dados brutos do disco. A conclusão acima já basta: são fontes de
  dado diferentes (quadros de vídeo vs. uma tela gerada pelo host), então "corresponder" não se aplica.

- Os dumps mostram as rotas estáticas. Não provam que os campos opcionais, ou resultados diferentes de 0,
  1 e 2, nunca sejam usados em uma partida, nem quem os configura.
- O destino de dezessete fases é inferido da forma da função (§2); o valor do literal só foi lido em cinco.
- Nenhuma vitória normal foi observada no Ymir: todas as entradas foram forçadas.
- Os dumps atuais não têm cabeçalho de procedência nem os valores dos literais; o script que os produziu
  não está no repositório.
