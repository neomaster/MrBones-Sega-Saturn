# Arquitetura de `sidescrl_motion`

Especificação do movimento do protagonista nas fases de sidescroll: andar, parar em paredes,
subir degraus e rampas, pular e cair, tudo composto em **uma função determinística por frame**.

**Origem.** Especificação *clean-room* (C99, só inteiros, sem alocação, sem globais). Nada aqui foi
recuperado do binário do Saturn. As constantes de gravidade, velocidade de queda, caminhada e salto
são *placeholders* sobrescrevíveis em tempo de compilação (seção 7).

**Fonte de verdade.** Os comentários de [`sidescrl_motion.h`](../src/core/sidescrl_motion.h) e das
dependências. Este documento explica a arquitetura e registra os números medidos; se divergir do
código, o código vence.

```c
void sidescrl_motion_step(SidescrlActor *actor, int32_t walk_dir, bool jump_edge,
                          int32_t max_climb_step, SidescrlWallFn is_wall,
                          SidescrlGroundFn get_ground, void *map_ctx);
```

## 1. Módulos e dependências

```
sidescrl_motion ──► sidescrl_slope ──► sidescrl_physics ──► aabb_hitchk
       │
       └──────────► sidescrl_wall ───► aabb_hitchk
```

| Módulo | Papel | Eixo |
|---|---|---|
| `aabb_hitchk` | Interseção de caixas, máscara de camadas, `ground_clamp` | ambos |
| `sidescrl_physics` | `SidescrlActor`, poses, gravidade, integração em ponto fixo com resto sub-pixel | ambos |
| `sidescrl_slope` | Terreno como perfil de alturas: degraus e rampas, janela de contato | vertical |
| `sidescrl_wall` | Limita um deslocamento horizontal para parar rente a uma parede | horizontal |
| `sidescrl_motion` | Orquestra os três na ordem certa e resolve as interações entre eles | ambos |

## 2. Convenções

- Pixels, **+x para a direita, +y para baixo**. Caixas semiabertas: colunas `[x, x+w)`, linhas
  `[y, y+h)`. Os pés ficam em `y + h`.
- Ponto fixo: `SIDESCRL_PHYS_UNITS_PER_PIXEL = 2341`. Velocidades e restos (`rem_x`, `rem_y`) estão
  nessa unidade. Divisão por piso, aritmética saturada em `int32_t` via `int64_t`.
- Poses ancoradas nos pés (largura 20 em todas): STAND 42 de altura (topo em `y`), DUCK 24 (topo em
  `y+18`), JUMP 36 (topo em `y+6`).
- Terreno: dois callbacks que **precisam descrever o mesmo mundo**.
  - `is_wall(ctx, col, row)`: o pixel é sólido?
  - `get_ground(ctx, col)`: `y` do pixel sólido mais alto da coluna, ou `SIDESCRL_SLOPE_NO_GROUND`
    (`INT32_MAX`) se a coluna está vazia. O solo é amostrado na **coluna central**, `x + w/2`.

## 3. Pipeline do frame

Cinco estágios, nesta ordem. Os quatro primeiros são código; o quinto é uma propriedade.

| # | Estágio | Lê | Escreve |
|---|---|---|---|
| 1 | Andar (intenção horizontal) | `walk_dir`, `on_ground`, `vx` | `facing_left`, `vx` |
| 2 | Sub-pixel + parede | `vx`, `rem_x`, caixa, `on_ground`, callbacks | `rem_x`, `vx` (se cortado), `x` |
| 3 | Impulso de salto | `jump_edge`, `on_ground` | `vy`, `on_ground`, `jumping` |
| 4 | Solo / rampa / gravidade | `vy`, `y`, `rem_y`, caixa, `get_ground` | `y`, `rem_y`, `vy`, `on_ground`, `jumping` |
| 5 | Caixa | `x`, `y`, `pose` | nada (derivada) |

### 3.1 Andar

`walk_dir` vira -1/0/+1 pelo sinal. Direção diferente de zero define `facing_left`. **Só no chão**,
`vx` se aproxima de `dir * WALK_MAX` em passos de `WALK_ACCEL`, ou de 0 em passos de `FRICTION` quando
`walk_dir == 0`. No ar, `vx` é mantido: sem controle aéreo e sem atrito aéreo.

### 3.2 Sub-pixel e parede

1. **Sub-pixel.** `total = rem_x + vx`; os pixels inteiros (divisão por piso) são o `desired` e o
   resto fica em `rem_x`, sempre em `[0, 2341)`.
2. **Tolerância de degrau.** No ar é 0. No chão é `base + R`, com `base = max(max_climb_step, 0)` e
   `R` = quanto o solo sob a **coluna dianteira** (última coluna do lado do movimento) está acima
   dos pés (0 se está nivelado ou abaixo). `R` tem teto `altura - 1 - base`, para a linha do topo da
   caixa continuar bloqueando.
3. **Parede.** `sidescrl_wall_clamp_dx` devolve quantas colunas inteiras, até `|desired|`, estão livres
   de pixel sólido nas linhas bloqueantes `[y, pés - tolerância)`. Para na primeira coluna bloqueada.
4. **Limite de seguimento** (só no chão e com `is_wall != NULL`). O deslocamento é encurtado até o solo
   sob a coluna **central**, depois do movimento, ficar no máximo `max_climb_step` acima dos pés. Coluna
   sem solo é aceita: o ator simplesmente anda para fora da borda.
5. **Efetivar.** Se algum limite cortou o movimento, `vx = 0` e `rem_x = 0`. Depois `x += permitido`.

### 3.3 Impulso de salto

Se `jump_edge && on_ground`: `vy = JUMP_IMPULSE`, `on_ground = false`, `jumping = true`. Um
`jump_edge` no ar não faz nada. O estágio 2 já rodou com a tolerância do chão, então o frame do salto
ainda anda como quem está no chão.

### 3.4 Solo, rampa e gravidade

`sidescrl_slope_step` com `max_step_up = max_step_down = max_climb_step`. Ele roda a gravidade (`vy +=
GRAVITY_ACCEL`, limitada a `MAX_FALL_SPEED`), integra `y`, amostra o solo sob o centro e decide:

| Situação | Regra |
|---|---|
| Subindo (`vy < 0`) | no ar; o terreno é ignorado |
| Estava no chão | janela de contato `[-max_step_up, +max_step_down]`: dentro dela o ator cola na superfície; fora, perde o contato |
| Estava no ar, caindo | pousa só ao alcançar a superfície: janela `[-max(up, FLOOR_SNAP_PX), 0]`, nunca adere ao chão abaixo |

Com contato: `on_ground = true`, `vy = 0`, `rem_y = 0`, `jumping = false`.

`sidescrl_slope_step` também integra `x` com `vx`. Como o estágio 2 já moveu `x`, o `vx` é segurado em 0
durante esta chamada e restaurado depois. Sem isso o ator andaria duas vezes por frame e atravessaria
paredes.

### 3.5 Caixa (sem código)

Não existe `actor->box` para sincronizar. A caixa é função pura de `(x, y, pose)`:

```c
BoundingBox box = sidescrl_actor_box(&hero);   /* depois do passo; use para desenhar e para hit tests */
```

O "quinto estágio" é esta regra: **nunca guarde a caixa entre frames**, derive-a depois do passo. A
pose não muda dentro do passo; trocá-la é com `sidescrl_actor_set_pose`, que exige espaço livre para
crescer e nunca move os pés.

## 4. Decisões de composição

Cada linha é uma interação entre módulos que a composição literal resolveria errado.

| Problema | Causa | Solução em `sidescrl_motion.c` |
|---|---|---|
| Rampa de 45° vira parede | `slope` põe os pés no solo do **centro**; `wall` mede obstáculos a partir dos pés na borda dianteira, que já está ~10 px acima | `R` na tolerância (`lead_rise`) |
| Com `R` sem teto a parede some | `base + R >= 42` deixa o `wall_clamp` sem linhas bloqueantes | teto: `R <= altura - 1 - base` |
| Rampa íngreme faz o ator atravessar o chão | o `wall` aprova colunas que o `slope` não consegue seguir (subida maior que a janela de 6 px) e o ator perde contato dentro do sólido | `follow_dx`: limita o movimento pela subida do solo sob o centro |
| Dupla integração de `x` | `slope_step` move `x` com `vx` | `vx` segurado em 0 no estágio 4 |
| `on_ground` piscando em repouso | gravidade sub-pixel não penetra o chão na maioria dos frames | sonda de apoio de 1 px em `sidescrl_physics_step` |
| Tunelamento em queda rápida | queda de até 8 px/frame | `FLOOR_SNAP_PX` derivado da velocidade terminal (= 9) |
| `max_step_down` não é parâmetro | assinatura fixada pela especificação | igual a `max_climb_step` |

## 5. Custo por frame

Com os padrões, no pior caso: `wall_clamp` consulta até `|dx| * (altura - base)` pixels (3 x 36 = 108),
`follow_dx` até `|dx|` colunas, `lead_rise` 1, `slope_step` 1. Sem alocação e sem estado global.

## 6. Envelope medido

Medido com as constantes padrão e `max_climb_step = 6`, num mapa de alturas único alimentando os dois
callbacks. As linhas marcadas com ✔ são asseridas por `tests/test_sidescrl_motion.c`; as demais foram
medidas à parte e **não** estão fixadas pelos testes.

**Caminhada.** 3 px/frame de velocidade máxima, aceleração e atrito de 1 px/frame². A partir do
repouso chega a 3 px/frame em 3 frames (andando 6 px) e para em 3 frames (mais 3 px). ✔

**Salto parado ou correndo.** Impulso -6 px/frame. Apogeu de 91 px no frame 29, 62 frames até pousar;
correndo a 3 px/frame, 186 px de alcance. Só se assere que o apogeu passa de 60 px, que o salto limpa
um bloco de 30 px e que o pouso é estável (sem quique, `vx` mantido). ✔ parcial

**Degraus.** Altura de 1 a 6 px é subida; 7 ou mais é parede, nos dois sentidos. ✔ No ar não há
tolerância: uma saliência de 5 px, que quem caminha sobe, para quem ainda está baixo no ar. ✔

**Rampas subindo** (linhas por coluna). ✔ aqui quer dizer que o teste assere o resultado final e o
contato com a superfície a cada frame; as velocidades citadas foram só medidas.

| Inclinação | Comportamento |
|---|---|
| 1 (45°) | velocidade cheia, sem frame no ar, nos dois sentidos ✔ |
| 2 | velocidade cheia, sem frame no ar (medido para a direita) |
| 3 | sobe, sem perder contato, nos dois sentidos ✔; a no máximo 2 px/frame (medido) |
| 4 | para a direita sobe a no máximo 1 px/frame (medido); para a esquerda é parede (medido). Os testes só exigem os invariantes (nunca afunda, nunca atravessa) ✔ |
| 5 ou mais | parede, nos dois sentidos ✔ |

A assimetria do 4 vem de `sidescrl_slope_column` usar `x + w/2`: com 20 colunas o centro fica a 9
colunas da borda direita e a 10 da esquerda, então `R` é 36 para a direita e 40 para a esquerda.
O limite de velocidade cheia é `max_climb_step` dividido pelos pixels por frame (6 / 3 = 2).

**Rampas descendo.** A janela de contato para baixo é `max_climb_step` por frame. Até 2 linhas por
coluna o ator cola no chão. Mais íngreme que isso é tratado como queda: fica no ar, mantém `vx` e
pousa (medido: 57 frames no ar para 3 linhas por coluna). Só a descida de 45° está asserida, nos dois
sentidos ✔; as descidas de 2 linhas por coluna e as mais íngremes foram só medidas.

**Penhasco no topo de rampa.** Continua sendo parede, mesmo com a borda dianteira já alta na rampa. ✔
Asserido para a rampa de 45° (só para a direita) e para a de 3 linhas por coluna (nos dois sentidos).

**Abismo.** Coluna sem solo sob o centro não bloqueia: o ator sai da borda e cai. ✔

## 7. Constantes

Todas com `#ifndef`, sobrescrevíveis com `-D`.

| Constante | Padrão | Observação |
|---|---|---|
| `SIDESCRL_PHYS_UNITS_PER_PIXEL` | 2341 | mesma escala de `sidescrl_phase.c` |
| `SIDESCRL_PHYS_GRAVITY_ACCEL` | 450 | ~0,19 px/frame² |
| `SIDESCRL_PHYS_MAX_FALL_SPEED` | 8 px/frame | |
| `SIDESCRL_PHYS_FLOOR_SNAP_PX` | 9 | derivado, não definido à mão |
| `SIDESCRL_MOTION_WALK_MAX` | 3 px/frame | placeholder |
| `SIDESCRL_MOTION_WALK_ACCEL` | 1 px/frame² | placeholder |
| `SIDESCRL_MOTION_FRICTION` | 1 px/frame² | placeholder |
| `SIDESCRL_MOTION_JUMP_IMPULSE` | -6 px/frame | placeholder |

`sidescrl_phase.c` tem a própria física (`GRAVITY_Y = 3 * UNITS`, `JUMP_IMPULSE = -48 * UNITS`,
`WALK_STEP = 6 * UNITS`, em unidades por frame), com valores bem diferentes dos placeholders acima.
`sidescrl_motion` **não está ligado a ele**; qualquer integração exige decidir qual conjunto de
constantes vale.

## 8. Invariantes

Verificados a cada frame, durante 6000 frames de entradas aleatórias em terreno aleatório (semente
fixa), e comparando dois atores idênticos para determinismo: ✔

- mesmo estado e mesmas entradas dão o mesmo estado;
- `0 <= rem_x, rem_y < 2341`;
- `vy <= MAX_FALL_SPEED` e `|vx| <= WALK_MAX`;
- `|Δx| <= WALK_MAX / U + 1` por frame;
- `on_ground` implica `vy == 0`, `jumping == false` e pés exatamente no solo sob o centro;
- `facing_left` acompanha `walk_dir` quando ele é diferente de zero.

## 9. Limites conhecidos

- **`is_wall == NULL`** desliga o `wall_clamp` **e** o limite de seguimento: o ator pode andar para
  dentro de uma face e afundar nela. Passe sempre um `is_wall` real.
- **No ar a tolerância é 0:** um salto que cai de lado contra uma rampa perde a velocidade horizontal
  na primeira coluna que sobe.
- **Velocidade acima de 1024 px/frame** conta como colisão com parede (o `wall_clamp` é limitado por
  `SIDESCRL_WALL_MAX_SCAN`). Não ocorre com as constantes de caminhada.
- **Mapa e solo:** o solo é amostrado só na coluna central e as paredes por pixel; os callbacks devem
  descrever o mesmo terreno.
- **Constantes não originais** (seção 7). A integração com `sidescrl_phase.c` está na seção 11.

## 10. Verificação e histórico de revisão

Esta revisão final mediu o módulo contra a especificação e encontrou um defeito introduzido pela
própria composição:

- **Defeito.** Em rampas de 6 linhas por coluna o ator atravessava o chão e caía para sempre
  (`y = 1219` e subindo). Causa: `R` sem teto fazia `base + R` chegar a 42, o `wall_clamp` ficava sem
  linhas bloqueantes e deixava o ator andar 3 colunas de uma vez, e o centro subia 18 px, fora da janela
  de 6 px do `slope_step`. Rampas de 3 e 4 linhas por coluna tinham 1 e 25 frames no ar, em vez de
  cola contínua.
- **Correção.** Teto em `R` e `follow_dx` (seção 3.2). Testes novos: `test_steep` (rampas de 3 a 8
  linhas por coluna nos dois sentidos, e rampa com penhasco) e `test_abyss`.
- **Mutações.** Dez mutações plantadas em cópias do módulo contra o teste: oito pegas. Duas
  sobreviveram e são equivalentes: o teste explícito de `NO_GROUND` em `follow_dx` é redundante
  (`INT32_MAX` já passa na comparação) e aplicar o limite no ar não muda nada, porque o `wall_clamp` com
  tolerância 0 já garante solo igual ou abaixo dos pés sob as colunas da caixa.

### 10.1 Estado dos testes

Medido depois da correção, na hora da escrita:

| Verificação | Resultado |
|---|---|
| `clang -std=c99 -pedantic -Wall -Wextra -Werror -Wconversion -Wshadow` em `sidescrl_motion.c` | limpo |
| Maior linha de `sidescrl_motion.h/.c` e do teste | 89 / 94 / 97 (limite 105) |
| `test_sidescrl_motion`: grupos | walk, walls, ramps, steep, abyss, jump, degenerate, fuzz |
| CTest completo, build dinâmico (`build`) | 52/52, incluindo `test_full_campaign`, `test_fmv_all` e `test_movie_audio` |
| CTest dos testes de sidescroll, build estático `/MT` (`build-static`), recompilado | 8/8 |

O CTest completo no build estático foi rodado depois da integração em `sidescrl_phase` (seção 11.5).

## 11. Integração em `sidescrl_phase`

`sidescrl_phase_step` chama `sidescrl_motion_step` uma vez por frame. Isso substitui a lógica anterior:
velocidade constante de 6 px/frame, salto de 48 px/frame com gravidade de 3 px/frame² (a subida só parava
no topo da tela) e parede testada em uma única coluna.

### 11.1 O que a fase fornece

| Entrada do `sidescrl_motion_step` | Origem na fase |
|---|---|
| `walk_dir` | direita (`0x8000`) vale +1 e tem prioridade, esquerda (`0x4000`) vale -1 |
| `jump_edge` | botão de salto (`0x0100`) pressionado neste frame |
| `max_climb_step` | `MAX_CLIMB_STEP = 6` |
| `get_ground(col)` | `floor_py[col]` do `scrl_map` |
| `is_wall(col, row)` | `row >= floor_py[col]`; fora do mundo é parede |

- **Ator.** `phase->actor` é a pose de verdade. `x` é a coluna central menos 10 e `y` é o pé menos 42.
  `bones_x`, `bones_y`, `vel_x`, `vel_y`, `airborne`, `facing` e `ground_y` são espelhos recalculados depois
  de cada passo, então inimigos, render e `stage_runner` leem o que sempre leram. `bones_x` é a coluna
  central e `bones_y` os pés, em unidades, com o resto sub-pixel somado.
- **Bordas.** Colunas menores que `EDGE_MARGIN - 10` (`-6`) e maiores ou iguais a `scroll_w` são parede. O
  centro do ator fica em `[4, scroll_w - 10]`, o que mantém a margem de 4 colunas de antes à esquerda.
- **Teto.** `y < 0` é cortado para 0, como o `ny < 0` antigo.
- **Sons.** `JUMP` no frame em que o salto começa (`was_on_ground`, agora no ar, `jumping`). `LAND` em toda
  transição ar -> chão, o que inclui cair de um degrau fundo.
- **Estado.** `AIR` fora do chão, `WALK` com `vx != 0`, senão `STAND`. Encostar numa parede zera `vx` e
  mostra `STAND`, como antes.
- **`sidescrl_phase_spawn(phase, coluna)`** põe o ator parado no chão sob a coluna. A carga usa a coluna 40.

### 11.2 O que mudou no comportamento

| | Antes | Agora |
|---|---|---|
| Caminhada | 6 px/frame constante | acelera 1 px/frame² até 3 px/frame; para por atrito |
| Salto | sobe até o teto da tela | sobe 91 px e fica 61 frames no ar (medido, chão plano); sem salto duplo |
| Parede | uma coluna, só com `06AMAP` | pixel a pixel: degrau até 6 linhas sobe, face maior bloqueia |
| Rampa | o pé copia o chão a cada coluna | seguimento com janela de contato; 3 linhas/coluna sobe a no máximo 2 px/frame |
| Degrau fundo | não existe | vira queda com gravidade, com som `LAND` |

O salto e a caminhada usam as constantes *placeholder* da seção 7. A fase não define velocidades próprias.

### 11.3 Chão estimado

Das dez fases de sidescroll, cinco têm `06AMAP` (VALLEY, BAT1, BAT2, BAT3, VORTEX) e cinco não (MAUSO,
GLASS, VILLAGE, SHADOW, LAFF). Para as cinco sem ele `scrl_map` estima o chão pela densidade de bytes do
TIL, e esses bytes são fluxos comprimidos: o resultado é ruído. Medido no
MAUSO antes do limite: das 1176 colunas, 217 tinham salto de 5 linhas ou mais entre vizinhas (máximo 34). O
código antigo escondia isso porque só copiava a altura. Com a física unificada o ator caiu num poço em
`x = 274`, não conseguiu sair, e os poofs gastaram o esqueleto (`lost`, 31 contatos).

`scrl_map_build_collision` agora limita a inclinação de um chão **estimado** a `SCRL_FLOOR_MAX_SLOPE = 2`
linhas por coluna, que é o limite de velocidade plena da seção 6. O chão **autoral** (`06AMAP`) não é
alterado. É um *stand-in*: torna o chão andável, não o torna correto. Só o dado real de tile e colisão
resolve isso.

### 11.4 Limites da integração

- `MAX_CLIMB_STEP` e `EDGE_MARGIN` são constantes da fase, sem origem no binário.
- `scrl_map_get_hixel` e `scrl_map_wall_at` seguem públicos e testados, mas a fase não os usa mais.
- Os testes com disco só seguram a direita: o salto é coberto apenas pelo terreno sintético. Nenhum teste
  exige pular para passar de um obstáculo das fases reais.

### 11.5 Verificação

| Verificação | Resultado |
|---|---|
| `test_sidescrl_phase`, parte sem disco (terreno sintético) | andar, rampas de 1, 2 e 3 linhas, rampa de 5 como parede, parede de 80 linhas, bordas, salto, queda |
| `test_scrl_map`, parte sem disco | chão estimado com `|Δ| <= 2`; chão autoral com penhasco de 100 linhas intacto |
| Mutações em `sidescrl_phase.c` | 8 de 8 pegas (subida, margem, som de pouso, borda direita, paredes, som de salto, estado, espelho) |
| Mutações no limitador de `scrl_map.c` | 5 de 5 pegas; uma sexta (o passe de volta) é código morto e foi removida |
| Fases de sidescroll, segurando direita | as 10 terminam em `FINISHED`: MAUSO 181 quadros e 6 contatos, VALLEY 184 e 5, BAT1 211 e 4, BAT2 181 e 5, BAT3 223 e 5, GLASS 181 e 6, VORTEX 181 e 5, VILLAGE 181 e 6, SHADOW 181 e 5, LAFF 181 e 5 |
| CTest completo, build estático `/MT` com os discos | 52/52 |

Os 181 quadros são o chão plano: 540 px a 3 px/frame, mais a aceleração. BAT1 e BAT3 demoram mais e são
as que têm mais colunas com inclinação acima de 2 linhas (84 e 96, contra 10 na BAT2 e 0 ou 4 nas demais).
