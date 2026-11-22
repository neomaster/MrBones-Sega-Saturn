# Geometria e cadência das fases de Mr. Bones no Windows

Edição americana, discos 1 e 2. Medições em 20 de novembro de 2026. Foram inspecionados os 22 diretórios jogáveis dos dois discos. Na revisão medida abaixo, BAT2 interrompe a campanha automatizada.

## Como reproduzir

A partir da raiz, o extrator lê os CUE/BIN, confere os recursos com o manifesto e registra a cadeia principal de objetos de cada overlay. Cada linha inclui o disco, SHA-256 de 03CODE, endereço do registro e do construtor, próximo registro, flags, coordenadas fixas com sinal e dois campos brutos em +20 e +24 cuja interpretação ainda não foi confirmada. Um nome de construtor com diferença de 12 bytes é uma identificação por correspondência do mapa. O endereço bruto do binário é preservado. A coluna `linked_tail_hex` conserva os parâmetros após o cabeçalho de 28 bytes até o endereço do próximo registro. O terminador não fornece o tamanho de seu próprio registro.

```powershell
$env:MB_MUTE='1'
$d1='disco1/Mr. Bones (USA) (Disc 1).cue'
$d2='disco2/Mr. Bones (USA) (Disc 2).cue'
python tools/extract_stage_layouts.py $d1 $d2 manifest/versions.txt build-static/saturn-fidelity/layouts.csv --events build-static/saturn-fidelity/events.csv
cmake --build build --config Debug
cmake --build build-static --config Release
ctest --test-dir build -C Debug -E test_movie_audio --output-on-failure
ctest --test-dir build-static -C Release -E test_movie_audio --output-on-failure
.\build-static\mrbones.exe $d1 manifest/versions.txt --disc2 $d2 --level /04RUNWAY --mute --no-hd-bones --benchmark-frames 300
```

## De onde vêm as medidas

- **Coordenadas e corpo.** Os overlays são carregados em 0x00232000. A tabela em `src/core/runway_projection.c` identifica `_BonesStageInit` nas 22 fases. O port segue os ponteiros de configuração no disco para obter posição inicial, direção, caixa de contato e parâmetros de salto. O sinal de X e Y do mundo é invertido ao projetar na tela. A unidade 2341 do port continua presente em interfaces internas. A projeção muda de fase para fase.
- **Salto.** `GravityWorks` usa o literal SH-2 0xFBB3, que corresponde a aceleração de 1101 no sentido descendente. O reforço de salto mantido é 550 por atualização. A configuração de corpo completo contém impulso de 15728 e janela de 30 atualizações. BAT2 e BIGBNS usam 13762 e 26 na configuração examinada. Esses valores passam pela projeção da fase. A caminhada horizontal, a direção no ar, a cadência de animação e os estados dos modos especiais ainda exigem calibração individual.
- **Máscara SCRL.** Em VALLEY, BAT1–3 e ICE1–2, cada par de 06AMAP delimita um bloco de 08ATIL por setor de 2048 bytes e tamanho. Nos tiles com colisão, os 64 bytes em +0x48 contêm quatro valores por byte, cada um cobrindo 2×2 pixels. Em VALLEY, `MAPGetHixel` (0x2C43EE), `SCRLCOLLInitGetFloor` (0x2C4214) e `BonesWallAheadCheck` (0x2C83D0) distinguem piso e parede lateral. A parede requer o valor 3. Exemplo verificável no disco 1, x=80 está livre em y=733 e sólido em y=734. Na família PAN, `PANCOLLfindfloor` usa os tipos 1 e 3.
- **Trampolins e saídas.** As posições, orientação e variante de `CreateCSpringBr` vêm de 03CODE. Em VALLEY, o construtor em 0x2BC830 e `PlatformCheck` em 0x2BCAFC fornecem os braços de contato, limites de força e 4–6 atualizações de compressão. A janela vertical de busca de `CPlatSvrxxYy` usa limites de +0x2666 e −0x8000 em unidades 16.16. A busca de piso em 0x2C42B4 avança em incrementos de dois pixels e conserva a paridade da sonda. Quando encontra uma célula ausente na descida, devolve o deslocamento anterior. A integração dos estados de Bones com esses trampolins continua em validação. O registro de `CWinBox` em 0x2EEE00 descreve um retângulo aberto de 10000×10000 a partir de (15704,68). O construtor converte as dimensões em pixels para unidades de mundo. A rotina em 0x2BEA18 compara as duas coordenadas.
- **RUNWAY.** Os 2651 registros de 15PAN foram decodificados para assinaturas de luminância. A comparação com 87 amostras da gravação com correlação superior a 0,75 dá 14,991 registros/s, com mediana de correlação de 0,910. Em 4:44 o cenário corresponde ao registro 18, em 4:50 ao 108, em 5:42 ao 888 e em 7:36 ao 2596. O CSV de eventos contém 21 eventos de RUNWAY, 152 de LOGS e 133 de STAING, mais 20 posições tipadas de `_TableHole` e 27 pares de `_TableYFall` de RUNWAY. O arquivo de eventos contém 353 linhas de dados, incluindo 306 gatilhos PAN. O port avança um registro a cada quatro passos ativos de 60 Hz. `CLevelManager::UpdateSpeed` em 0x26D51C escreve 0x2C54 em `_SpeedScroll` quando o registro muda. Encontrei uma leitura desse símbolo em `CGlow::Brain`. O port acumula esse valor na origem interna de coordenadas e compensa Bones na mesma atualização. A leitura do símbolo por CGlow não demonstra movimento físico da câmera. A lista de gatilhos em 0x28DBA0 contém o evento de vitória 0x800 no registro 2635, equivalente a 175,67 s desde o início do relógio da fase. O sincronismo do cenário é independente da posição horizontal do jogador. [Gameplay utilizado](https://www.youtube.com/watch?v=-c9br4VL-8I&t=1868s)
- **Perseguidores de RUNWAY.** No registro 1, `PlayTriggerList` cria CLord. `CLord::Gang` em 0x26B388 espera 30 atualizações e cria quatro CGangMember. Cada um aponta para uma trilha de 03CODE pela tabela em 0x2D9684. Foram lidas 10391, 10276, 10210 e 10206 entradas de quatro bytes, terminadas por 0x2710. `CGangMember::Read` em 0x26B89C separa a coordenada X e o bit de sincronização do PAN, a coordenada Y com sinal, a animação e o quadro. O port lê a forma em 0x28DB68, resolve a textura em 04STXT a partir de 0x25C08000 e aplica a âncora VDP1. O lançamento de objetos entra no quadro zero de ThrowEnd. A resposta dinâmica de Reach e a trajetória do projétil ainda são parciais. Os membros deixaram de ser distribuídos ao longo de corredores inventados.
- **Fossos e borboletas de RUNWAY.** A rotina de 0x26D68C compara X, deslocado pelo registro PAN, com os 27 limiares descendentes em 0x28DD54. O Y ativo determina a perda de 0x6400 de energia e o impulso de recuperação de 0x4CCC após um yield. O port usa esses limiares no lugar das três zonas fixas. A outra lista, em 0x28DC58, descreve efeitos CGlow criados por CheckJumpys. A rotina de queda não consulta essa lista. Os eventos de máscara 4 criam 16 borboletas. A posição inicial (−0x66DB6, 0x16DB6), a velocidade horizontal de 0x666 por atualização e a cura de 0xF000 vêm de CButFly. A animação em 0x232A24 contém 26 entradas com duração e âncora próprias. O contato usa a caixa da configuração de Bones dentro da janela original de ±0x9999. A verificação interna de cada junta ainda depende do corpo articulado.
- **Controle de RUNWAY.** O mapa identifica 0x2858BC como `_BonesPos`. A rotina em 0x26A614 escreve diretamente em X. Esquerda soma 3932 ou 1310 em postura alta, direita em postura alta subtrai 2621, direita normal conserva a posição e neutro normal soma 1966. O port inverte o sinal ao projetar para a direita. Os registros 4, 5 e 6 restabelecem a posição dos literais de `PlayTriggerList`. `CLevelManager::Brain` dá 720 atualizações de tolerância antes de verificar a margem esquerda de −150 pixels. A configuração de caminhada dos demais modos PAN e a animação articulada do protagonista ainda divergem.
- **Origem da divergência de pose.** O primeiro quadro de `_SkeletonRun` passa pela forma 0x25386C e pela primitiva 0x253850. Ela aponta para 04STXT+0x1EE40, com âncora (24,43). O renderizador pré-existente também usava essa textura para representar o protagonista. Ela pertence à animação dos perseguidores. Ajustar a escala dessa imagem não reproduz os estados e as peças articuladas de Bones.
- **VILLAGE.** A lista começa em 0x2BFBD0 e contém oito casas criadas por `CreateCHouse` em 0x29C38C. A variante em +28 seleciona a pose inicial e os índices 3, 7 e 11 em 04STXT. As âncoras no overlay são (32,49), (31,57) e (26,48). Os atores móveis e suas máquinas de estados continuam em investigação.

## Cobertura por fase

“Registros” conta objetos na cadeia ligada principal lida até seu terminador. Um travessão indica que esse formato de lista não foi identificado na fase. Foram lidos 1035 registros em 15 fases. A contagem inclui o primeiro objeto, identificado separadamente no mapa como _FirstNumenStruct. A extração desses registros permite inspecionar tipos e posições. A coluna final descreve o alcance da integração no jogo.

| Fase | Pixels por unidade | Registros | Evidência ou limite da implementação |
|---|---:|---:|---|
| 04RUNWAY | 28 | 1 | Relógio, entrada horizontal, quatro perseguidores com poses gravadas e gatilho final integrados. Perfil de queda e 16 borboletas disponíveis nos gatilhos reais. Ataque reativo, contato por junta e corpo articulado continuam parciais. |
| 05MAUSO | 42 | 4 | Scroller, Maestro, ButFly e BlueHead identificados. Comportamento e pose ainda diferem da gravação. |
| 06VALLEY | 42 | 175 | Máscara, três trampolins e volume final recuperados. Percurso superior e entrada no volume final reproduzidos pelo controle automático. |
| 07GUITAR | 64 | — | Configuração inicial recuperada. Regras e cadência do modo musical sem equivalência quadro a quadro. |
| 08BAT1 | 39 | 70 | Dezoito trampolins. Início em (3126,225) e saída em x=53, à esquerda. |
| 09BAT2 | 39 | 60 | Treze trampolins e configuração de torso. O primeiro registro, 0x2D56B0, aponta para o construtor de CEndBatwood e posiciona a saída em (2364,244). Rota vertical em validação. |
| 10BAT3 | 39 | 102 | Vinte trampolins. Rota e contatos em validação. |
| 11LOGS | 21 | — | Lista de gatilhos identificada em 0x2DBB80. Eventos e movimento de cada tronco pendentes. |
| 14DRUM | 64 | — | Projeção identificada e 48 notas da DrumSequence já presentes no port. Cadência completa a validar. |
| 15GLASS | 25 | 176 | Objetos extraídos. Comportamento das plataformas a validar. |
| 16BIGBNS | 42 | 18 | Objetos e parâmetros do corpo danificado recuperados. Composição dos segmentos a validar. |
| 17VORTEX | 25 | 18 | Objetos extraídos. Física do voo e câmera a validar. |
| 18RIDE | 42 | — | Projeção identificada. Percurso e eventos PAN a validar. |
| 19VILLAGE | 42 | 24 | Oito casas posicionadas e ancoradas pelo overlay. Movimento dos habitantes e pose de Bones a validar. |
| 20ICE1 | 32 | 219 | Início em (600,8000), plano A de 1864×8192. O enquadramento inicial usa a altura do disco. O percurso do gelo continua parcial. |
| 21ICE2 | 73 | 45 | Início em (200,2100). Falta reconstruir percurso, orientação e pose do patinador. |
| 22SHADOW | 18 | 2 | Registro extraído. Regras de projeção da sombra a validar. |
| 23MOAT | 32 | 40 | Objetos extraídos. Temporização e câmera PAN a validar. |
| 24STAING | 64 | — | Lista em 0x27D8D4 identificada. Composição de cena e comportamento dos atores pendentes. |
| 25GENRM | 32 | — | Projeção identificada. Eventos do modo especial a validar. |
| 26LAFF | 64 | 81 | Objetos extraídos. Colisão de combate, pose e escala do chefe a validar. |
| 27FINALE | 42 | — | Tabela já usada pelo port em 0x2B0158, 256 quadriláteros de quatro vértices. Combate interativo, disparos e composição distintos do FMV. |

12TROLL1 e 13TROLL2 pertencem às transições da campanha. A contagem dos 22 diretórios jogáveis acima segue o catálogo do port.

## Comparação visual e de tempo

O material local em `build-static/saturn-fidelity` inclui capturas PPM com `hd_bones=OFF`, quatro pranchas JPEG, `layouts.csv`, `runway-pan-match.csv`, os CSV de tempo por apresentação e os logs de compilação e teste. A captura de base foi feita antes desta reconstrução. A coluna de referência é um recorte da área ativa da gravação a 30 fps. O autor da gravação informa que usou cinematics japoneses. Para validar FMV da edição USA, a fonte é o disco americano. Nas fases sem alinhamento temporal demonstrado, a prancha permite comparar enquadramento e proporção geral. Ela não mede erro por pixel de uma pose equivalente.

O modo padrão apresenta o framebuffer nativo por GDI. F5 mantém xBR3x como opção. O benchmark usa o executável de janela, com QPC para medir atualização, renderização, conversão, blit e GdiFlush. Os CSV registram custo de trabalho, intervalo entre apresentações e passos descartados. Uma apresentação com menos de 16,6 ms de trabalho ainda pode perder um prazo do agendador. Essas medidas não verificam a frequência física de varredura do monitor.


## Percurso e composição observados

As quatro pranchas estão em `build-static/saturn-fidelity/comparison-1.jpg` a `comparison-4.jpg`. Foram inspecionadas a 320×224 por coluna.

| Fases | Observação |
|---|---|
| RUNWAY | No registro 888, a lua, a lápide e a árvore do port ocupam a mesma região da referência. O grupo de perseguidores está à esquerda. A pose e a altura do protagonista divergem. As entradas dos jogadores nessas capturas são diferentes. |
| MAUSO e VALLEY | A referência mostra a plataforma do maestro e, em VALLEY, Bones no ar. O port exibe outros trechos dessas fases. Há diferença de composição, escala aparente e seleção dos atores. Só o percurso automatizado de VALLEY confirmou o contato com a rota superior e o volume de saída. |
| BAT1, BAT2 e BAT3 | BAT1 completa o percurso à esquerda. As pranchas de BAT2 e BAT3 evidenciam a ausência das transformações corporais vistas na gravação. Os testes diretos dessas duas fases também não chegam à saída. |
| VILLAGE, ICE1 e ICE2 | As casas seguem registros e âncoras do disco. ICE1 mostra o gelo no início da fase. A célula da antiga prancha de ICE2 usava 3.520 s, instante que pertence a ICE1. A nova comparação usa ICE2 entre 3.568 e 3.574 s, depois do cartão de título, e mostra a diferença de escala de Bones sob o gelo. [Sequência corrigida](../build-static/saturn-fidelity/interactions/comparison-4.jpg) |
| STAING, LAFF e FINALE | O port apresenta o vitral, a arena de LAFF e o círculo de FINALE. O alcance dos braços, a relação de tamanho com o chefe e a pose dentro do círculo divergem da gravação. Em FINALE, a amostra antiga é o FMV e a nova vem do combate interativo. |

Em RUNWAY, a sequência de 342,25 s a 343,15 s mostra a impulsão no piso inferior e o pouso na plataforma seguinte. Em 342,35 s o pé ainda está na altura do piso. Em 342,45 s o corpo já subiu e em 343,05 s o pé volta à altura da plataforma superior. O topo do arco passa diante da lua e do HUD, que reduzem a precisão da leitura. O intervalo observado entre impulsão e pouso é de aproximadamente 0,6–0,8 s. Integrar a configuração de corpo completo com o botão sustentado dá uma elevação máxima de 92,6 pixels em 28 passos e 49 passos para voltar à altura inicial. O vídeo não identifica o instante de soltura do botão. As imagens estão em `runway-jump-detail.jpg` e `runway-jump-timing.jpg`.

Em BAT2 a saída é o primeiro registro da lista, em 0x2D56B0. O construtor tem endereço físico 0x2CABA4. Em 0x2CAC6C, CEndBatwood::Brain consulta o índice em +0x44, verifica um entre três estados de Bones e chama BonesObjHitChk com o raio 0x8000 antes de EndLevel(0). O argumento em +28 do registro original vale 1 em BAT1, 2 em BAT2 e 3 em BAT3. O teste de saída horizontal do port ainda não reproduz essas condições. Em VALLEY, Latch_CheckViable (0x2D1C9C) testa hixels 1 e 2 na coordenada da mão, usa SCRLCOLLfindtop (0x2C4398) e salva a posição no espaço do mundo. Latch_HangPullUpSide (0x2D1324) depende do fim da animação e das transformações corporais. São requisitos concretos para recuperar a escalada e as saídas. A travessia de BAT1 por autoplay atesta a direção da rota. A equivalência do gatilho de corpo ainda está aberta.


## Resultado da compilação e dos testes

| Configuração | Compilação limpa | Testes com áudio mudo |
|---|---|---|
| Clang Debug, pasta `build` | 181 etapas concluídas, zero avisos | 53 de 55 aprovados |
| MSVC x64 Release /MT, pasta `build-static` | 181 etapas concluídas, zero avisos | 55 de 57 aprovados, incluindo `test_static_imports` |

`test_full_campaign` e `test_fmv_transition` param em BAT2 nas duas configurações. Nos percursos diretos, 10 das 12 fases críticas retornaram vitória. BAT2 fica em x=447 e BAT3 repete um salto em x=657. O campo y=112 do log é relativo à câmera. No traço físico, Bones está a y=1354 no mapa de BAT2. Em BAT3, a oscilação de altura observada está entre aproximadamente 2513 e 2632. A trilha de RUNWAY chega ao registro 2635 com quatro perseguidores, 76 lançamentos e energia final 131072. As execuções usaram `MB_MUTE=1`, `--mute` no executável e a exclusão de `test_movie_audio` no CTest.

No executável Release final, 300 apresentações por fase totalizaram 3600 amostras. Todas ficaram abaixo de 16,6 ms de trabalho. O máximo foi 6,179 ms em MAUSO. As médias de apresentação por fase ficaram entre 59,967 e 59,988 FPS. O percentil 95 do intervalo entre apresentações chegou a 17,360 ms em MAUSO. Nenhum passo de simulação foi descartado nessa rodada. O ensaio inicial com xBR3x em RUNWAY mediu 49,545 FPS e 21,226 ms de trabalho no percentil 95. O arquivo `frame-summary.csv` permite conferir as 12 fases individualmente.

No diretório local de evidências, `pose-config.csv` traz disco, SHA-256, endereços, posição de origem, direção, oito coordenadas da caixa de contato e impulso das 22 configurações iniciais. O executável medido tem SHA-256 `9b1f8d185ab7790e1f6e3a8d61e7e44987117c0d6861819fa3186f897a828109`. Os logs são `compile-build.log`, `compile-build-static.log`, `final-build.log` e `final-build-static.log`. A reconstrução do corpo articulado, a distribuição de atores em outras fases e a equivalência de câmera e movimento da campanha inteira seguem abertas.

## BAT2 e BAT3 em 20 de novembro de 2026

Em Batwood, cruzar uma coluna do mapa era suficiente para o port declarar vitória. O Saturn compara a configuração do corpo por identidade e só então consulta a colisão de Bones com o objeto de saída. Os dois overlays estão no disco 1 da edição americana. Os endereços abaixo são os endereços carregados no SH-2, com base `0x232000`. A extração conferiu cada SHA-256 com `manifest/versions.txt`.

| Evidência do disco | /09BAT2 | /10BAT3 |
|---|---|---|
| Início da lista de objetos | `0x2d56b0` | `0x2d6ed0` |
| Configuração inicial em BonesStageInit | `0x2dba70 → 0x2d89b0` (SSRA) | `0x2dde80 → 0x2da550` (SSRHAL) |
| Tabela de conexões de BonesFindConfigInfo | `0x2db740` | `0x2dd9a8` |
| Tabela de tipos de CAnyBone | `0x2de884` | `0x2e0c94` |
| Latch_CheckViable | `0x2add2c` | `0x2aee10` |
| CEndBatwood, registro e seletor | `0x2d56b0`, seletor 2 | `0x2d789c`, seletor 3 |
| Brain da saída, posição projetada, estado exigido | `0x2cac6c`, (2364, 244), `0x2d83e0` (SSRHAL) | `0x2cc488`, (6017, 77), `0x2da170` (SSRHAALL) |

A configuração seleciona a altura dos pés, o alcance da mão, a caixa do ator, o impulso e as articulações visíveis. O mapa de ligação e os literais do código confirmam 39 pixels por unidade de mundo. A leitura da lista encontra 12 objetos de peças e 13 galhos em BAT2, 19 objetos de peças e 20 galhos em BAT3. O registro de 40 bytes de cada configuração admite até duas transições, identificadas pelas peças aceitas. Objetos com máscara múltipla alternam as peças em períodos de 100 atualizações. A atração usa o bombeamento de esqueletismo, a subida inicial e a aproximação ao ator antes da conexão.

O comando C do Saturn salta e mantém a pegada. O ponto da mão consulta a colisão de dois bits do plano A. O valor 2 seleciona a pegada lateral e o valor 1, a de costas. A busca do topo avança de dois em dois pixels. A escalada respeita a espera de oito atualizações da pegada, a verificação de espaço acima e a aproximação ao topo. Soltar C permite cair. No teclado, C salta ou segura, ↑ sobe, X ou Espaço dispara e Z atrai as peças.

A figura articulada lê os ramos ativos da configuração e as rotações por articulação em `03CODE`. Os quadriláteros referenciam registros reais de `04STXT` pela base `0x25c08000`. A saída usa a posição original em 16.16, a tolerância `0x8000` na distância ao quadrado e as articulações ativas. O teste exerce o contato no lugar correto com o corpo incompleto, o corpo completo em altura incorreta, a pausa durante a pegada e a vitória por entradas normais.

| Percurso nativo reproduzido | Primeira conexão | Segunda conexão | Saída |
|---|---|---|---|
| BAT2 | Quadril em (1796, 892), quadro 646 | Perna em (2268, 620), quadro 833 | Quadro 1033, SSRHAL |
| BAT3 | Perna em (158, 2542), quadro 26 | Braço em (635, 2645), quadro 127 | Quadro 2125, SSRHAALL |

Os quadros são do percurso automatizado. Os dois compiladores reproduziram os mesmos quadros finais. Em BAT2 houve 89 atualizações pendurado e 90 de subida. Em BAT3 houve 141 e 193, respectivamente.

### Comparação com a gravação do Saturn

As pranchas preservam o enquadramento de cada versão. O pareamento procura pontos do cenário fora do HUD e mede os resíduos depois de estimar escala, rotação e translação. Esses resíduos medem a correspondência do cenário. A pose e o instante de entrada do jogador exigem comparação separada. Segundo a descrição do autor, o gameplay é da edição americana e os cartões de fase e FMVs são da japonesa. [Gameplay usado como referência](https://www.youtube.com/watch?v=-c9br4VL-8I&t=1868s)

Em BAT2, o início, o tronco utilizado na subida e a placa de saída aparecem nas duas versões. No quadro final escolhido, o deslocamento vertical entre os marcos do cenário é de 72 pixels. Em BAT3, foram localizados o início, a parede com apoios verdes e a placa acima da pilha de toras. Na chegada, a translação medida foi de aproximadamente um pixel horizontal e menos de um pixel vertical. O port ainda apresenta inimigos em posições diferentes, principalmente na entrada. A câmera, as transições entre poses e o tempo da animação de escalada também divergem em quadros visíveis. A comparação utilizou a gravação e os overlays do disco. A equivalência quadro a quadro com uma captura nova de hardware ou do Ymir continua em aberto.

As saídas locais incluem [prancha de BAT2](../build-static/saturn-fidelity/09BAT2-saturn.png), [prancha de BAT3](../build-static/saturn-fidelity/10BAT3-saturn.png), [medidas dos marcos](../build-static/saturn-fidelity/bat-registration.csv), [trajetória de BAT2](../build-static/saturn-fidelity/09BAT2-poses.csv) e [trajetória de BAT3](../build-static/saturn-fidelity/10BAT3-poses.csv).
