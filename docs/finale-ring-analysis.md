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
  chama `special_phase_finale_shoot`; ao desativar o último, `ring_cleared`
  e `SPECIAL_ST_FINISHED` (caminho `EndLevel(0)` → filme).
- O AVI de `/27FINALE/20AVI` continua a poder encerrar a fase sozinho
  (compatível com `test_special_phase` / hashes Ymir). HUD do anel só é
  desenhado quando `!movie_ok` (blit de filme limpo).

Ainda aberto: estado inicial confirmado no Ymir, áudio/animação por ramo de
`ShootRing`, sprites STXTR byte-fiel do anel e vitória jogada ponta a ponta.
