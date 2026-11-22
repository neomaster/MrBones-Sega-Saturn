Mr. Bones (Protagonista) — Sprites Extraidos e Suporte HD (3x+)
===================================================================

Estrutura das pastas:
1. orig_1x\ (29 PNGs)
   Sprites originais do Saturn (1x) atualmente usados pelo motor do personagem:
   - stand_00.png .. stand_01.png (parado)
   - tall_00.png .. tall_01.png (levantado/esticado)
   - air_00.png .. air_03.png (pulo/ar)
   - walk_00.png .. walk_19.png (ciclo de corrida/caminhada)
   - hud_skull.png (caveirinha do HUD CSkelBar)

2. hd_3x\ (63 PNGs — ATIVO NO JOGO com POSES HD + OPONENTES 3D)
   Sprites ativos em alta resolucao (RGBA transparente, escala 3x alinhada pelos pes)
   gerados para o protagonista, projeteis e oponentes caveiras.
   O executavel mrbones.exe (inclusive via Jogar_MrBones.bat) procura automaticamente
   esta pasta ao iniciar e carrega todos os 63 sprites customizados (28 poses + 6 ossos + 29 oponentes).
   - Mapeamento dos sprites ativos na folha:
     * walk_00..walk_19     <- pose_210 .. pose_229
     * stand_00..stand_01   <- pose_216 .. pose_217
     * air_00..air_03       <- pose_262, pose_264, pose_266, pose_268
     * tall_00..tall_01     <- pose_270 .. pose_271
     * thrown_00..thrown_05 <- fileira 11 cols 0..5 (6 ossos giratorios arremessados)
     * gang_00..gang_07     <- pose_282 .. pose_289 em tom ciano espectral lunar (8 oponentes)
     * lord_00..lord_07     <- remaster 3D CGI de /05MAUSO e /27FINALE (8 poses gigantes)
     * valley_00..valley_12 <- remaster 3D CGI de /06VALLEY (13 esqueletos emergindo do solo)

3. hd_3x_xbr_backup\ (28 PNGs)
   Backup dos 28 sprites 3x gerados automaticamente por xBR antes da substituicao.

4. all_poses_hd\ (100 PNGs) e all_poses_1x\ (100 PNGs) + morph_1x\ (13 PNGs)
   Todas as 100 poses completas (pose_210_off01ee40.png .. pose_309_off0552e0.png)
   extraidas com fundo transparente em HD a partir da folha nova, alem dos arquivos
   1x originais e dos 13 quadros de remontagem (morph_00..morph_12) para referencia.

Atalhos e flags em mrbones.exe:
- Resolucao padrao da janela de gameplay: 960x672 (3x), redimensionavel ate tela cheia
- Tecla F5: alterna filtros de upscale ao vivo (xbr3x -> scale3x -> xbr2x -> scale2x -> nearest)
- Tecla F6: liga/desliga o overlay HD dos sprites do Mr. Bones (hd_3x\)
- CLI:
    --upscale xbr3x|scale3x|xbr2x|scale2x|nearest
    --scale 1|2|3|4
    --hd-dir <pasta>
    --no-hd-bones
