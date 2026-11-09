# Rotas de FMV: análise dos cérebros de término

Condição: resultado 0, campos opcionais +0x48 e +0x4c iguais a -1. Cada binário foi extraído do disco indicado e importado como SH-2 em 0x00232000. O endereço do cérebro é o prólogo revalidado; o mapa pode divergir. A associação de filme ao nível atual e de argumento ao próximo nível decorre da análise do carregador em `ghidra-fmv-loader.txt`.

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

Os arquivos de descompilação estão em `docs/ghidra-fmv-brains/`. Os 13 caminhos `PlayDuckMovie` anteriores a FINALE são integrados no host; FINALE é mantido separado porque o tratamento atual da fase já usa o mesmo AVI e a sobreposição de jogabilidade ainda não foi reconstruída. Os dados indicam as rotas padrão estáticas, não comprovam que campos opcionais ou resultados diferentes jamais sejam usados em runtime.

Em FINALE, os bytes do binário original confirmam que o literal `0028ce00` aponta para `0020b61c` (`PlayDuckMovie`), usado pelo cérebro de encerramento no ramo padrão. O mapa também identifica o cérebro `CFBones`, seus controles e `RingDefinitionTable`. A captura anterior obtida com `00228970=27` selecionou o filme, e não demonstra a apresentação da fase.

Em 09/11, o Ymir com disco 2 e seleção forçada de fase `0022896c=27`, filme `00228970=0`, mostrou a arena de FINALE com Mr. Bones e uma guitarra (captura `build-static/ymir-finale-review/screenshots/mr Bones [MK-81016]-20261109T133600.304.png`). Sem comandos de jogo, a execução retornou ao menu; não foi observada uma vitória. O port ainda inicia diretamente o AVI e não implementa a arena, seus controles ou a transição de vitória. O teste de campanha simulado não valida essa fase.

Em outra execução, o breakpoint no cérebro `0028ccd0` ocorreu com `R4=06026a9c`, resultado `+0x44=1` e campos `+0x48/+0x4c=-1`. Alterar apenas o resultado de 1 para 0 em RAM levou o Ymir ao filme de FINALE. A captura `build-static/ymir-finale-winref/screenshots/mr Bones [MK-81016]-20261109T134500.548.png` coincide em RGBA com o intervalo 288 do AVI decodificado pelo port. Isso confirma o ramo sob o resultado forçado; não valida a condição de vitória normal ou a fase nativa.

No Ymir, uma entrada direta forçada em GUITAR (`0022896c=7`, `00228970=0` no ponto `0020b95c`) mostrou a fase Guitar Solo: personagem com guitarra diante da plateia. O arquivo `/07GUITAR/20AVI` mostra uma cena cinematográfica diferente; sua chamada após GUITAR é indicada pela descompilação da tabela. A entrada foi forçada, portanto a observação não comprova sozinha a transição de vitória nem a fidelidade da fase nativa, ainda incompleta.

O mesmo procedimento com `0022896c=14` mostrou DRUM com o personagem entre tambores; `/14DRUM/20AVI` contém uma sequência visual diferente. A chamada de seu FMV após a vitória padrão é uma conclusão da análise estática, não uma transição observada em uma partida completa.

Com `0022896c=16`, o original mostrou BIGBNS como fase jogável com céu azul e personagem; um quadro de `/16BIGBNS/20AVI` é visualmente diferente. O mapa não mostrou símbolos do leitor DUCK, e a chamada do filme após vitória permanece inferida da análise estática, sem confirmação em partida normal.

Com `0022896c=25` e disco 2, a seleção forçada apresentou GENRM jogável, com inimigos e cenário circular; o quadro 100 de `/25GENRM/20AVI` mostra uma cena cinematográfica distinta. O mapa GENROOM não apresenta símbolos `MFP_`, `DUCKPLAY`, `DUCKFILE`, `DXL_` ou `20AVI`. A chamada do filme após a vitória é inferida do cérebro de término, sem observação de uma vitória normal; a fase nativa ainda não reproduz fielmente o original.
