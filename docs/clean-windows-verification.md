# Verificação em Windows x64 limpo

Este roteiro usa `build-static/mrbones.exe` Release `/MT` e os dois discos da edição americana extraídos em CUE+BIN. A máquina de destino precisa de Windows x64 e PowerShell, já incluído no Windows; não precisa de Visual Studio, Git, Python, FFmpeg nem DLLs de codec. O pacote inclui os dados dos discos fornecidos pelo usuário e destina-se ao teste local.

## Preparar o pacote na máquina de compilação

1. Confirme que `build-static/mrbones.exe` foi compilado em Release `/MT`. O cache atual registra `CMAKE_BUILD_TYPE=Release` e `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`.
2. Mantenha `manifest/versions.txt` e cada CUE ao lado de **todas** as faixas BIN referenciadas em suas linhas `FILE`. O disco 1 tem dez faixas; o disco 2 tem doze. Não basta copiar apenas a faixa de dados.
3. Na raiz do repositório, execute:

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\package-clean-windows.ps1
   ```

   O empacotador localiza `dumpbin.exe` pelo `build-static/CMakeCache.txt` ou pelo `PATH`; use `-Dumpbin 'C:\caminho\dumpbin.exe'` se necessário. Aceita `-Disc1Cue`, `-Disc2Cue` e `-OutputDir` para fontes ou destino diferentes. O destino deve estar vazio.

4. Antes de copiar, o script executa `dumpbin /headers /dependents` e exige PE x64 com **exatamente** `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `WINMM.dll` e `bcrypt.dll` como imports diretos. Qualquer DLL de runtime ou codec externo reprova o pacote. A checagem equivalente da suíte é `tests/test_static_imports.cmake`.
5. O script grava `mrbones.exe.sha256` e confere o hash da cópia. Para conferir manualmente:

   ```powershell
   (Get-FileHash .\dist-windows-clean\mrbones.exe -Algorithm SHA256).Hash
   Get-Content .\dist-windows-clean\mrbones.exe.sha256
   ```

   Os valores devem ser iguais. Registre o hash no relatório da compilação. A checagem de imports é feita na máquina de compilação, pois `dumpbin` não estará disponível no Windows limpo.

Estrutura resultante:

```text
dist-windows-clean/
  mrbones.exe
  mrbones.exe.sha256
  clean-windows-verification.md
  run_tests.bat
  run_tests.ps1
  manifest/versions.txt
  disco1/Mr. Bones (USA) (Disc 1).cue
  disco1/Mr. Bones (USA) (Disc 1) (Track 01).bin ... (Track 10).bin
  disco2/Mr. Bones (USA) (Disc 2).cue
  disco2/Mr. Bones (USA) (Disc 2) (Track 01).bin ... (Track 12).bin
```

## Executar no Windows x64 limpo

Copie a pasta inteira para um diretório gravável e abra `cmd.exe` nessa pasta. O runner confere SHA-256 antes de cada teste, usa os CUEs locais e grava saves em `%LOCALAPPDATA%\MrBonesPort\clean-test-saves`. Execute em ordem:

```bat
run_tests.bat check
run_tests.bat lose
run_tests.bat finale
run_tests.bat campaign
```

`run_tests.bat` aceita `check`, `lose`, `finale` ou `campaign`; sem argumento, usa `check`. O mesmo vale para `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\run_tests.ps1 <modo>`.

| Modo | Comando principal | O que conferir |
| --- | --- | --- |
| `check` | `mrbones.exe <disc1.cue> manifest\versions.txt --disc2 <disc2.cue> --check` | Saída 0; identidade dos discos, recursos e rotas sem janela. |
| `lose` | `... --level /28LOSE` | Vídeo de derrota sem listras, áudio, fim e retorno/saída; ausência de travamento ou clique residual. |
| `finale` | `... --level /27FINALE` | A arena abre primeiro; vença para iniciar o vídeo final. Conferir imagem, áudio e transição ao menu. |
| `campaign` | `... --full-campaign` | Saída 0, troca lógica de disco, `FINALE→MENU` e `nvmshadow.bin` no diretório de saves. |

`--check` e `--full-campaign` são verificações automatizadas sem execução visual da campanha inteira. Para homologar apresentação e jogabilidade, observe `lose` e `finale` e registre, para cada caso, Windows/versão, dispositivo de áudio, hash do EXE, resultado, imagem, áudio, duração aproximada, controles usados e transição final. Compare a mesma sequência no Ymir de referência; anote diferenças remanescentes. Não trate saída 0 dos modos headless como prova de fidelidade audiovisual.

Se faltar um BIN, o CUE não abrirá corretamente. Se o hash divergir, não use aquele EXE: copie novamente o pacote gerado. Guarde o relatório e o hash junto da versão testada.
