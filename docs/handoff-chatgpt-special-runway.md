# Handoff de Especial e Runway — edição americana

## FINALE, 21/11/2026

O quadro 486 e a área do jogo apresentada pelo GDI coincidiram com o Saturn nas fitas curta e deslocada. A janela 443–496 teve 54 quadros RGB idênticos por fita, incluindo a sustentada. Nas três fitas completas, 7008 de 7.440 quadros coincidiram integralmente. A contagem anterior era 6852. Nenhum quadro antes idêntico perdeu a paridade. Permanecem diferenças fora dessa janela. [Comparação por quadro](../build-chatgpt/evidence/full-rgb.csv) · [Prancha do quadro 486](../build-chatgpt/evidence/486.png)

| Fita | Idênticos antes | Idênticos depois | Pixels divergentes no 486 | Vitória | Pedido do filme | Filme visível |
|---|---:|---:|---:|---:|---:|---:|
| Sustentada | 2284 | 2336 | 0 | 2064 | 2127 | 2286 |
| Curta | 2290 | 2342 | 0 | 2060 | 2123 | 2282 |
| Deslocada | 2278 | 2330 | 0 | 2062 | 2125 | 2284 |

O Ymir 0.4.1 carregou o disco 2 e o mesmo estado de seleção em três execuções independentes. O SHA-256 do estado é `df644123c8c744f36a28818ba985f84c60bab65b4377c7322a55615bad88552a`. O primeiro comando da fita entra no VBlank absoluto 557. O núcleo funcionou sem dispositivo de áudio. O comparador conferiu cada máscara física e todos os 48 impactos contra a troca dos bits do anel. [Hashes das fitas, eventos, estados e quadros](../build-chatgpt/evidence/full-rgb.json)

`CFace` usa a vtable `0x28E154` e a animação `0x2818BC`. O callback em FireBolt na direção norte leva a SetAnim em `0x28DB74`. O objeto apareceu no passe físico 442. As cinco folhas entre `0x2817C4` e `0x2818A4` somam 52 quadros apresentados. No 486, o comando 73 da VDP1 usou a paleta `0x8100` e uma textura de 32 × 35 na origem (147, 23). A composição deu peso 31/32 à cor da face e 1/32 ao fundo. Com o nó na lista e seu desenho desativado, as divergências caíram de 648 para 636 na curta e na deslocada. Os 12 pixels recuperados são o cruzamento das articulações 22 e 10. O desenho do nó recuperou os outros 636. [Contraprova do renderizador](../build-chatgpt/actor/order-control.csv) · [Textura e cobertura](../build-chatgpt/evidence/cause486.json) · [Revisão do Antigravity Gemini](../build-chatgpt/evidence/gemini-review.txt)

Para repetir, definir `$env:MB_MUTE='1'` na raiz e executar `python build-chatgpt/evidence/replay.py ymir short holdout steady`, em seguida `python build-chatgpt/evidence/replay.py release short holdout steady` e `python build-chatgpt/evidence/compare-finale.py`. O replay do port passa `--mute`, lê as fitas de `tests/fixtures` e grava imagens em escala 1. Os dois hashes novos de referência vêm do RGB original em P6.

## Identidade dos overlays

Os 11 overlays abaixo foram extraídos nesta rodada dos CUE/BIN. Todos os hashes conferiram com `manifest/versions.txt`. Os mapas de ligação no disco 1 indicam a base `0x00232000`. A pasta privada contém os dados extraídos e as listas de símbolos, eventos e construtores. [Identidade, LBA e tamanho](../build-chatgpt/evidence/overlays.csv) · [Construtores](../build-chatgpt/evidence/layouts.csv) · [Eventos](../build-chatgpt/evidence/events.csv)

| Fase | Disco | SHA-256 de 03CODE |
|---|---:|---|
| 07GUITAR | 1 | `ef567508e6b7b6988177c068853cca150735f51c52d73a59f014598eb0ed8e3a` |
| 14DRUM | 1 | `a0d40d03eee16f6d119c553c4f8857df555f7cd62d3ce98a9651cd594f7b515d` |
| 16BIGBNS | 1 | `ce9fcd3d430487142ece55a89ee23932f2510bfe3b1c33b79655b04aaf145993` |
| 25GENRM | 2 | `670ad213889949d2cfe51777d4d5800ca203be0ebee2c74648322f53cf951c01` |
| 11LOGS | 1 | `6a14ae591f9e4adc92f8accc20de7092bce924151460f2a50260734be3b8acc5` |
| 18RIDE | 2 | `10725ba33fa0d9a24e9ef9c8aa2531514d06d4a6e2302366e575e1690ca0cf8b` |
| 20ICE1 | 2 | `2e60302a51da27b76b0eb5d0f041df74fdfe4bb22726f3130e074fd7466c29dc` |
| 21ICE2 | 2 | `f343894aac96114199fc0716e34b4ca05210e55cd53d10657fb26417aaa81a99` |
| 23MOAT | 2 | `a66291ea2c05bf6a1146fa16fa73db71c637999f24e736a6c47dd4c5d9e9e12a` |
| 24STAING | 2 | `75f22dc9c9f04486d150025786467ed06afce43c134ae0a1740af5b39d4ea31d` |
| 27FINALE | 2 | `19962c17f9ca3c467b05229956e98058df72d05bf083e6c30006a05f8f59528b` |

Os binários e as capturas em `build-chatgpt` são evidência local. O gestor consolida este documento no histórico geral.

## Mecânicas e comparação direta com o Saturn

As sondas usam o carregador original, selecionando apenas o número da fase no ponto de entrada de `LoadFirstLevel` (`0x20B95C`). O disco 1 passa pelo IPL com o SMPC do perfil americano. A sonda confirma a assinatura do overlay carregado, registra a entrada no cérebro do ator e entrega as máscaras ao controle físico por VBlank. As fitas positivas têm uma margem final de controle solto para identificar chamadas tardias de término. O resultado corresponde a R4 na chamada do original. [Sonda Ymir](../build-chatgpt/evidence/courses.cpp) · [Comparador de fita](../build-chatgpt/evidence/measure.py)

| Fase | Mecânica recuperada do disco |
|---|---|
| GUITAR | A tabela de sequências pertence ao sequenciador. A seleção de riff lê seis `LickStruct` em `0x283BC0`. `StartLick` entra em `0x2611A0`. O SH-2 consulta a posição do CD, a máscara dos botões e a energia. A animação de fim começa em `0x261548`; 180 atualizações separam o início da animação de `EndLevel(0)` em `0x26200C`. |
| DRUM | Há 1.197 registros a partir de `0x26B58C`, agrupados em 933 instantes distintos. `CheckJoy` em `0x24897C` usa a direção sustentada e as máscaras em `0x26B574`. Os dois cursores antecipam e julgam o grupo. |
| BIGBNS | A lista em `0x27168C` contém seis ondas de 16, 20, 29, 26, 25 e 24 bumpers, totalizando 140, e uma peça por onda. A primeira tem os 16 objetos originalmente identificados. A coleta da sexta peça leva ao ramo de fim em `0x25E958`. |
| GENRM | Energia inicial `0x8000`. Contato azul concede `0xF00`, vermelho tira `0xA00`. Acima de `0x16666` o original habilita a cabeça azul. O contato posterior com `CBluMov` é o evento de vitória. |
| LOGS | Os 152 registros em `0x2DBB80` incluem eventos sonoros. A máscara de colisão acompanha o tronco desenhado no PAN. `EvalSuccess` em `0x2BB6F0` testa a posição e a configuração corporal após o registro 2856. |
| RIDE | `CCurrent` controla os trechos e as colisões com a parede. `EndLevelBrain` em `0x23F320` verifica a forma do corpo e o avanço do PAN além de 2395. |
| ICE1 e ICE2 | ICE1 possui 64 `CIceBlock`. Vinte e quatro têm rota, quarenta têm ponteiro de rota nulo. Sete dos 24 começam a se mover depois que Bones pousa neles. As listas de fissura contêm respectivamente 86 e 43 trajetórias. No Ymir a coordenada Z de Bones no quadro 60 é −0x11999 em ICE1 e +0x11999 em ICE2. |
| MOAT | O disco fornece Sparky, nove alvos de trajetória e duas porcas. A libertação passa por `CSparky::Breakout` em `0x2B1050` e pelo fim da fase em `0x2AE984`. |
| STAING | `PlayTriggerList` em `0x261094` despacha 133 registros a partir de `0x27D8D4`. Há dois no registro PAN 594. A primeira máscara de fim aparece no 2520. Após `Delay(30)`, o ator chama `EndLevel(0)`. |

[Extração das quatro fases especiais](../build-chatgpt/especial/stage-evidence.json) · [Descompilação e resultados PAN](../build-chatgpt/pan/report.md)

### Replays das fases

Os ensaios abaixo identificam a versão da fita por hash completo no JSON de cada linha. Quando a fase termina antes do último comando, o comparador confere o prefixo consumido. Um horizonte de observação esgotado deixa o resultado em aberto.

| Fase e fita | Amostras da fita | Recebidas no Saturn, incluindo margem | Tick terminal | R4 |
|---|---:|---:|---:|---:|
| [07GUITAR / neutral-course](../build-chatgpt/evidence/saturn/07GUITAR/neutral-course/summary.json) | 1715 | 1715 | — | — |
| [07GUITAR / right-course](../build-chatgpt/evidence/saturn/07GUITAR/right-course/summary.json) | 1715 | 1715 | — | — |
| [07GUITAR / score2](../build-chatgpt/evidence/saturn/07GUITAR/score2/summary.json) | 1908 | 1726 | 1715 | 0 |
| [11LOGS / cache-positive](../build-chatgpt/evidence/saturn/11LOGS/cache-positive/summary.json) | 11493 | 2107 | 2096 | 1 |
| [11LOGS / cache1](../build-chatgpt/evidence/saturn/11LOGS/cache1/summary.json) | 1000 | 1000 | — | — |
| [11LOGS / cold1](../build-chatgpt/evidence/saturn/11LOGS/cold1/summary.json) | 1000 | 1000 | — | — |
| [11LOGS / course1](../build-chatgpt/evidence/saturn/11LOGS/course1/summary.json) | 11493 | 11793 | — | — |
| [11LOGS / pilot1](../build-chatgpt/evidence/saturn/11LOGS/pilot1/summary.json) | 2591 | 2591 | 2580 | 1 |
| [11LOGS / shift-330](../build-chatgpt/evidence/saturn/11LOGS/shift-330/summary.json) | 12123 | 2024 | 2013 | 1 |
| [11LOGS / slave1](../build-chatgpt/evidence/saturn/11LOGS/slave1/summary.json) | 1000 | 1000 | — | — |
| [14DRUM / score2](../build-chatgpt/evidence/saturn/14DRUM/score2/summary.json) | 9084 | 9095 | 9084 | 0 |
| [16BIGBNS / head-control1](../build-chatgpt/evidence/saturn/16BIGBNS/head-control1/summary.json) | 5508 | 5508 | 5497 | 0 |
| [16BIGBNS / joints200](../build-chatgpt/evidence/saturn/16BIGBNS/joints200/summary.json) | 200 | 200 | — | — |
| [16BIGBNS / wave2](../build-chatgpt/evidence/saturn/16BIGBNS/wave2/summary.json) | 2729 | 442 | 431 | 1 |
| [16BIGBNS / wave3](../build-chatgpt/evidence/saturn/16BIGBNS/wave3/summary.json) | 3479 | 573 | 562 | 1 |
| [18RIDE / current1](../build-chatgpt/evidence/saturn/18RIDE/current1/summary.json) | 9584 | 2731 | 2720 | 1 |
| [18RIDE / current2](../build-chatgpt/evidence/saturn/18RIDE/current2/summary.json) | 9584 | 9594 | 9583 | 1 |
| [18RIDE / dual-attract](../build-chatgpt/evidence/saturn/18RIDE/dual-attract/summary.json) | 9594 | 7709 | 7698 | 1 |
| [18RIDE / dual1](../build-chatgpt/evidence/saturn/18RIDE/dual1/summary.json) | 9594 | 9594 | 9583 | 1 |
| [18RIDE / pilot1](../build-chatgpt/evidence/saturn/18RIDE/pilot1/summary.json) | 9594 | 9594 | 9583 | 0 |
| [18RIDE / swim900](../build-chatgpt/evidence/saturn/18RIDE/swim900/summary.json) | 900 | 900 | — | — |
| [20ICE1 / cachePilot](../build-chatgpt/evidence/saturn/20ICE1/cachePilot/summary.json) | 945 | 945 | 934 | 1 |
| [20ICE1 / floePilot](../build-chatgpt/evidence/saturn/20ICE1/floePilot/summary.json) | 926 | 926 | 915 | 1 |
| [20ICE1 / friction2](../build-chatgpt/evidence/saturn/20ICE1/friction2/summary.json) | 4138 | 1006 | 995 | 1 |
| [20ICE1 / pilot1](../build-chatgpt/evidence/saturn/20ICE1/pilot1/summary.json) | 8000 | 8000 | — | — |
| [20ICE1 / pilot2](../build-chatgpt/evidence/saturn/20ICE1/pilot2/summary.json) | 8000 | 8000 | — | — |
| [20ICE1 / right-300](../build-chatgpt/evidence/saturn/20ICE1/right-300/summary.json) | 300 | 300 | — | — |
| [20ICE1 / route1](../build-chatgpt/evidence/saturn/20ICE1/route1/summary.json) | 3438 | 999 | 988 | 1 |
| [21ICE2 / friction2](../build-chatgpt/evidence/saturn/21ICE2/friction2/summary.json) | 530 | 830 | — | — |
| [21ICE2 / neutral-course](../build-chatgpt/evidence/saturn/21ICE2/neutral-course/summary.json) | 581 | 581 | — | — |
| [21ICE2 / pilot1](../build-chatgpt/evidence/saturn/21ICE2/pilot1/summary.json) | 592 | 592 | 581 | 0 |
| [21ICE2 / polled](../build-chatgpt/evidence/saturn/21ICE2/polled/summary.json) | 592 | 592 | 581 | 0 |
| [21ICE2 / right-course](../build-chatgpt/evidence/saturn/21ICE2/right-course/summary.json) | 581 | 581 | — | — |
| [21ICE2 / route1](../build-chatgpt/evidence/saturn/21ICE2/route1/summary.json) | 368 | 668 | — | — |
| [21ICE2 / up-300](../build-chatgpt/evidence/saturn/21ICE2/up-300/summary.json) | 300 | 300 | — | — |
| [23MOAT / buttons](../build-chatgpt/evidence/saturn/23MOAT/buttons/summary.json) | 360 | 360 | — | — |
| [23MOAT / pilot1](../build-chatgpt/evidence/saturn/23MOAT/pilot1/summary.json) | 513 | 513 | 502 | 1 |
| [23MOAT / pilot2](../build-chatgpt/evidence/saturn/23MOAT/pilot2/summary.json) | 1012 | 1012 | 1001 | 1 |
| [23MOAT / pilot3](../build-chatgpt/evidence/saturn/23MOAT/pilot3/summary.json) | 991 | 991 | 980 | 1 |
| [23MOAT / pilot4](../build-chatgpt/evidence/saturn/23MOAT/pilot4/summary.json) | 2224 | 2224 | 2213 | 1 |
| [23MOAT / rescue1](../build-chatgpt/evidence/saturn/23MOAT/rescue1/summary.json) | 2304 | 2604 | — | — |
| [23MOAT / rescue2](../build-chatgpt/evidence/saturn/23MOAT/rescue2/summary.json) | 2800 | 2290 | 2279 | 1 |
| [24STAING / course1](../build-chatgpt/evidence/saturn/24STAING/course1/summary.json) | 10110 | 7025 | 7014 | 1 |
| [24STAING / hands505](../build-chatgpt/evidence/saturn/24STAING/hands505/summary.json) | 505 | 505 | — | — |
| [25GENRM / blue2](../build-chatgpt/evidence/saturn/25GENRM/blue2/summary.json) | 2108 | 1851 | 1840 | 1 |
| [25GENRM / fire1](../build-chatgpt/evidence/saturn/25GENRM/fire1/summary.json) | 2108 | 2108 | — | — |
| [25GENRM / plasma-control1](../build-chatgpt/evidence/saturn/25GENRM/plasma-control1/summary.json) | 9310 | 9310 | 9299 | 0 |

## Verificação nativa e comparação das rotas

Cada arquivo `recorded.csv` registra o estado físico dos botões por VBlank no original. O controlador de referência consulta coordenadas para decidir os botões. Durante o jogo, ele escreve somente no controle. As fitas fixas são reproduzidas nos dois lados e os prefixos consumidos são verificados. `R4=0` no SH-2 indica vitória. No enum do port a vitória tem o valor 1.

| Fase | O que a comparação demonstra |
|---|---|
| GUITAR | Mesma fita de 62 acertos. A energia coincide em todas as 1.715 amostras e ambos vencem no tick 1.715. |
| DRUM | Mesmo julgamento dos 933 grupos e mesma energia em todos os 9.084 ticks até a vitória. |
| BIGBNS | A fita de derrota `wave2` coincide em 431 pares X/Y e nas quatro quedas. A fita `head-control1` vence nos dois no tick 5.497, com 36 rebotes e seis peças, após a correção em `0x25EFA4`. O traço registra 5.463 de 5.497 posições X/Y iguais. |
| GENRM | A fita `plasma-control1` vence no Saturn no tick 9.299. Uma compilação anterior venceu cedo, no 5.046. Depois da correção do tiro, a medição primária terminou as 9.310 entradas com 36 coletas, energia 73.728 e a fase nativa ainda ativa. O volume corporal usa oito juntas. |
| LOGS | O port atravessa os 152 registros. Com o cache SH-2 do Ymir desligado, o mestre espera a descompressão em `0x2A838C` e o slave em `0x2A7814`. Com a opção oficial ligada, PAN chega ao registro 1 no tick 334. Na fita `cache-positive` o original sofre três impactos de 0xF600 e perde no 2.096 (PAN 441). Deslocar a fita em 330 VBlanks também termina em derrota. |
| RIDE | A primeira fita corrigida chega a PAN 2396 nos dois. A fita `pilot1` vence no original no tick 9.583. O port reproduziu a vitória no tick 9.583 e PAN 2.396, reaplicando o arquivo físico inteiro. Ainda houve 12 golpes no port contra 3 no Saturn e energia final de 105.472 contra 131.072. As bolhas originais em 0x23E93C devolvem 0x3200 de energia por contato. |
| ICE1 | A fita `friction2` vence no port e perde no Saturn no tick 995. Com o cache SH-2 desligado, a segunda navegação parou de ler o controle após 458 consultas. Com o cache ligado, PAN e controle voltaram a avançar. As fitas `cachePilot` e `floePilot` terminaram em derrota no original nos ticks 934 e 915. |
| ICE2 | O quadro neutro 60 coincide em 71.680 pixels RGB (320×224). Na fita `pilot1` o original vence no 581. A mesma fita física consolidada vence no port no tick 579 e no Saturn no retrace 581. O Saturn não avançou a lógica em dois retraces. Ao alinhar só as 579 atualizações de jogo realmente executadas, os dois componentes de posição coincidem em todas. No ensaio físico, o quadro 60 em movimento também tem 0 pixels RGB diferentes; o 151 tem 195 e o 300 tem 2.306. |
| MOAT | O port exige as duas porcas e a libertação de Sparky. A fita `rescue2` recebe o quinto impacto e perde no original no tick 2.279. Na fita `pilot4`, o original sacode a porca duas vezes e perde sem soltá-la. |
| STAING | A fita `course1` termina em derrota no Saturn no 7.014 e no port no 5.395. A busca por trechos produziu outra fita. Ao executá-la desde a entrada, o Saturn registrou derrota no tick 6.235. Os pontos em `0x27C858`, `0x27CCF0` e `0x27D188` correspondem ao quadril e aos pés. As mãos controlam a suspensão de Bones. |

[Trajetórias e energia dos especiais](../build-chatgpt/especial/same-input.json) · [Pranchas dos dez percursos](../build-chatgpt/evidence/plates/overview.png) · [Registro dos CPUs de LOGS](../build-chatgpt/evidence/saturn/11LOGS/slave1/cpus.csv) · [Replay linear de STAING](../build-chatgpt/evidence/saturn/24STAING/search1-linear/summary.json)

As pranchas associam cada imagem ao tick da fita e ao quadro de apresentação de origem em `run.txt`. A inspeção mostra diferenças de composição, HUD e sprites em várias fases. Em DRUM e LOGS, inclusive, a referência ainda apresenta a abertura nas primeiras amostras. As diferenças RGB dessas imagens incluem a apresentação e a lógica de jogo.

### Compilação e testes

Em 22/11/2026, as duas builds privadas foram recompiladas sem avisos. As suítes completas foram executadas com `MB_MUTE=1` e exclusão de `test_movie_audio`. Ambas aprovaram `test_runway_skel`, os três replays de FINALE e `test_fmv_all`.

| Build | Aprovados | Falhas | Duração |
|---|---:|---:|---:|
| Clang 22.1.8 Debug | 53/58 | 5 | 99,89 s |
| MSVC 19.44.35228.0 x64 Release `/MT` | 55/60 | 5 | 51,66 s |

[Compilação Debug](../build-chatgpt/evidence/homologacao-Debug-build.log) · [CTest Debug](../build-chatgpt/evidence/homologacao-Debug-ctest.log) · [Compilação Release](../build-chatgpt/evidence/homologacao-Release-build.log) · [CTest Release](../build-chatgpt/evidence/homologacao-Release-ctest.log)

As cinco falhas foram as mesmas nas duas builds. Todas se repetiram no reteste executado em série no Release. [Reteste](../build-chatgpt/evidence/homologacao-Release-reteste.log)

- `test_full_campaign`, `test_fmv_transition` e `test_sidescrl_phase` esgotaram o limite com o controle automático em VALLEY, em x=6300 e y=112, com energia cheia. O teste de transição terminou ainda na campanha, antes de exercitar a transição esperada.
- `test_runway_phase` falhou na condição de encerramento de STAING com PAN ≥ 2520. A execução direta da estática, com o mesmo gerador de entradas da fase, registrou derrota com PAN 1638, oito impactos de vidro e oito impactos vermelhos. [Saída de STAING](../build-chatgpt/evidence/homologacao-STAING-autoplay.log)
- `test_rhythm_enemies` parou na asserção `e.count == 2u` de MAUSO. O caso testa a inicialização genérica, enquanto a fase carrega os inimigos pelo fluxo próprio.

O executável Release x64 `/MT` dessa primeira rodada tinha 1.513.472 bytes e SHA-256 `461f86d6804e43fe8d0b23f458ad90357873eb977a4e10b7d1ab65ccb109a94e`. A inspeção com `dumpbin /headers /dependents` confirmou os imports diretos `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `WINMM.dll` e `bcrypt.dll`. `test_static_imports` passou. [Dumpbin e hash históricos](../build-chatgpt/evidence/homologacao-Release-imports.log)

Comandos da rodada a partir da raiz do repositório

~~~powershell
$env:MB_MUTE='1'
& build-chatgpt/evidence/build.cmd Debug
& build-chatgpt/evidence/build.cmd Release
ctest --test-dir build-chatgpt/debug -C Debug --parallel 3 -E '^test_movie_audio$' --output-on-failure
ctest --test-dir build-chatgpt/release-static -C Release --parallel 3 -E '^test_movie_audio$' --output-on-failure
Get-FileHash build-chatgpt/release-static/mrbones.exe -Algorithm SHA256
~~~

### Revalidação após as correções de VALLEY, STAING e MAUSO

- VALLEY passou no teste da fase em 4.578 atualizações nas duas builds com a revisão de rolagem lateral integrada. A rota da campanha passou a usar a largura da fase quando ela excede o limite padrão de atualizações. Na revisão anterior, a rota chegou à vitória em 6.340 atualizações. [Sonda da revisão anterior](../build-chatgpt/valley-qa/prelim.log)
- STAING passou em `test_runway_phase` após o piloto replanejar mais cedo e aumentar a margem de previsão para obstáculos vermelhos. O ensaio isolado venceu em PAN 2527 no tick 10.110, com 12 impactos de vidro, dois impactos vermelhos, 128 recompensas e 18.432 de energia. [Traço do piloto](../build-chatgpt/staing-qa/tuned-run.log)
- MAUSO passou em `test_rhythm_enemies`. O teste assegura que a inicialização genérica comece sem inimigos e usa o mesmo criador de assemblers que a fase para testar dano e orientação.

As três falhas restantes são reproduzíveis em BAT2. `test_full_campaign` e `test_fmv_transition` passam por VALLEY e param perto da saída de BAT2. `test_sidescrl_phase` passa por VALLEY e BAT1 e falha na primeira transição de configuração de BAT2. A sonda registra um golpe no tick 11, antes da primeira coleta, que muda o corpo de `0x2D89B0` para `0x2D8AA8`. A busca restrita de controles de abertura não evitou esse primeiro golpe em BAT2. BAT3 também apresentou perda de peça antes da primeira coleta na sonda independente. [Contatos de abertura](../build-chatgpt/staing-qa/bat-opening-trace.log) · [Busca BAT2](../build-chatgpt/staing-qa/bat-probe-log.txt) · [Busca BAT3](../build-chatgpt/staing-qa/bat3-probe-log.txt)

A estática dessa revalidação tinha 1.514.496 bytes e SHA-256 `20c9a9f8ce5a2b7e9ad811fa1890d709e195aab7dfd59045f1b6be28023c4048`. O `dumpbin` e `test_static_imports` confirmaram PE x64 e os imports diretos `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `WINMM.dll` e `bcrypt.dll`. [Hash e imports históricos](../build-chatgpt/evidence/correcao-revalidacao-Release-imports.log)

As duas builds foram recompiladas sem avisos. O CTest foi executado com `MB_MUTE=1` e `test_movie_audio` excluído.

- Clang Debug passou em **55/58**. [Compilação Debug](../build-chatgpt/evidence/correcao-revalidacao-Debug-build.log) · [CTest Debug](../build-chatgpt/evidence/correcao-revalidacao-Debug-ctest.log)
- MSVC Release x64 `/MT` passou em **57/60**. [Compilação Release](../build-chatgpt/evidence/correcao-revalidacao-Release-build.log) · [CTest Release](../build-chatgpt/evidence/correcao-revalidacao-Release-ctest.log)

### Colisão e piloto de BAT2 e BAT3

Nos dois percursos, o teste de dano usa a caixa da configuração corporal e o volume do inimigo no mesmo sistema de coordenadas. O volume do zumbi parte do piso na própria coluna e do torso do sprite ancorado pelos pés. O alcance horizontal de 8 px vem da rotina de aproximação do zumbi. O volume do morcego usa a posição do voo, a escala de desenho e a altura da câmera. A reação ao dano continua a poder soltar uma peça desde o início da fase.

| Fase | Mudanças de configuração | Vitória | Quadros pendurado | Quadros subindo | Impactos |
|---|---|---:|---:|---:|---:|
| BAT2 | `0x2D84D8` no tick 646, `0x2D83E0` no 833 | 1.033 | 89 | 90 | 0 |
| BAT3 | `0x2DA458` no tick 24, `0x2DA170` no 136 | 1.848 | 9 | 19 | 0 |

BAT3 passou a usar entradas que aproveitam os apoios de escalada. A sequência integrada reproduz as 1.848 máscaras da fita nativa validada. SHA-256 da fita `868136216d7dcdfd06165a388d335b9ebbbb890c73e15552b31e993cb286542b`. O teste isolado executou as asserções de configuração, pausa durante a escalada e contato com a saída das duas fases. Na contraprova com a abertura antiga, o impacto no tick 17 ainda muda o corpo de `0x2DA550` para `0x2DA648`. [Fita de BAT3](../build-chatgpt/bat-route-qa/real-coarse-6020.csv) · [Asserções isoladas](../build-chatgpt/bat-collision-qa/test-batwood-final.log) · [Contraprova](../build-chatgpt/bat-route-qa/integrated-legacy-bat3.log)

As builds foram recompiladas sem avisos. As duas suítes usaram `MB_MUTE=1` e excluíram `test_movie_audio`. `test_full_campaign` e `test_fmv_transition` passaram em ambas. `test_static_imports` passou no Release.

- Clang Debug passou em **57/58**. [Compilação Debug](../build-chatgpt/evidence/bat-homologacao-Debug-build.log) · [CTest Debug](../build-chatgpt/evidence/bat-homologacao-Debug-ctest.log)
- MSVC Release x64 `/MT` passou em **59/60**. [Compilação Release](../build-chatgpt/evidence/bat-homologacao-Release-build.log) · [CTest Release](../build-chatgpt/evidence/bat-homologacao-Release-ctest.log)

O único teste restante, `test_sidescrl_phase`, para na travessia do portão aberto em `test_valley_mechanics`. A cópia privada passou por todo o teste após corrigir duas montagens. Em VALLEY, ela apoia o personagem no piso do mapa e inicia o teste de cura com dano não letal. Em BAT1, ela sincroniza a posição do ator antes de instanciar o inimigo de teste. As duas mudanças preservam as asserções e ficam fora da reserva de BAT2/BAT3. [Patch de teste em cópia privada](../build-chatgpt/side-status/valley-bat1-fixtures.patch) · [Resultado da cópia privada](../build-chatgpt/side-status/sidescrl-fixtures-run.log)

O binário Release x64 `/MT` da rodada de BAT2/BAT3 tinha 1.516.544 bytes e SHA-256 `41808959b5bb74014ff405d76fb180884399d2ae1aa53628b00030156f930c9e`. Os imports diretos verificados foram `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `WINMM.dll` e `bcrypt.dll`. [Hash e imports históricos](../build-chatgpt/evidence/bat-homologacao-Release-imports.log)

### Homologação após o ajuste das fixtures de VALLEY e BAT1

Em VALLEY, a travessia começa no piso calculado pelo tilecache e segue pelas atualizações normais do ator. O teste de cura posiciona o personagem sobre o piso da tumba e começa com `0x1000` de energia, após dano não letal. As asserções continuam exigindo bloqueio com o portão fechado, abertura apenas pela chave pareada, travessia do portão aberto e a cura exata de `0x12C00` uma única vez. Em BAT1, a montagem sincroniza o ator em x=53 antes de usar sua posição para instanciar o inimigo. O teste continua exigindo a configuração `0x2D69AC` e uma peça em queda.

As duas builds compilaram sem avisos. As suítes completas definiram `MB_MUTE=1` e excluíram somente `test_movie_audio`. `test_sidescrl_phase`, `test_full_campaign` e `test_fmv_transition` passaram nas duas builds.

- Clang Debug passou em **58/58**, em 173,15 s. [Compilação Debug](../build-chatgpt/evidence/fixture-final-Debug-build.log) · [CTest Debug](../build-chatgpt/evidence/fixture-final-Debug-ctest.log)
- MSVC Release x64 `/MT` passou em **60/60**, em 73,08 s. `mrbones.c` foi recompilado e `mrbones.exe` vinculado novamente. `test_static_imports` passou. [Compilação Release](../build-chatgpt/evidence/fixture-final-Release-build.log) · [CTest Release](../build-chatgpt/evidence/fixture-final-Release-ctest.log)

O executável homologado tem 1.516.544 bytes. SHA-256 `1cda6d207a9bfbc1800e50515cfb0582a88fee2529671cb1f4fbfc6d1fc0d91e`. O `dumpbin /headers /dependents` confirmou PE x64 e apenas cinco imports diretos, `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `WINMM.dll` e `bcrypt.dll`. [Executável](../build-chatgpt/release-static/mrbones.exe) · [Hash e imports](../build-chatgpt/evidence/fixture-final-Release-imports.log)

### Distribuição para Windows x64 limpo

O pacote em `dist-windows-clean` contém o executável homologado, `mrbones.exe.sha256`, `manifest/versions.txt`, o guia e os scripts `run_tests.bat` e `run_tests.ps1`. O SHA-256 da cópia é `1cda6d207a9bfbc1800e50515cfb0582a88fee2529671cb1f4fbfc6d1fc0d91e`. O empacotador validou novamente os cinco imports esperados. Os CUEs e as faixas originais são fornecidos à parte pelo usuário. [Pacote](../dist-windows-clean/) · [SHA-256](../dist-windows-clean/mrbones.exe.sha256) · [Guia](../dist-windows-clean/clean-windows-verification.md)

O modo `verify` confere a arquitetura, a presença do manifesto e o hash sem precisar dos discos. Os modos `check`, `lose`, `finale` e `campaign` aceitam caminhos externos com `-Disc1Cue` e `-Disc2Cue`. `check` e `campaign` rodam com áudio mudo. [Empacotador](../tools/package-clean-windows.ps1)

Os verificadores foram executados por Windows PowerShell 5.1 a partir de uma cópia isolada fora do repositório, com caminhos de executáveis e módulos limitados aos componentes do Windows. `verify`, `check` e `campaign` retornaram 0. A campanha passou pelas 22 fases jogáveis, registrou a troca de disco e voltou ao menu de FINALE com `nvmshadow.bin` gravado. As contraprovas rejeitaram CUE ausente e checksum alterado. Essa execução foi feita no computador de compilação. [Resumo da verificação](../build-chatgpt/evidence/clean-package-smoke.log) · [Checagem estrutural](../build-chatgpt/evidence/clean-package-check.log) · [Campanha](../build-chatgpt/evidence/clean-package-campaign.log)

### Tempo por quadro

A medição histórica usou a janela Win32, o caminho padrão de apresentação, `--benchmark-frames 300 --mute --no-hd-bones` e os discos locais. A captura do título verifica que as 300 apresentações permanecem na fase. O trabalho inclui atualização, desenho e blit GDI. O intervalo entre apresentações inclui a espera do relógio. Os números pertencem ao executável anterior de SHA-256 `0d3d1d1a6916890ec3b34be1b8b8ce375e46ec24862e9424688c0ca244ea4935`. [Amostras agregadas por executável](../build-chatgpt/evidence/bench/result.csv)

| Fase | Quadros | Mediana trabalho (ms) | P95 (ms) | Máx. (ms) | ≥16,6 ms | Apresentação média (ms) | Máx. entre apresentações (ms) |
|---|---:|---:|---:|---:|---:|---:|---:|
| 07GUITAR | 300 | 1.486 | 2.251 | 5.808 | 0 | 16.672 | 21.014 |
| 11LOGS | 300 | 1.309 | 2.342 | 4.743 | 0 | 16.676 | 20.256 |
| 14DRUM | 300 | 1.271 | 2.064 | 3.167 | 0 | 16.673 | 18.256 |
| 16BIGBNS | 300 | 1.412 | 2.119 | 3.727 | 0 | 16.671 | 18.928 |
| 18RIDE | 300 | 1.257 | 1.891 | 3.250 | 0 | 16.671 | 18.847 |
| 20ICE1 | 300 | 2.061 | 3.435 | 5.747 | 0 | 16.672 | 20.653 |
| 21ICE2 | 300 | 1.948 | 3.119 | 4.514 | 0 | 16.678 | 31.573 |
| 23MOAT | 300 | 1.294 | 2.077 | 5.056 | 0 | 16.671 | 20.748 |
| 24STAING | 300 | 1.306 | 2.203 | 4.344 | 0 | 16.672 | 19.331 |
| 25GENRM | 300 | 1.212 | 2.133 | 5.552 | 0 | 16.676 | 20.866 |
| 27FINALE | 300 | 1.466 | 2.523 | 4.846 | 0 | 16.672 | 20.441 |

### Evidência complementar e escopo dos ensaios

- Nas dez fases, controle neutro e Direita sustentada produziram 0 vitórias nativas em 20 ensaios. A tabela discrimina derrota e fase ainda em andamento no horizonte da rota para cada par. [Controles fixos e resultados](../build-chatgpt/evidence/native-fixed.csv)
- No Saturn, os 20 ensaios com controle neutro ou Direita sustentada registraram 12 derrotas e oito casos sem desfecho até o limite observado. Nenhum registrou vitória. O CSV de prefixos lado a lado cobre quatro ensaios. As rodadas nativa e Saturn usaram horizontes diferentes em LOGS e RIDE. [Logs por fase](../build-chatgpt/evidence/jobs/) · [Prefixos comparados](../build-chatgpt/evidence/negative-summary.csv)
- GUITAR e DRUM têm equivalência temporal de pontuação. BIGBNS tem a mesma recomposição e o mesmo instante de vitória. A correspondência visual varia conforme a fase. [Reaplicação emitindo só entradas do controle](../build-chatgpt/evidence/big-wave-proof.log) · [ICE2 sob controle no quadro 60](../build-chatgpt/evidence/plates/21ICE2/polled/tick60.png)
- A revisão de FINALE por Antigravity Gemini está no parecer do quadro 486. A segunda consulta opcional sobre a família de fases retornou cota esgotada. [Revisão de FINALE](../build-chatgpt/evidence/gemini-review.txt)
- Configuração do compilador e vinculação do CRT foram extraídas do cache e da receita Ninja do executável. [Compiladores e /MT](../build-chatgpt/evidence/toolchains.txt)
