# Handoff FINALE — sessão Grok PID 27676

Atualizado: 2026-11-09 (host session `01a1b1d3-90c2-7461-8f1c-4ee355dbd171` = este processo).

## Feito no host

- Arena CRing em `src/core/special_phase.c` + API `special_phase_finale_shoot`
- Spec: `docs/finale-ring-analysis.md`
- Teste: clear 16/16 em `tests/test_special_phase.c`
- Hipótese de init: **16 slots acesos**, count=16 (clear-all → EndLevel(0))

## Pedido ao braço estático/Ymir (subagente)

Confirmar estado inicial real de `objeto+0x708` e flags `entry+0x68` após `CreateCRing`.
Entregar em `docs/finale-ring-init.md` com recomendação keep-16-lit vs 0-lit.

## Não tocar

- Hashes Ymir do AVI em `test_special_phase` (blit limpo enquanto `movie_ok`)
- Módulos protegidos se a árvore marcar algum

## Próximo host (esta sessão)

1. Consumir `finale-ring-init.md` quando existir e ajustar init se necessário
2. Opcional: arena-first (só abre AVI após `ring_cleared`) atrás de flag/`--check` compat
3. Posições HUD a partir de `RingDefinitionTable@0x2b0158` se útil
