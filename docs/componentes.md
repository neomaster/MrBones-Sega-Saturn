# Componentes: decisão, interface e dependência de hardware

Estado de cada linha: **decidido** (aprovado no plano), **a validar** (precisa de evidência de execução),
**implementado** (código + teste). Nada abaixo de "implementado" conta como pronto.

| Componente | Decisão | Interface | Depende de hardware | Estado |
|---|---|---|---|---|
| Identidade de versão | manifesto de hashes | `identity_check(manifest, cd, disc, mode, result)` | — | **implementado** (`src/core/identity.c`, testes sintético e discos reais) |
| Leitura de disco | biblioteca com 3 visões | A: `cd_iso_*` (arquivos); B: `cd_track_*`/`cd_read_audio` (faixas); C: `cd_read_raw/user` (setores por LBA) | leitura por setor, FAD = LBA + 150 | **implementado** |
| Configuração | ini + env + CLI | `config_load` | — | **implementado** |
| Código do jogo | reconstruir e compilar nativamente; sem interpretador SH-2 | a definir por função reconstruída | concorrência, memória e temporização a identificar | análise parcial, não iniciado |
| SCU (DMA, timers, interrupções) | reimplementar | registradores `0x25FE0000` | temporização de DMA | decidido, não iniciado |
| CD block (`GFS_*`, `CDC_*`) | HLE por símbolo do mapa; fallback: registradores do CD block | `cdimage` (visão C) | tempos de seek e de transferência | decidido; a validar no log de acessos |
| VDP1 | lista de comandos → framebuffer por software | VRAM `0x25C00000` | ordem, transparência | decidido, não iniciado |
| VDP2 | só os planos usados | VRAM `0x25E00000` | prioridades, paletas | decidido; planos usados a medir |
| SCSP + 68000 | mínimo na prova; executar driver 68K original depois, se usado | RAM de som | clock próprio do 68K | decidido; uso a medir (`SND_Init` existe no `/02`) |
| SMPC/pad/backup | reimplementar | registradores SMPC | tempos de resposta | parcial: `saturn_pad` + `host_input`; `nvm_shadow` arquivo (`save_dir`); SMPC HW não |
| Fase 03MENU nativa | Title/Start/Options/Levels | `menu_phase_*` / `mrbones` | — | **parcial**; transição → RUNWAY; ver `coverage.md` |
| Executável integrado | MENU→fase | `mrbones.exe` | — | §13; só RUNWAY jogável |
| Composição diagnóstica RUNWAY | blit RGBA de fundo+sprites | `runway_compose_*` | — | implementado (diagnóstico); VDP1/VDP2 real não iniciado |
| Agendador cooperativo | fibras Windows ≈ ObjYeild | `mb_obj_yield` / `mb_scheduler_*` | — | implementado (adaptação); ring SH-2 exacta não reproduzida |
| Joypad brain (1 passo) | cópia pad→BonesInput | `mb_joypad_brain_step` | — | implementado; loop infinito original vira yield do chamador |
| Fase RUNWAY nativa | loop stand/walk/finish | `runway_phase_*` / `mbrunway` | — | **parcial jogável** (reconstrução); ver `coverage.md` |
| Áudio CD-DA host | waveOut faixa do cue | `host_audio_start_cdda` | — | BGM RUNWAY; SCSP não |
| Áudio SFX PCM8 | one-shot waveOut | `host_audio_play_pcm8` + `snd1_pcm_*` | — | span4/s05 experimental; não SCSP |
| Mapa colisão 40×112 hixel | GetHixel + push-out + wall-above | `pan_collision_*` / `pan_bones_*` | — | §12k/§12l; HitChk bnscfg (±5,−8)/(±5,+12) |
| Bancos STXTR Bones + flip-X | morph×13 + full×20; H-flip facing | `runway_phase` / `blit_sprite_ex` | — | §12l |
| TallSet / BrainSwitch | crouch8 → tall; enqueue `0xc` | `bones_tall_*` / `brain_switch` | — | §12m |
| 15PAN multi-frame + Mplaynext | seek visual; cue dur=8 | `pan_scroll` / `mplay_seq` | — | §12m; coll=frame0 |
| libc/libgcc do jogo | substituir por equivalentes nativos após validação | — | — | não iniciado |

O leitor nativo `resource_load` é uma adaptação do papel de leitura síncrona de
`CFileLoader::LoadFile` em `0x002033e8`; ainda não reproduz todas as chamadas,
estados ou comportamento assíncrono do original.

## Lista de recursos de RUNWAY

A lista de arquivos e setores que a cena realmente lê **não está fixada**: vem do log de acessos do CD block
no passo 5. Candidatos por nome (hipótese, não prova): `/02`, `/03MENU/*`, `/04RUNWAY/{02TITL,03CODE,04STXT,05SND1,15PAN,19DUCK}`.

## Faixas CDDA

Nas pastas atuais, `mbverify` conta 0 de 9 faixas de áudio presentes no disco 1 e 0 de 11 no disco 2
(arquivos `Track 02+` ausentes). O cue as declara. Se o jogo depende de CDDA, é preciso extrair os `.bin` dos `.7z`
(não abertos) ou aceitar silêncio nessas faixas na prova.
