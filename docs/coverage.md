# Cobertura

Estados: **não testado**, **parcial**, **validado com referência**. Compilar não marca nada como concluído.
Referência RUNWAY-001: ainda não criada (nenhum emulador instalado).

| Fase | Inicialização | Jogabilidade | Imagem | Áudio | Transição | Save | Diferenças conhecidas | Evidência |
|---|---|---|---|---|---|---|---|---|
| (infra) identidade de disco | validado | n/a | n/a | n/a | n/a | n/a | manifesto vem dos mesmos dumps, não de redump | `tests/test_cdimage.c`; `mbverify --full` nos dois discos |
| (infra) leitura ISO/faixas/setores | validado (sintético + discos reais) | n/a | n/a | n/a | n/a | n/a | CDDA: faixas 02+ ausentes na pasta | `tests/test_cdimage.c` |
| MENU | não testado | não testado | não testado | não testado | não testado | não testado | — | — |
| RUNWAY | não testado | não testado | não testado | não testado | não testado | não testado | — | — |
| demais 24 fases, INTRO, LOSE, troca de disco | não testado | não testado | não testado | não testado | não testado | não testado | TROLL1/2 sem dados reais | — |
