# Componentes: decisão, interface e dependência de hardware

Estado de cada linha: **decidido** (aprovado no plano), **a validar** (precisa de evidência de execução),
**implementado** (código + teste). Nada abaixo de "implementado" conta como pronto.

| Componente | Decisão | Interface | Depende de hardware | Estado |
|---|---|---|---|---|
| Identidade de versão | manifesto de hashes | `identity_check(manifest, cd, disc, mode, result)` | — | **implementado** (`src/core/identity.c`, testes sintético e discos reais) |
| Leitura de disco | biblioteca com 3 visões | A: `cd_iso_*` (arquivos); B: `cd_track_*`/`cd_read_audio` (faixas); C: `cd_read_raw/user` (setores por LBA) | leitura por setor, FAD = LBA + 150 | **implementado** |
| Configuração | ini + env + CLI | `config_load` | — | **implementado** |
| SH-2 master/slave | interpretar; traduzir folhas depois | `bus_read/write`, `sh2_run(cycles)` | delay slot, alias não-cacheado `0x2xxxxxxx`, interrupções, cache | decidido, não iniciado |
| SCU (DMA, timers, interrupções) | reimplementar | registradores `0x25FE0000` | temporização de DMA | decidido, não iniciado |
| CD block (`GFS_*`, `CDC_*`) | HLE por símbolo do mapa; fallback: registradores do CD block | `cdimage` (visão C) | tempos de seek e de transferência | decidido; a validar no log de acessos |
| VDP1 | lista de comandos → framebuffer por software | VRAM `0x25C00000` | ordem, transparência | decidido, não iniciado |
| VDP2 | só os planos usados | VRAM `0x25E00000` | prioridades, paletas | decidido; planos usados a medir |
| SCSP + 68000 | mínimo na prova; executar driver 68K original depois, se usado | RAM de som | clock próprio do 68K | decidido; uso a medir (`SND_Init` existe no `/02`) |
| SMPC/pad/backup | reimplementar | registradores SMPC | tempos de resposta | decidido, não iniciado |
| libc/libgcc do jogo | executar o original | — | — | decidido |

## Lista de recursos de RUNWAY

A lista de arquivos e setores que a cena realmente lê **não está fixada**: vem do log de acessos do CD block
no passo 5. Candidatos por nome (hipótese, não prova): `/02`, `/03MENU/*`, `/04RUNWAY/{02TITL,03CODE,04STXT,05SND1,15PAN,19DUCK}`.

## Faixas CDDA

Nas pastas atuais, `mbverify` conta 0 de 9 faixas de áudio presentes no disco 1 e 0 de 11 no disco 2
(arquivos `Track 02+` ausentes). O cue as declara. Se o jogo depende de CDDA, é preciso extrair os `.bin` dos `.7z`
(não abertos) ou aceitar silêncio nessas faixas na prova.
