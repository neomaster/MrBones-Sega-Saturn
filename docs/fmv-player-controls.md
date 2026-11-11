# Controles do player DUCK

O `/19DUCK` analisado tem 134.744 bytes, SHA-256
`21d0bd94b181f834982ba9f0378839e6389f5700584615dc8245c90ce398b999` e é carregado em
`0x06010000`. O mesmo hash foi encontrado nos recursos de MENU, VALLEY, FINALE, LOSE e INTRO
dos discos correspondentes. A listagem e a descompilação estão em
[`ghidra-duck-player.txt`](ghidra-duck-player.txt); o script é
[`DumpDuckPlayer.java`](../tools/DumpDuckPlayer.java). A base lógica indicada por Ghidra é zero,
mas o bloco importado começa em `0x06010000`, conforme os literais e o mapa DUKMOVIE.

Em `playLoop` (`0x06010f50`), o literal `0x060110e4` aponta para `0x0022fb08`; o loader `/02`
usa esse endereço como primeiro estado de controle em `SMPCgetPad` (`0x0020bf08`). Esse estado é a
palavra dos botões atualmente mantidos, invertida a partir dos dados ativos em zero. O player lê
seu byte alto em `0x0601101e` e testa o bit `0x08` em `0x0601102a`: quando ativo, o desvio em
`0x0601102e` sai para a limpeza do player em `0x06011042`. Esse bit representa `0x0800`, Start.
O player também tem uma condição distinta quando os quatro bits inferiores do byte estão ativos;
o comportamento completo dessa combinação não foi portado por esta alteração.

No Ymir 0.4.1, após selecionar o filme de FINALE pelo depurador, a execução parou em
`0x0601101c`, já com as instruções do player carregadas. Alterar o byte `0x0022fb08` de zero para
`0x08` levou ao breakpoint `0x06011042`, com `R0=R1=8`; ao continuar, o player terminou e o menu
START apareceu. Foi uma simulação do estado do controle em RAM, não uma pressão física comprovada.

O port trata Start mantido durante as apresentações que passam por `special_phase`, encerrando
também os filmes de LOSE e FINALE. O teste de integração nativo com FINALE registrou o corte do
PCM e o início posterior da música do menu em `build-static/native-finale-start-verified/`.
O host aplica o mesmo critério aos filmes de título e história do menu: A, B e C não os
interrompem, enquanto Start mantido os encerra. As demais telas do menu mantêm seus controles.
Quando Start coincide com o término da abertura, o host processa o salto antes de avançar o
relógio do filme, para a mesma entrada não selecionar também a opção START do menu. O teste
`test_fmv_transition` cobre essa coincidência e o término natural sem entrada.

Uma segunda execução no Ymir selecionou o filme de FINALE no carregador e simulou Start no ponto
de leitura do controle, novamente por alteração em RAM. Após retomar o depurador, a captura de
áudio registrou uma cauda de 60 ms correspondente ao PCM do filme (correlação 0,999969) e depois a
faixa de menu. O intervalo medido entre o fim da cauda e o início de referência da faixa foi de
4,689 s; na captura nativa anterior foi de 4,648 s, diferença de 41 ms. Os arquivos e o cálculo
estão em `build-static/ymir-finale-skip-audio/`. A comparação utiliza uma execução com pausa e
seleção forçadas e não estabelece o comportamento de uma vitória jogada ou de um controle físico.
