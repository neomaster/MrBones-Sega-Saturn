# Cache de tiles VDP2: `scrl_decomp` e `scrl_tilecache`

Como os planos de fundo de sidescroll (`07ACDB`/`08ATIL` até `13DCDB`/`14DTIL`) viram pixels na tela:
o TIL comprimido é decodificado **uma vez**, no `scrl_map_init`, e o desenho lê só o resultado.

**Origem.** O formato do fluxo vem de [`ghidra-vdp2-tiles.txt`](ghidra-vdp2-tiles.txt) (§3.3) e do
decodificador que o projeto já tinha. O layout do registro de bloco vem do `decode_menu_plane` de
`menu_phase.c`, que já é validado com os discos nos planos do MENU. **Fonte de verdade:** os comentários
de [`scrl_decomp.h`](../src/core/scrl_decomp.h) e [`scrl_tilecache.h`](../src/core/scrl_tilecache.h). Se
este documento divergir do código, o código vence.

```
CDB + TIL ──► scrl_tilecache_build ──► cache (blocos 32x32, 1 byte por ponto)
                  │ usa scrl_decomp_byte_ex                       │
                                                                  ▼
                              scrl_map_draw ──► sample_plane ──► tira RGBA
```

## 1. Decodificador (`scrl_decomp`)

Um byte de controle, lido do bit mais alto para o mais baixo, e um item por bit:

| Bit | Item |
|---|---|
| 0 | literal: o próximo byte da origem vai para a saída |
| 1 | código BE16 `dist << 4 \| nibble`. `nibble = 0` termina o fluxo. `nibble 1..15` copia `nibble + 1` bytes (2 a 16) de `dist` bytes atrás, de 1 a 4095, byte a byte (`dist` menor que o tamanho repete o padrão) |

Depois de oito itens vem outro byte de controle. `scrl_decomp_byte_ex` limita toda leitura e escrita por
`src_cap` e `dst_cap` e devolve um relatório: bytes da origem usados, se terminou no terminador, e quantos
*matches* o formato não permite (distância 0, ou alcançando antes do início da saída). Esses matches são
decodificados como sempre foram (distância 0 vale 1, bytes antes do início valem 0), então `menu_phase` e
os testes com disco veem a mesma saída. `scrl_decomp_byte` continua existindo como invólucro.

## 2. Layout lido pelo cache

- **CDB.** Cabeçalho de 10 bytes BE16 (`pix_w`, `pix_h`, `a`, `b`, `n`), depois caracteres 8x8 de 64 bytes
  (um byte por ponto), endereçados por deslocamento em bytes.
- **TIL.** Registros em sequência que cobrem o arquivo exatamente:

| Offset | Campo |
|---:|---|
| +0 | `size` BE16, bytes do registro com o cabeçalho (>= 72) |
| +2 | `count` BE16, caracteres do registro, 0 a 16 |
| +4 | `id` BE16, número do bloco na grade, linha a linha |
| +8 | 16 descritores BE32, uma grade 4x4 de células 8x8 |
| +72 | `count * 64` bytes de caracteres, comprimidos |

- **Descritor.** Os 16 bits baixos vezes 32 são um deslocamento em bytes: no payload do CDB se o bit
  `0x08000000` está ligado, senão nos caracteres do próprio registro. Nenhum outro bit é interpretado
  (paleta, espelhamento); eles são juntados em `ignored_bits`.
- **Grade.** `ceil(largura / 32) x ceil(altura / 32)` blocos. Um plano de 1176x608 dá 37 x 19 = **703**.
  Bloco sem registro é *ausente* (transparente). Bloco *vazio* é o que tem todos os pontos em 0.

## 3. Política

Dois tipos de problema, tratados de formas diferentes.

| Problema | O que acontece |
|---|---|
| **Estrutura:** registro truncado, `size` fora do arquivo, `count > 16`, `id` fora da grade, bloco repetido, sobra que não é um registro, grade acima de 65536 blocos | o `build` falha e **o plano não desenha nada** |
| **Conteúdo:** os caracteres de um registro não descomprimem em exatamente `count * 64` bytes, ou usam um *match* proibido | só esse bloco é descartado (fica transparente) e entra em `lz_failed_blocks`; os números do primeiro ficam em `fail` |
| **Célula:** descritor aponta para fora da sua origem | a célula fica 0 (transparente) e entra em `bad_cells` |

É a estrutura que garante que um arquivo em outro formato nunca vira ruído: ladrilhar o arquivo inteiro
com tamanhos, ids e contagens coerentes é improvável por acaso. Ponto 0, bloco ausente e ponto fora do
plano são transparentes no `sample_plane`; um plano sem cache é pulado inteiro, o que também evita
varrer pixels à toa em cada quadro. O limite de 65536 blocos é o que `scrl_cdb_parse` aceita (16384 x 4096
px, 64 MB no pior caso). O cache de um plano ocupa `blocos * 1 KB`.

## 4. Verificação

| Verificação | Resultado |
|---|---|
| `test_scrl_decomp` (sem disco) | vetor de referência, os 15 nibbles, ordem dos bits, terminador, destino cheio, 150 ida e volta aleatórias, limite de 4095, truncamento em todo ponto, matches proibidos |
| `test_scrl_tilecache` (sem disco) | layout, borda do plano, registro de 16 caracteres, todas as rejeições, blocos descartados, diagnóstico, 8000 *builds* com bytes danificados ou puro ruído |
| `test_scrl_map`, parte sem disco | a tira desenhada, com deslocamento de câmera e volta do plano, e ruído no lugar do TIL |
| Mutações | 34 de 34 pegas: 9 no decodificador, 25 no cache |
| CTest, cada commit em *checkout* limpo, com os discos | `1aa8a3c` 53/53, `f3c7c58` 54/54 |

Os fixtures vêm de um codificador só de teste (`tests/lz_test_enc.h`) e de caracteres inventados. **Isso
mostra que o código é coerente com o formato descrito, não que ele reproduz a rotina original sobre dados do
jogo.** Nenhum teste compara a saída com o original.

## 5. O que se viu nos planos reais

Contagens impressas pelo `test_scrl_map` com disco (só números):

| Plano | Resultado |
|---|---|
| MAUSO `09BCDB`, `11CCDB`, `13DCDB` | completos: 128, 160 e 256 blocos, nenhum descartado, nenhuma célula ruim |
| MAUSO `07ACDB` (1176x608) | estrutura boa: 703 blocos, 357 vazios, 346 comprimidos. **Só 4 dos 346 decodificam**; 342 são descartados. O primeiro é o registro 65: 131 de 256 bytes, 3 *matches* proibidos |
| VALLEY `07ACDB` (15808x1376) | recusado inteiro: os 43 primeiros registros são coerentes, o registro 43 (o 44º) tem `size` 0, e o arquivo de 17 MB continua depois disso |

Bits de descritor não interpretados: `0x00300000` em B, C e D, `0x00200000` em A. Provavelmente seleção de
paleta; não verificado.

## 6. Limites e pontos em aberto

- **Plano A do MAUSO.** Por que 342 de 346 blocos não decodificam com o modelo de fluxo acima é uma
  pergunta em aberto. A causa pode ser uma variante do formato que o modelo não cobre; não foi investigada.
  Enquanto isso o plano A fica quase todo transparente. Nada foi lido byte a byte: só as contagens acima.
- **VALLEY A.** Depois de 43 registros coerentes (no máximo 2,8 MB, pois `size` tem 16 bits) o arquivo de
  17 MB segue com algo que não é registro. O que vem depois é desconhecido. Hoje o plano é recusado inteiro
  e fica transparente, em vez de desenhar só os 43 primeiros blocos.
- **Paleta.** Só o índice de 0 a 255 é usado, direto nas primeiras 256 entradas. Banco de paleta e
  espelhamento não são modelados.
- **Posição.** O mapeamento do plano para a tira (módulo da largura, `y` sem deslocamento vertical,
  divisores de paralaxe 1, 2, 3, 4) é o de antes. Não foi conferido contra o jogo.
- **Aparência.** A janela do jogo não foi aberta: o desenho dos planos B, C e D com arte real não foi visto.
- **Duplicação.** `decode_menu_plane` repete a lógica de bloco do cache e poderia usá-lo.
