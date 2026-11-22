# FINALE: anel e término da fase

Análise estática de `/27FINALE/03CODE`, disco 2 USA, 600.000 bytes,
SHA-256 `19962c17f9ca3c467b05229956e98058df72d05bf083e6c30006a05f8f59528b`.
O arquivo foi importado no Ghidra como SH-2 big-endian com base `0x00232000`;
as funções e suas instruções estão em `ghidra-finale-detail.txt`. O script
`tools/DumpFinale.java` permite reproduzir a extração. Isto é evidência estática,
com nomes e estruturas ainda sujeitos à confirmação em execução.

## Cadeia até o filme

- `CBlueShot::Brain` em `0x0028dec8` espera o estado do projétil e chama
  `CRing::ShootRing` em `0x0028d700` com a direção do disparo.
- `ShootRing` escolhe uma de 16 entradas de 108 bytes do anel. A tabela de
  direção em `0x002b21e0` contém `0, 128, 192, 64`. A posição selecionada
  depende também da fase do anel armazenada em `objeto+0x704`.
- Quando a entrada está ativa, `ShootRing` a desativa e decrementa o contador
  em `objeto+0x708`; se esse contador chegar a zero, grava `1` no byte
  `0x00228951` e chama `0x0028ce04` com argumento zero. Quando a entrada está
  inativa, ele a ativa e incrementa o contador. Também há chamadas de som e
  mudanças de animação em ambos os ramos.
- `0x0028ce04` encaminha o argumento ao criador de `CEndLevel` em
  `0x0028cbc8`; o cérebro em `0x0028ccd0`, no ramo padrão com resultado zero
  e campos opcionais não definidos, chama `PlayDuckMovie(3)`. A experiência
  anterior no Ymir confirmou esse filme com o resultado forçado, não a
  sequência completa de disparos ou uma vitória jogada.

`CRing::Brain` em `0x0028d5f4` itera 16 entradas, atualiza seus ponteiros
para a tabela em `0x002b0158` e avança a fase usando incrementos em
`0x002b2158`. O limite da tabela de posições usado nessa função é
`0x002b2158`, portanto o intervalo referenciado tem `0x2000` bytes. As
descrições antigas que atribuem `0x3ac` bytes à tabela ou descrevem
`ShootRing` apenas como gerador de projéteis não refletem essa rotina.

## Host (`special_phase`)

Implementação nativa em `src/core/special_phase.c` (2026-11-09):

- 16 slots (`ring_active[]`), contador `ring_active_count` (+0x708), fase
  publicada `ring_phase` (+0x704) e acumulador `ring_phase_acc` (Brain).
- Tabela de direção `@0x002b21e0` = `{0,128,192,64}`; incrementos de fase
  `@0x002b2158` (BE32 reais no host); seleção `ShootRing` =
  `((dir + 0x100 - phase) + 8) >> 4` com wrap em 16.
- `RingDefinitionTable@0x002b0158`…`0x002b2158` (0x2000 B): 256 registos de
  0x20 B = 4 pares `{dx,dy}` BE32. Slot `i` na fase `p` usa o registo
  `(p + i*16) & 255` (`paint_finale_rings` via `finale_ring_table.inc`).
- A arena começa com 16 setores acesos. O disparo, a órbita e a condição de vitória estão detalhados na seção sobre o duelo abaixo.
- A entrada interativa e `--full-campaign` adiam o filme até o último impacto. Os testes de reprodução também conseguem abrir o filme diretamente. Durante o filme o controle da arena fica inativo.
- Ao vencer, o port encerra o CD-DA e reutiliza `/27FINALE/20AVI`, preparado antes do duelo. O carregamento da edição USA ocupa 159 VBlanks medidos desde o pedido. O PCM começa dois VBlanks antes da primeira imagem.
- `_RingAnim` em `0x28B4C4` percorre quatro texturas de 112 × 30 pixels, três VBlanks por textura. O construtor inicializa os 16 relógios com `6 - RandomRange(24)`. Os endereços CG vão de `0x25C265E0` a `0x25C28D70`.
- `_RingOutAnim` em `0x28B60C` usa cinco texturas de `0x25C29AA0` a `0x25C2CF60`, com três VBlanks por textura. `_RingInAnim` em `0x28B648` percorre essas texturas em ordem inversa, com cinco VBlanks por textura. `ShootRing` muda a colisão no acerto. `CRing::Brain` retira o setor da lista de desenho quando a animação termina.
- Os comandos VDP1 usam `CMDPMOD=0x04A0`, `CMDCOLR=0x1200`, origem local (159,111) e índice zero transparente. As 256 cores do banco 2 em `_MainPalette` coincidem com a CRAM do Ymir. Cada componente da cor exibida combina 23/32 do sprite com 9/32 do fundo. A lista aparece dois VBlanks após a submissão. Os setores entram em ordem inversa de criação. O primeiro tile de fundo é exibido na linha 1.

- **SFX por disparo** (`!movie_ok`):
  - Chamada Saturn: `SoundEvent(id)` `@0x00204128`.
  - **acerto** `@0x002b21f0` = `{0x2A,0x2A,0x2B,0x29}` por dir → registo
    tipo 3 com `word[4]` = sample span4 `{12,12,13,11}` (68 B cada).
  - **activação** `@0x002b2200` = `{0x32,0x32,0x33,0x31}` → `word[4]` =
    `{0x14,0x14,0x15,0x13}` (indices ≥19 fora de span4 count=19); host
    usa sample 15 (516 B) via `runway_sfx_event_pcm`.
  - Flags legado: `FINALE_SFX_HIT`=`JUMP|C`, `FINALE_SFX_ACTIVATE`=`C`.
  - `phase->ring_sfx_event` + `mrbones` despacha o PCM do evento.

## Duelo jogável na edição USA

O direcional escolhe a defesa enquanto Bones permanece na arena. Cima, baixo, direita e esquerda correspondem às direções internas 0, 1, 2 e 3. Soltar o direcional devolve Bones à posição neutra 4. A primeira leitura de controle segue a espera de 60 atualizações de `CFBones::Brain`.

| Ação | Resposta nativa | Origem no SH-2 |
|---|---|---|
| Receber o raio vermelho de frente, com a guitarra vazia | Guarda uma carga e emite o evento `0x2F`. | `CFBones::BoltHit`, comparação em `0x28D3B4–0x28D3BE`. |
| Receber de outra direção ou com uma carga guardada | Perde `0x9999` de energia, descarrega a guitarra e emite `0x30`. | `0x28D3DA–0x28D3F0`. |
| Pressionar B com carga (X ou Espaço no Windows) | Cria um raio azul e consome a carga. Manter B pressionado antes do bloqueio exige soltar e pressionar novamente. Disparar da posição neutra aponta para sul. | `CheckJoy` em `0x28D274–0x28D2AC`, transições em `0x2B00B4`. |
| Raio azul alcançar o campo de força | Consulta a orientação do anel no impacto. Apaga um setor aceso ou reacende um setor apagado. Apagar o último setor inicia o filme. | `CBlueShot::Brain` em `0x28DEC8`, `CRing::ShootRing` em `0x28D700`. |

O gerenciador escolhe a direção com os dois bits baixos de `_rand` do carregador. O port lê a tabela em `/02+0x8528`. Na entrada observada no disco 2, o gerador começa em zero. Os 23 construtores das juntas fazem duas chamadas cada e as 16 animações do anel fazem uma por setor. A primeira escolha de direção encontra `0xB336BA3E` no tick 3. Duas chamadas no tick 6, com retornos em `0x29547A` e `0x20489A`, também avançam o gerador usado nos ataques seguintes. O replay verifica a semente produzida pela inicialização normal. As quatro sequências apontadas por `0x2B2EE0` têm 29, 28, 28 e 28 registros. Cada registro ocupa quatro yields. A criação de `CBolt` ocorre no último. O marcador `AnimFlag` do vermelho está no índice 25, 21, 25 ou 25 conforme a direção. O azul alcança o anel no índice 19 em todas as direções. O construtor do anel inicializa os 16 setores em `0x28D506` e o contador em `0x28D53C`. O índice da tabela de velocidade é o número de setores acesos, inclusive a entrada 16 (`0x20000`).

Os descritores dos raios vermelhos apontam para texturas dentro de `03CODE`. Os azuis apontam para a VRAM abastecida por `04STXT`. Na rotina `_VDP1TxtrToVram` em `0x207230`, a palavra no deslocamento 10 do cabeçalho distingue pixels literais e pacotes que alternam zeros e literais. Há quadros dos dois tipos na mesma animação vermelha. O teste verifica os 3.024 índices decodificados do décimo quadro do raio leste por SHA-256.

O estado existente do Ymir confirma câmera em `(0, 0)` na WRAM e origem local VDP1 em `(159, 111)`. A posição do gerenciador em `0x28D896` é `(-1560, 23405)` em ponto fixo. A projeção de 42 pixels por unidade de `RWorldSprite` coloca a origem dos raios em **(159, 96)**. Cada quadro usa o deslocamento do próprio descritor. [Extração do estado e dos dois discos](../build-static/saturn-fidelity/finale-charge/reference.txt)

### Comparação e limites

No vídeo do Saturn, 20 picos de vermelho no interior da arena entre 76min26,50s e 77min02,70s deram mediana de **1,90 s** entre ataques. No port, 16 criações de projéteis deram mediana de **114 atualizações**, equivalentes a **1,90 s a 60 Hz**. O vídeo mede a passagem do efeito por uma região da imagem. O contador nativo mede a criação do projétil. A comparação verifica o intervalo entre ataques. O vídeo não contém o histórico do controle para medir latência entre pressão e resposta. [Gameplay de referência](https://www.youtube.com/watch?v=-c9br4VL-8I&t=4588s) · [Medições por quadro](../build-static/saturn-fidelity/finale-charge/video-colors.csv) · [Estados nativos](../build-static/saturn-fidelity/finale-charge/arena.csv)

Na rota automática, o primeiro bloqueio ocorreu no passo 135 e o primeiro setor apagou no passo 155. O último impacto armou o filme no passo 1.910, com 16 disparos e energia integral. Com o controle neutro, o quarto impacto causou derrota no passo 477. A campanha do executável percorreu menu, 22 fases, troca de disco, duelo, 5.658 quadros de filme e retorno ao menu. O ensaio do duelo cobrou os botões pela interface normal do controle. [Rota neutra](../build-static/saturn-fidelity/finale-charge/neutral.csv) · [Log da campanha](../build-static/saturn-fidelity/finale-charge/campaign.log)

A primeira prancha registrou as diferenças que motivaram a reconstrução de Bones, da guitarra e dos efeitos de bloqueio e dano. [Comparação anterior](../build-static/saturn-fidelity/finale-charge/comparativo-finale.png)

## Medição de 20 de novembro de 2026 com a fita sustentada

O núcleo oficial Ymir 0.4.1, revisão `d49c0868cf9998d279394498ad0cd0efbd102ccb`, montou o disco 2 USA e carregou o estado de seleção direta de Los Endos. O primeiro quadro da arena corresponde à atualização absoluta 557. Duas execuções do original com a fita congelada produziram os mesmos estados e eventos SH-2. O port abriu a fase pelo carregador normal e consumiu os 2.480 estados físicos do controle. O cabeçalho da fita confere a semente obtida na inicialização. A fita contém apenas a máscara de botões por quadro e seu SHA-256 é `e9efd9737c56103ca0be3cded4fd0a22d7ab5e463002ba87193fdcbad5fad2ca`. [Fita](../tests/fixtures/finale-victory-usa.csv) · [Origem do estado e repetição no Ymir](../build-static/saturn-fidelity/finale-parity/ymir/LEIA-ME.md)

| Evento | Ymir | Port Windows |
|---|---:|---:|
| Primeiro dano, energia passa de 131.072 para 91.751 | 137 | 137 |
| Primeiro bloqueio com carga | 251 | 251 |
| Último dos 16 setores se apaga | 2064 | 2064 |
| Raio que já estava em voo reduz a energia para 52.430 | 2079 | 2079 |
| Pedido do filme final | 2127 | 2127 |
| Primeiro quadro do AVI visível | 2286 | 2127 |

Os 16 setores se apagaram na mesma ordem. Sete impactos ocorreram no mesmo quadro e nove diferiram por um. Até a vitória, a orientação coincidiu em 2.039 de 2.064 quadros e a carga em 2.052. A fase angular observada no Windows ficou entre zero e duas unidades à frente, numa volta de 256. O traço do SH-2 mostrou que o original pode publicar zero ou duas atualizações do anel entre VBlanks consecutivos. Uma fita exploratória com um toque de B de um único VBlank venceu no Saturn e perdeu no port. A fita de regressão usa pulsos sustentados em ambos. [Comparação por quadro](../build-static/saturn-fidelity/finale-parity/native/comparison.txt) · [Ordem dos impactos](../build-static/saturn-fidelity/finale-parity/native/sector-events.csv) · [Ensaio do toque curto](../build-static/saturn-fidelity/finale-parity/native/short-pulse-static.txt)

O Windows já dispõe do AVI em memória ao terminar a arena. O primeiro quadro de 320 × 224 apresentou o mesmo RGB nas duas versões, com SHA-256 `cdfc0bae24133fcebe4a84e10797aea4d271f43e0bbac349d09778ec12f700bd`. Um quadro posterior, com 8.376 cores, também coincidiu pixel a pixel no quadro 2480 do Ymir e nos quadros 2323 a 2326 do replay nativo. A espera de 159 VBlanks do Saturn entre o pedido e a primeira imagem equivale a aproximadamente 2,65 s. [Quadros do filme](../build-static/saturn-fidelity/finale-parity/native/movie-frames.csv) · [Transição lado a lado](../build-static/saturn-fidelity/finale-parity/comparison/vitoria.png)

### Pose, guitarra e dano

O port lê as 23 articulações, as ligações entre peças, as malhas e as sequências de movimento de `/27FINALE/03CODE`. A tabela de mudanças de direção está em `0x2B00B4`. A alça e a guitarra ocupam os nós 21 e 22. As texturas vêm de `04STXT`. A escala resulta da projeção das peças articuladas em 320 × 224, com a origem local VDP1 em (159, 111). A seleção da face dos ossos longos segue a orientação da matriz. A guitarra usa as paletas do próprio objeto e alterna as cores enquanto tem carga. [Poses nas quatro direções](../build-static/saturn-fidelity/finale-parity/comparison/poses.png) · [Máscaras do corpo e da guitarra](../build-static/saturn-fidelity/finale-parity/pose/rig-comparison.csv)

`CFBones::BoltHit` em `0x28D390` e `CShotHit::Brain` em `0x28E098` distinguem a defesa do dano. A tabela em `0x2B2F74` aponta para quatro sequências direcionais de dano com 15 imagens e dois ciclos por registro. A defesa usa outra sequência de 15 imagens, com um ciclo por registro e deslocamento para o lado de onde vem o raio. Os pixels vêm de `16DTXT`, carregado no Saturn em `0x06040000`. Raios, faíscas, corpo e guitarra compartilham a ordenação de profundidade. O cálculo de cor preserva o fundo VDP2. Na perda de energia do quadro 137, a primeira apresentação vermelha ocorre em 139 e a barra de saúde passa de 14 para 11 peças em 140. A paleta recupera dois níveis por canal a cada atualização. O escurecimento geral após a vitória vem da rotina separada `PalMasterFade(-5)`. [Efeito e guitarra](../build-static/saturn-fidelity/finale-parity/comparison/dano-e-guitarra.png) · [Animações do disco](../build-static/saturn-fidelity/finale-parity/impact/disc.csv) · [Registros VDP2 no encerramento](../build-static/saturn-fidelity/finale-parity/ymir/original-fade.csv)

Quando o disparo apaga um setor, `CBlueShot::Brain` escolhe a sequência de oito imagens de `16DTXT` em `0x060CAC20` e aplica o deslocamento direcional da tabela em `0x2B2F34`. Na primeira quebra do anel, o Saturn registra o impacto no quadro 283 e exibe o clarão entre 285 e 292. Na última quebra, o efeito continua durante o escurecimento. [Sequências das duas quebras no Saturn](../build-static/saturn-fidelity/finale-parity/ymir/ring-impact-window/sectors.png)

Foram comparados os 40 VBlanks consecutivos de 62 a 101. O índice de interseção sobre união das silhuetas de Bones e guitarra foi de 91,44% na média e 86,28% no mínimo. No quadro 70, ambos ocuparam a caixa de 51 × 78 pixels. No 85, a guitarra cruzou a cabeça em ambos, com a mesma silhueta de 349 pixels numa caixa de 47 × 25. No RGB capturado do executável, a mediana da maior diferença entre canais nesses pixels foi 2 em 255. No quadro 101, somente 64,97% dos pixels da guitarra ficaram a até 12 níveis por canal do original. O traço SH-2 explica a posição da guitarra na troca de direção. O Saturn combina as animações ainda ativas, submete a pose e só depois avança pesos e chaves de movimento. [Silhuetas e caixas por quadro](../build-static/saturn-fidelity/finale-parity/pose/rig-comparison.csv) · [Cores medidas no executável](../build-static/saturn-fidelity/finale-parity/pose/presented-comparison.csv) · [Ordem das chamadas no SH-2](../build-static/saturn-fidelity/finale-parity/ymir/pose-window/calls.csv)

No quadro 143, os 2.899 pixels cobertos pela textura do dano ficaram a até oito níveis por canal da imagem do Saturn. No 145, a mesma medida atingiu 98,89% de 4.777 pixels. O teste inclui áreas em que o personagem pode cobrir a textura. No quadro 2079, durante o escurecimento, a média RGB do quadro completo caiu para aproximadamente 1,4 em 255. [Medidas do impacto](../build-static/saturn-fidelity/finale-parity/comparison/efeito.csv) · [Comparação do filme alinhada pelo conteúdo](../build-static/saturn-fidelity/finale-parity/comparison/filme.png)

## Validação de 21 de novembro de 2026

Cada linha abaixo usa 2.480 máscaras físicas de controle idênticas nos dois executáveis. Os valores são VBlanks relativos à entrada na arena. A edição USA foi executada a partir do disco 2 no núcleo Ymir 0.4.1. O port recebeu as máscaras pelo controle da fase.

| Fita | Último setor, Saturn e Windows | Pedido de filme | Início do PCM | Primeira imagem |
|---|---:|---:|---:|---:|
| B sustentado | 2064 | 2127 | 2284 | 2286 |
| Pulsos breves de B | 2060 | 2123 | 2280 | 2282 |

As duas fitas terminam com 16 disparos, dois danos e energia de 52.430. O rastreio de 2.062 chamadas a `CheckJoy` identificou quais VBlanks físicos fornecem a amostra ao SH-2. Na fita curta, um toque restrito ao VBlank 488 cai entre duas aquisições. Na medição anterior à correção de paleta abaixo, alguns disparos intermediários chegaram um VBlank antes ao port. [Amostragem original do controle](../build-static/saturn-fidelity/finale-input/acquisition-summary.txt) · [Fita de pulsos breves](../tests/fixtures/finale-short-pulse-usa.csv)

O original procura o overlay, abre o AVI, carrega o índice e habilita o display. A calibração dessa sequência se aplica ao encerramento do duelo USA. O botão Start mantido na primeira consulta do player pula o filme antes da imagem. Dez quadros da fita sustentada e sete da curta tiveram RGB integral idêntico entre o executável estático e o Saturn, nos mesmos ticks. O instante de início do PCM foi validado com o host mudo. [Rastro do leitor e do player](../build-static/saturn-fidelity/finale-timing/film/findings.md) · [Comparação da Release](../build-static/saturn-fidelity/finale-seguimento/release-movie.csv)

Para os VBlanks 62–101, a silhueta conjunta de Bones e da guitarra coincidiu nos 40 quadros. O port lê as rotações das articulações, as fases iniciais e a máscara de `_WiggleInfo` da edição USA e usa a precisão do SH-2 para combinar as poses. A ordenação do Rendeng em `0x205E90` troca nós adjacentes depois de preparar a lista de desenho. Nos 40 quadros, o executável produziu o mesmo RGB nos 43.767 pixels visíveis de Bones e da guitarra, incluindo os 8.906 da guitarra. O quadro 101 também coincidiu em todas as palavras de cor do framebuffer VDP1. No trecho de dano, a comparação inclui a recuperação da paleta e a sobreposição da textura de impacto. [RGB do executável](../build-static/saturn-fidelity/finale-seguimento/release-rig-rgb.csv) · [Pose e guitarra por quadro](../build-static/saturn-fidelity/finale-seguimento/pose/production-check.csv) · [Pixels da textura de dano](../build-static/saturn-fidelity/finale-seguimento/damage-rgb.csv)

Os pixels dos setores, identificados pelo banco de cor no framebuffer do Ymir, coincidiram nos 40 quadros de 62 a 101. A comparação cobre as bordas em que duas texturas se sobrepõem. Na versão anterior, o erro médio nesses pixels era 72,67 níveis RGB em 255. [Medidas do anel](../build-static/saturn-fidelity/finale-seguimento/ring-rgb.csv) · [Original, antes, depois e diferenças](../build-static/saturn-fidelity/finale-seguimento/comparativo-finale.png)

O aviso visual que antecede o disparo usa a paleta do cenário VDP2. `CShotManager::FireBolt` em `0x28D90C` aciona duas listas vermelhas e duas de retorno ao cinza por direção. A CRAM do Ymir confirmou o ciclo nos 40 VBlanks de 62 a 101. O port atualiza os texels dos índices usados e aplica o passo de 32 níveis por canal. No quadro 85, o ponto em (226,39) passou de RGB (1,0,0) para (87,0,0), o valor da captura original. [Comparação das luzes](../build-static/saturn-fidelity/finale-input/lights-comparison.csv)

Na Release final, a amostragem de sprites de dimensão par também preserva o texel central escolhido pelo VDP1. Os 12.025 pixels da área do efeito de dano nos quadros 139–145 tiveram RGB igual ao original. A imagem completa no segundo dano, no tick 2079, também coincidiu. Naquela compilação, a borda inferior e a quebra de setores ainda divergiam. No quadro 290 da fita sustentada, 268 dos 71.680 pixels diferiam. [Efeito e sobreposições](../build-static/saturn-fidelity/finale-seguimento/damage-rgb.csv) · [Imagem completa por quadro](../build-static/saturn-fidelity/finale-seguimento/release-screen.csv)

### Reproduzir a vitória no Windows

Na raiz do projeto, com os dois discos USA extraídos, o comando abaixo encerra com código zero quando o replay chega ao filme. Os testes `test_finale_saturn_replay` e `test_finale_short_pulse` verificam o quadro da vitória, o pedido do filme, a primeira imagem, a energia, os disparos e os danos. Para usar a segunda fita no comando, substitua o nome do CSV por `finale-short-pulse-usa.csv`.

```powershell
$env:MB_MUTE = '1'
$disc1 = 'disco1/Mr. Bones (USA) (Disc 1).cue'
$disc2 = 'disco2/Mr. Bones (USA) (Disc 2).cue'
& .\build-static\mrbones.exe $disc1 manifest/versions.txt --disc2 $disc2 `
  --level /27FINALE --finale-replay tests/fixtures/finale-victory-usa.csv `
  --no-hd-bones --mute --save-dir build-static/finale-replay-save
```

### FINALE USA no disco 2 — borda, quebra e instante de impacto em 21/11/2026

O controle físico foi aplicado VBlank a VBlank ao jogo original no Ymir 0.4.1 e ao executável Windows. As três partidas começaram no mesmo estado da arena, com a semente `0xb336ba3e`. As duas fitas do projeto forneceram 4.960 pares de imagens RGB de 320 × 224. Uma terceira fita deslocou as pressões de B e preservou as direções para verificar o instante dos impactos fora das partidas usadas na reconstrução.

| Fita | Impactos no VBlank original, antes → agora | Pixels divergentes na tela 290, antes → agora | Último setor | Pedido do filme | Primeira imagem |
|---|---:|---:|---:|---:|---:|
| B sustentado | 7/16 → 16/16 | 268 → 0 | 2064 | 2127 | 2286 |
| Toques curtos | 5/16 → 16/16 | 1.002 → 0 | 2060 | 2123 | 2282 |
| B deslocado | 7/16 → 16/16 | 16 na compilação daquela medição, todos na figura de Bones | 2062 | 2125 | 2284 |

Cada partida teve 2.480 amostras de controle, 16 disparos, dois danos e energia final 52.430. A conferência das duas fitas do projeto incluiu a mudança de máscara escrita pelo SH-2 em `CRing::ShootRing` (`0x28D700`). A terceira confirmou a mudança na WRAM e a primeira imagem visível do filme. [32 impactos das fitas do projeto](../build-static/saturn-fidelity/finale-ajustes/sectors-release.csv) · [16 impactos do controle independente](../build-static/saturn-fidelity/finale-ajustes/holdout-release.csv) · [Amostras visuais da terceira fita](../build-static/saturn-fidelity/finale-ajustes/holdout-frames.csv)

- **Borda inferior.** O original reescreve `SCYN2` depois da linha 222. Na linha 223, NBG2 fica transparente e NBG1 exibe a linha 222 do mapa de trás. O registrador de cor também já contém o próximo passo do fade. O renderizador agora reproduz essa leitura das camadas. Os 1.587.200 pixels da última linha coincidiram nas duas partidas completas, inclusive durante a entrada e a saída. [Rastreio de NBG1/NBG2 e dos fades](../build-static/saturn-fidelity/finale-ajustes/borda/conclusao.md)
- **Quebra dos setores.** O projétil azul conserva seu lugar na lista de desenho quando passa para a animação de impacto. O port preserva a última imagem dessa animação até a próxima atualização e reproduz a ordem do personagem, do anel e do indicador de energia. Na fita sustentada, as diferenças internas no quadro 290 eram 96 pixels de sobreposição entre anel e efeito, 17 no indicador e cinco na mão de Bones. A linha inferior respondia pelos outros 150. As duas fitas coincidiram em toda a tela do quadro 280 ao 296, inclusive. [Prancha da fita sustentada](../build-static/saturn-fidelity/finale-ajustes/comparison-steady.png) · [Prancha dos toques curtos](../build-static/saturn-fidelity/finale-ajustes/comparison-short.png)
- **Instante do impacto.** A rotina de paleta executada antes do anel tem custo variável conforme as cores em transição. O código nativo usa essa carga para publicar a colisão, a máscara e o evento de som antes ou depois da fronteira observada no raster. O movimento mantém seu relógio lógico. Todos os 48 impactos caíram no VBlank original. O modelo ainda subestima o trabalho de outros atores em 25 de 2.001 passagens do anel sem impacto observado nessas divergências. [Rastreio SH-2, ciclos e limites da calibração](../build-static/saturn-fidelity/finale-ajustes/timing/scheduler-evidence.md)

A comparação integral encontrou alguma diferença de RGB em 416 dos 2.480 quadros da fita sustentada e 621 dos 2.480 da curta. No controle independente, os 16 pixels divergentes do quadro 290 estão na figura de Bones, em x=155–163 e y=85–94. O anel inteiro coincidiu nesse quadro. [RGB das duas partidas por quadro](../build-static/saturn-fidelity/finale-ajustes/screen-release.csv) · [Localização dos 16 pixels da terceira fita](../build-static/saturn-fidelity/finale-ajustes/holdout-290.png)

Os testes `test_finale_saturn_replay` e `test_finale_short_pulse` agora verificam o VBlank e a máscara de cada um dos 16 setores e o SHA-256 da imagem inteira do quadro 290, além da transição de vitória. O teste de fase verifica a última linha e o fade. Ao usar o executável anterior, o teste de replay falhou nos impactos intermediários mesmo tendo chegado ao filme. [Referência de regressão](../tests/fixtures/finale-impact-usa.csv) · [Teste de replay](../tests/test_finale_replay.cmake) · [Controle negativo](../build-static/saturn-fidelity/finale-ajustes/negative-control.txt)

Em uma fita curta adicional de 430 entradas, um segundo disparo no mesmo setor reativou o setor 13 no VBlank 400, depois da quebra no 283. A máscara do port coincidiu com a WRAM nas 430 atualizações. As 37 imagens completas da recompilação, dos quadros 388 a 424, coincidiram com o Ymir. Essa fita termina antes da vitória. [Reativação na estática entregue](../build-static/saturn-fidelity/finale-ajustes/setor/reactivate/release-check.csv)

Um resíduo fora da primeira quebra foi localizado no quadro 1084 da fita sustentada. NBG2 tem prioridade 5 e esconde parte de um efeito VDP1 de prioridade 4. Ao desativar NBG2 no depurador, os 31 pixels investigados mostraram o efeito azul que fica à vista no port. [Conferência das janelas de impacto por setor](../build-static/saturn-fidelity/finale-ajustes/setor/sectors-summary.csv)

### 21/11/2026 — correção da figura de Bones e da prioridade do efeito azul

O raster do quadro 290 da fita deslocada contém os mesmos vértices, texturas e espelhamentos nos 60 polígonos visíveis de Bones. Na versão anterior, a troca da direção e o disparo simultâneos no quadro 258 alteravam a sequência de animações e as sobreposições que persistiam no quadro 290. Dez pixels provinham do cruzamento das articulações 4 e 14, cinco do cruzamento 2/14 e um do cruzamento 2/3. A ordem original em `CFBones::CheckJoy` consulta a direção em `0x28D270` e processa o disparo em `0x28D28C`. A imagem completa agora coincide no Ymir e no Windows. Duas chamadas de BonesCreateMotion na mesma atualização usam o subtotal de BonesBuild em `0x2B4034`. A segunda cancela o movimento ainda pendente. Preservar essa sequência também faz o quadro 721 da fita curta coincidir integralmente. [Rastreio da lógica de controle](../build-static/saturn-fidelity/finale-pose-prioridade/pose/cfbones.asm) · [Rotina original de mistura](../build-static/saturn-fidelity/finale-pose-prioridade/pose/mix.asm) · [Fita de controle](../tests/fixtures/finale-displaced-usa.csv) · [Comparação visual](../build-static/saturn-fidelity/finale-pose-prioridade/comparacao.png)

No quadro 1084 da fita sustentada, o texel azul `0x82EB` está no framebuffer VDP1 em (139,201). Na composição final, a camada NBG2 cobre esse texel. A máscara de cobertura vem de `/27FINALE/07ACDB` e `08ATIL` antes da conversão para RGB, preservando a diferença entre índice transparente e cor preta opaca. A VRAM2 do Ymir confirmou 13.800 pontos opacos e 57.880 transparentes. A comparação da tela inteira passou de 31 diferenças para zero. [Máscara e teste da prioridade](../build-static/saturn-fidelity/finale-pose-prioridade/priority/resultado.md)

| Fita | Quadros idênticos em 2.480, antes → agora | Diferenças de pixel somadas, antes → agora | Vitória | Primeira imagem do filme |
|---|---:|---:|---:|---:|
| B sustentado | 2.064 → 2.136 | 433.396 → 418.037 | 2064 | 2286 |
| Toques curtos | 1.859 → 2.124 | 796.215 → 564.473 | 2060 | 2282 |
| Toques deslocados | 2.034 → 2.129 | 569.519 → 534.755 | 2062 | 2284 |

Os 48 impactos e as 7.440 linhas inferiores coincidiram com o original. O comparador registra separadamente os pixels corrigidos e os que passam a divergir em cada quadro. As medições abrangem estas três fitas da campanha USA. A validação de reativação também confirma a imagem integral do Saturn nos quadros 388–424 da quarta fita. [RGB por quadro](../build-static/saturn-fidelity/finale-pose-prioridade/screen-release.csv) · [Máscaras e impactos](../build-static/saturn-fidelity/finale-pose-prioridade/hits-release.csv) · [Reativação](../build-static/saturn-fidelity/finale-pose-prioridade/reactivation.csv)

Para repetir a comparação, executar `python replay.py ymir holdout steady short`, `python replay.py release holdout steady short` e `python compare.py release` na pasta de evidências, preservando a captura `baseline` da versão anterior. Os três testes de replay em CTest usam somente o port, os discos e os hashes de referência. [Executor dos replays](../build-static/saturn-fidelity/finale-pose-prioridade/replay.py) · [Comparador de imagens e controles](../build-static/saturn-fidelity/finale-pose-prioridade/compare.py) · [Teste de regressão](../tests/test_finale_replay.cmake)

Das 7.440 telas comparadas, 6.389 coincidiram integralmente. A soma de diferenças caiu em 281.865 pixels. Em 5 quadros, a contagem individual aumentou frente ao executável anterior. Os aumentos individuais ocorreram em B sustentado 274 (+1), Toques curtos 264 (+19), Toques curtos 279 (+2), Toques curtos 952 (+70), Toques curtos 953 (+28).

Os cinco aumentos somam 120 diferenças de pixel. A análise de RAM e da lista VDP1 atribuiu os quadros 274 da fita sustentada e 279 da curta à ordem dos polígonos. Nos quadros 264, 952 e 953 da fita curta, a continuação da animação de bloqueio ainda adianta uma atualização. [Comparação dos comandos de desenho](../build-static/saturn-fidelity/finale-pose-prioridade/pose/rescompare.py)

### 21/11/2026 — reação ao bloqueio e sobreposição dos polígonos na FINALE USA

No SH-2, a atualização de CFBones consome o prazo de animação em `0x28D11E`. O ator de projétil chama `BoltHit` e inicia o bloqueio em `0x28D3CE`, depois dessa atualização. Na versão anterior, os pesos das poses antigas já diminuíam uma atualização antes do Saturn. A correção do relógio da defesa recuperou as 23 matrizes e os 66, 60 e 64 comandos dos polígonos, respectivamente nos quadros 264, 952 e 953 da fita curta.

A comparação do framebuffer e da lista de renderização revelou um segundo efeito. Na curta, o raio azul já participava da ordenação um passe antes do original. Quando chegava sua primeira imagem, o nó do port já havia trocado de lugar com faces de Bones. A ordenação incremental dava continuidade à diferença mesmo depois de as texturas e coordenadas coincidirem. Em 3.597 comandos de efeito entre os quadros 3 e 2.080 das três fitas, textura, dimensões e posição coincidiram. As 120 chaves de profundidade das faces nos cinco quadros coincidiram com as somas assinadas do Saturn, medidas depois do callback de cada objeto. A simulação das trocas entre vizinhos previu a ordem dos nós sobreviventes em 111 transições da lista original. [Chaves e ordem dos atores](../build-static/saturn-fidelity/finale-bloqueio-poligonos/atores/resultado.md) · [Posições e texturas dos sprites](../build-static/saturn-fidelity/finale-bloqueio-poligonos/bloqueio/leaves-summary.txt) · [Conferência dos efeitos nas três fitas](../build-static/saturn-fidelity/finale-bloqueio-poligonos/bloqueio/validated.txt) · [Reconstrução da lista e contraprovas](../build-static/saturn-fidelity/finale-bloqueio-poligonos/poligonos/conclusao.md)

Na Release, os cinco quadros e a janela 959–962 da terceira fita coincidiram integralmente com o original, tanto na superfície nativa como na área de jogo apresentada pelo GDI. A regressão usa o mesmo executável, os discos USA, as fitas de entrada e os hashes dos quadros gravados no Ymir. O executável anterior falha em todas as três fitas com o novo conjunto de imagens de referência. [Hashes dos quadros](../tests/fixtures/finale-frames-usa.csv) · [Teste de replay](../tests/test_finale_replay.cmake) · [Comparação visual](../build-static/saturn-fidelity/finale-bloqueio-poligonos/comparacao.png)

| Fita | Quadros idênticos em 2.480, antes → agora | Diferenças de pixels somadas, antes → agora | Vitória | Primeira imagem do filme |
|---|---:|---:|---:|---:|
| B sustentado | 2.136 → 2.284 | 418.037 → 355.134 | 2064 | 2286 |
| Toques curtos | 2.124 → 2.290 | 564.473 → 492.666 | 2060 | 2282 |
| Toques deslocados | 2.129 → 2.278 | 534.755 → 468.776 | 2062 | 2284 |

As evidências permitem repetir a medição com `python replay.py ymir steady short holdout`, `python replay.py release steady short holdout` e `python compare.py release` na pasta de validação. As capturas verificam a matriz RGB, os instantes dos impactos e as máscaras efetivamente recebidas pelo jogo a cada VBlank. [Replays](../build-static/saturn-fidelity/finale-bloqueio-poligonos/replay.py) · [Comparador](../build-static/saturn-fidelity/finale-bloqueio-poligonos/compare.py) · [Medições](../build-static/saturn-fidelity/finale-bloqueio-poligonos/verification.json) · [Revisão independente do Gemini](../build-static/saturn-fidelity/finale-bloqueio-poligonos/gemini/parecer.txt)

No quadro 486 das fitas curta e deslocada, a contagem passou de 636 para 648. Na fita curta, a reprodução isolada localizou os 12 pixels adicionais no cruzamento das articulações 22 e 10. Os 67 polígonos comparados têm a mesma geometria. [Comandos e sobreposição no 486](../build-static/saturn-fidelity/finale-bloqueio-poligonos/bloqueio/controlorder.txt) Um objeto CRender adicional do Saturn participa da lista nos quadros apresentados 444–495. Seu endereço é `0x060292C4` na fita curta e `0x06029074` na deslocada. Ao incluí-lo na simulação, a ordem das folhas volta a coincidir. O efeito visual completo desse objeto segue por reconstruir. [Contraprova de inclusão do objeto](../build-static/saturn-fidelity/finale-bloqueio-poligonos/poligonos/controle-objeto-486.txt)
