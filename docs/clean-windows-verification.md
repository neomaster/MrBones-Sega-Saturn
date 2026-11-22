# Verificação em Windows x64 limpo

Este roteiro usa a build MSVC Release x64 `/MT` homologada em `build-chatgpt/release-static`. O computador de destino usa o Windows PowerShell incluído no Windows x64. Forneça à parte os dois discos da edição americana, extraídos em CUE+BIN.

## Preparar o pacote na máquina de compilação

1. Na raiz do repositório, execute o empacotador com a build homologada disponível.

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\package-clean-windows.ps1
   ```

   O empacotador localiza `dumpbin.exe` pelo `build-chatgpt/release-static/CMakeCache.txt` ou pelo `PATH`. Use `-Dumpbin 'C:\caminho\dumpbin.exe'` para informar o caminho manualmente e `-ExpectedSha256` para fixar o hash informado no handoff. O destino deve estar vazio. Use `-OutputDir` para escolher outra pasta.

2. Antes de copiar, o script executa `dumpbin /headers /dependents` e exige PE x64 com exatamente `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `WINMM.dll` e `bcrypt.dll` como imports diretos. A verificação de imports ocorre na máquina de compilação.
3. O script grava `mrbones.exe.sha256` e confere a cópia. Para conferir manualmente, compare os dois resultados abaixo com o SHA-256 homologado.

   ```powershell
   (Get-FileHash .\dist-windows-clean\mrbones.exe -Algorithm SHA256).Hash
   Get-Content .\dist-windows-clean\mrbones.exe.sha256
   ```

O pacote contém os seguintes arquivos.

```text
dist-windows-clean/
  mrbones.exe
  mrbones.exe.sha256
  clean-windows-verification.md
  run_tests.bat
  run_tests.ps1
  manifest/versions.txt
```

## Executar no Windows x64 limpo

Copie a pasta e abra `cmd.exe` dentro dela. Execute `run_tests.bat verify` primeiro para verificar a arquitetura, a presença do manifesto e o hash, mesmo antes de disponibilizar os discos.

Mantenha cada CUE ao lado de **todas** as faixas BIN referenciadas em suas linhas `FILE`. O disco 1 tem dez faixas e o disco 2 tem doze. Use caminhos externos como no exemplo, ajustando a letra de unidade e as pastas.

```bat
set "DISC1=D:\Discos\disco1\Mr. Bones (USA) (Disc 1).cue"
set "DISC2=D:\Discos\disco2\Mr. Bones (USA) (Disc 2).cue"
run_tests.bat check -Disc1Cue "%DISC1%" -Disc2Cue "%DISC2%"
run_tests.bat lose -Disc1Cue "%DISC1%" -Disc2Cue "%DISC2%"
run_tests.bat finale -Disc1Cue "%DISC1%" -Disc2Cue "%DISC2%"
run_tests.bat campaign -Disc1Cue "%DISC1%" -Disc2Cue "%DISC2%"
```

O padrão é `check`. Ao omitir os caminhos, o script busca os CUEs com os nomes do exemplo nas subpastas `disco1` e `disco2` da distribuição. Também é possível usar `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\run_tests.ps1` com os mesmos argumentos. Os testes de jogo conferem o SHA-256 antes da execução e gravam saves em `%LOCALAPPDATA%\MrBonesPort\clean-test-saves`.

| Modo | Comando principal | O que conferir |
| --- | --- | --- |
| `verify` | Verificações do script PowerShell | Saída 0 e SHA-256 correspondente ao do pacote. |
| `check` | `mrbones.exe <disc1.cue> manifest\versions.txt --disc2 <disc2.cue> --check` | Saída 0; identidade dos discos, recursos e rotas sem janela. |
| `lose` | `... --level /28LOSE` | Vídeo de derrota sem listras, áudio, fim e retorno/saída; ausência de travamento ou clique residual. |
| `finale` | `... --level /27FINALE` | A arena abre primeiro; vença para iniciar o vídeo final. Conferir imagem, áudio e transição ao menu. |
| `campaign` | `... --full-campaign` | Saída 0, troca lógica de disco, `FINALE→MENU` e `nvmshadow.bin` no diretório de saves. |

`check` e `campaign` executam em modo automatizado com áudio mudo. Em `lose` e `finale`, observe imagem, áudio, duração e transição final. Para rodar essas cenas com áudio mudo, defina `MB_MUTE=1` antes de iniciar o script. Registre o resultado com a versão do Windows, o hash do EXE e os controles usados.

Em caso de erro ao abrir um CUE, confira as faixas BIN que ele referencia. Se o hash divergir, copie o pacote novamente e refaça `verify`.
