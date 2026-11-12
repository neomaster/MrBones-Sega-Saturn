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
- Estado inicial do host: **16 slots acesos**. Cada disparo (A/B/C + D-pad)
  chama `special_phase_finale_shoot`; ao desactivar o último, `ring_cleared`.
- Encadeamento de vitória (Saturno `0x28ce04(0)` → `CEndLevel` →
  `PlayDuckMovie(3)`):
  - **Headless/teste**: `movie_ok` no load → o filme corre de imediato
    (hashes Ymir). Clear do anel durante o filme não o termina.
  - **Arena jogável**: `special_phase_finale_defer_ending` (host interactivo)
    mantém `finale_movie_armed` com `movie_ok=0`. Ao zerar os 16 slots,
    `special_phase_finale_play_ending` reabre `/27FINALE/20AVI` e só depois
    o fim do filme entrega `SPECIAL_ST_FINISHED` → `STAGE_ROUTE_COMPLETE`.
  - Áudio (`mrbones`): com filme deferido, `restart_stage_audio` mantém a
    faixa CD-DA da arena (`movie_ok` false). Em `maybe_start_movie_audio`,
    `host_audio_stop(g_audio)` corre antes de `g_movie_audio` quando
    `play_ending` arma `movie_pcm_pending`. O `mrbones --check`
    (`test_fmv_transition`) entra em FINALE com o filme deferido, exige CD-DA
    ativo e nenhum PCM durante a arena, zera os 16 slots e exige que o PCM do
    filme substitua o CD-DA; o caso falha se `restart_stage_audio` ignorar
    `movie_ok` ou se o CD-DA não for parado.
  - Sem AVI armado: clear → `FINISHED` directo (fallback).
  - Validação interativa real (LOG): jogar FINALE até zerar os 16 slots →
    `/27FINALE/20AVI` → `MENU_SCR_START`; capturar vitória jogada no Ymir
    (até agora só vitória forçada via RAM).
- HUD do anel só quando `!movie_ok` (blit de filme limpo).
- **Sprites STXTR do anel** (`/27FINALE/04STXT`):
  - Descritores de animação em `03CODE`: hit `@0x0028b60c` → nós
    `0x28b514/54c/584/5bc/5f4`; activate `@0x0028b648` (ordem inversa).
  - CG VRAM `0x25C29AA0…0x25C2CF60` → offsets STXT
    `{0x21AA0,0x227D0,0x23500,0x24230,0x24F60}`; cada quadro **112×30**
    (3360 B, 8 bpp). Idle = frame 0; flash de acerto percorre 1…4.
  - Slot inactivo não é desenhado (Brain desliga da draw-list).
  - Posição: centróide dos 4 `{dx,dy}` do registo
    `RingDefinitionTable` + centro do ecrã.
- **SFX por disparo** (`!movie_ok`):
  - Chamada Saturn: `SoundEvent(id)` `@0x00204128`.
  - **acerto** `@0x002b21f0` = `{0x2A,0x2A,0x2B,0x29}` por dir → registo
    tipo 3 com `word[4]` = sample span4 `{12,12,13,11}` (68 B cada).
  - **activação** `@0x002b2200` = `{0x32,0x32,0x33,0x31}` → `word[4]` =
    `{0x14,0x14,0x15,0x13}` (indices ≥19 fora de span4 count=19); host
    usa sample 15 (516 B) via `runway_sfx_event_pcm`.
  - Flags legado: `FINALE_SFX_HIT`=`JUMP|C`, `FINALE_SFX_ACTIVATE`=`C`.
  - `phase->ring_sfx_event` + `mrbones` despacha o PCM do evento.

Ainda aberto: estado inicial confirmado no Ymir; mapeamento SCSP dos
indices 0x13–0x15 de activação além do fallback span4[15]; vitória
jogada ponta a ponta.
