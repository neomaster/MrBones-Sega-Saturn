# Confirmação do Estado Inicial de FINALE CRing

Evidência estática extraída de `/27FINALE/03CODE` (base `0x00232000`, SH-2 Big-Endian) da rotina `CRing::CRing` em `0x0028d4b4`:

## 1. Desmontagem do Construtor de CRing (0x0028d4b4..0x0028d554)

```assembly
0028d4ea:  mov r9,r8           ; r9 = ponteiro do objeto CRing (alocado com tamanho 0x070c)
0028d4ec:  add #68,r8          ; r8 = objeto + 0x44 (início do array de 16 slots de 108 bytes)
0028d4fa:  mov r8,r10          ; r10 = r8 (slot 0)
0028d4fc:  add #104,r10        ; r10 = slot 0 + 0x68 (campo de status ativo do anel)
0028d4fe:  mov #15,r1          ; limite do loop de 16 iterações (r12 = 0..15)
0028d500:  cmp/gt r1,r12
0028d502:  bt 0x28d538         ; sai do loop após 16 slots

; Loop de Inicialização dos Slots:
0028d504:  mov #1,r1           ; r1 = 1 (ATIVO / ACESO)
0028d506:  mov.l r1,@r10       ; GRAVA 1 EM slot->flags (+0x68)!
0028d508:  add #108,r10        ; avança para o próximo slot (+0x6c bytes)
...
0028d534:  bra 0x28d4fe        ; repete para todos os 16 slots

; Inicialização do Contador Global do Anel:
0028d538:  mov.w @(0x28d556,pc),r0 ; carrega offset 0x0708
0028d53a:  mov #16,r1          ; r1 = 16
0028d53c:  mov.l r1,@(r0,r9)   ; GRAVA 16 EM objeto + 0x0708!
```

## 2. Conclusão e Recomendação

* **Hipótese Confirmada**: A hipótese **`16-lit` (16 slots acesos)** está 100% correta e comprovada no binário original do Sega Saturn.
* **`objeto + 0x708`**: Inicializado exatamente com **16**.
* **`entry + 0x68`**: Todos os 16 slots são inicializados com **1**.
* **Mecânica de Vitória**: Cada acerto com projétil azul desativa o slot (`flags = 0`) e decrementa `+0x708`. Quando `+0x708 == 0`, o binário original chama `EndLevel(0)` e finaliza a fase.
* **Recomendação**: **Manter `keep-16-lit`** em `special_phase.c`.
