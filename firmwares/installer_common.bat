@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "INSTALL_MODE=%~1"
set "PROJECT_DIR=%~2"

if /i "%INSTALL_MODE%"=="minimal" goto mode_valid
if /i "%INSTALL_MODE%"=="zeroed" goto mode_valid
if /i "%INSTALL_MODE%"=="logreader" goto mode_valid

echo.
echo ERRO: modo de instalacao invalido.
goto failure

:mode_valid
if not defined PROJECT_DIR (
    echo.
    echo ERRO: pasta do instalador nao informada.
    goto failure
)

call :find_platformio
if errorlevel 1 goto platformio_missing

cls
if /i "%INSTALL_MODE%"=="minimal" (
    title ESP32 - Instalar firmware minimo
    echo ============================================================
    echo                 ESP32 - FIRMWARE MINIMO
    echo ============================================================
    echo.
    echo Este processo apaga toda a flash, incluindo firmware,
    echo configuracoes, NVS, logs e demais particoes.
    echo Depois, o firmware minimo sera compilado e gravado.
) else if /i "%INSTALL_MODE%"=="zeroed" (
    title ESP32 - Zerar completamente
    echo ============================================================
    echo                   ESP32 - ZERAR FLASH
    echo ============================================================
    echo.
    echo Este processo apaga toda a flash, incluindo firmware,
    echo configuracoes, NVS, logs e demais particoes.
    echo Nenhum firmware sera gravado depois.
) else (
    title ESP32 - Leitor forense de logs
    echo ============================================================
    echo              ESP32 - LEITOR FORENSE DE LOGS
    echo ============================================================
    echo.
    echo Este processo salva uma copia RAW integral dos logs,
    echo apaga firmware, configuracoes, NVS e demais particoes,
    echo preserva a particao de logs e instala o leitor local.
    echo.
    echo A particao de logs sera conferida antes e depois da gravacao.
    echo Nenhum apagamento completo da flash sera executado.
)

:ask_port
echo.
set "ESP32_PORT_INPUT="
set /p "ESP32_PORT_INPUT=Informe a porta serial do ESP32 (exemplo: COM3): "
set "ESP32_PORT="

for /f "usebackq delims=" %%P in (`powershell.exe -NoLogo -NoProfile -NonInteractive -Command "$p=$env:ESP32_PORT_INPUT.Trim().ToUpperInvariant(); if ($p -match '^COM[1-9][0-9]*$') { $p }"`) do set "ESP32_PORT=%%P"

if not defined ESP32_PORT (
    echo Porta invalida. Informe no formato COM seguido de um numero, como COM3.
    goto ask_port
)

echo.
echo Porta selecionada: %ESP32_PORT%
if /i "%INSTALL_MODE%"=="minimal" (
    echo Acao: apagar toda a flash e instalar o firmware minimo.
) else if /i "%INSTALL_MODE%"=="zeroed" (
    echo Acao: apagar toda a flash e deixar o ESP32 sem firmware.
) else (
    echo Acao: preservar somente os logs e instalar o leitor forense.
)
echo.
choice /c SN /n /m "Confirma a operacao? [S/N]: "
if errorlevel 2 goto cancelled

if /i "%INSTALL_MODE%"=="minimal" goto install_minimal
if /i "%INSTALL_MODE%"=="logreader" goto install_logreader
goto erase_only

:install_minimal
echo.
echo ------------------------------------------------------------
echo ETAPA 1/3 - COMPILAR FIRMWARE MINIMO
echo ------------------------------------------------------------
call :run_platformio build
if errorlevel 1 goto build_failed
echo     Compilacao concluida.

echo.
echo ------------------------------------------------------------
echo ETAPA 2/3 - APAGAR TODA A FLASH EM %ESP32_PORT%
echo ------------------------------------------------------------
call :run_platformio erase
if errorlevel 1 goto erase_failed

echo.
echo ------------------------------------------------------------
echo ETAPA 3/3 - GRAVAR FIRMWARE MINIMO EM %ESP32_PORT%
echo ------------------------------------------------------------
call :run_platformio upload
if errorlevel 1 goto upload_failed

echo.
echo ============================================================
echo RESULTADO: CONCLUIDO
echo Flash apagada e firmware minimo gravado com sucesso.
echo ============================================================
goto success

:install_logreader
echo.
echo ------------------------------------------------------------
echo ETAPA 1/6 - COMPILAR LEITOR DE LOGS
echo ------------------------------------------------------------
call :run_platformio build
if errorlevel 1 goto build_failed
echo     Compilacao concluida.

echo.
echo ------------------------------------------------------------
echo ETAPA 2/6 - SALVAR BACKUP RAW DOS LOGS
echo ------------------------------------------------------------
call :run_platformio backup-logs
if errorlevel 1 goto backup_failed

echo.
echo ------------------------------------------------------------
echo ETAPA 3/6 - APAGAR TUDO EXCETO OS LOGS
echo ------------------------------------------------------------
call :run_platformio erase-except-logs
if errorlevel 1 goto selective_erase_failed

echo.
echo ------------------------------------------------------------
echo ETAPA 4/6 - CONFERIR LOGS PRESERVADOS
echo ------------------------------------------------------------
call :run_platformio verify-logs-after-erase
if errorlevel 1 goto preserved_logs_failed

echo.
echo ------------------------------------------------------------
echo ETAPA 5/6 - GRAVAR LEITOR DE LOGS
echo ------------------------------------------------------------
call :run_platformio upload
if errorlevel 1 goto upload_failed

echo.
echo ------------------------------------------------------------
echo ETAPA 6/6 - CONFERIR LOGS APOS A GRAVACAO
echo ------------------------------------------------------------
call :run_platformio verify-logs-after-upload
if errorlevel 1 goto final_logs_failed

echo.
echo ============================================================
echo RESULTADO: CONCLUIDO
echo Leitor forense instalado e particao de logs preservada.
echo O backup RAW permanece salvo na pasta backups.
echo.
echo Rede local: http://esp32-log-reader.local/
echo Rede alternativa: ESP32-LogReader-XXXXXX
echo Senha da rede alternativa: Logs-XXXXXX
echo Use na senha o mesmo sufixo de 6 caracteres exibido no Wi-Fi.
echo No ponto de acesso, abra http://192.168.4.1/
echo ============================================================
goto success

:erase_only
echo.
echo ------------------------------------------------------------
echo ETAPA 1/1 - APAGAR TODA A FLASH EM %ESP32_PORT%
echo ------------------------------------------------------------
call :run_platformio erase
if errorlevel 1 goto erase_failed

echo.
echo ============================================================
echo RESULTADO: CONCLUIDO
echo ESP32 sem firmware, configuracoes ou particoes salvas.
echo ============================================================
goto success

:run_platformio
powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0platformio_compact.ps1" -PlatformIo "%PIO_EXE%" -ProjectDir "%PROJECT_DIR%" -EnvironmentName esp32dev -Action "%~1" -Port "%ESP32_PORT%"
exit /b %errorlevel%

:find_platformio
set "PIO_EXE="

if defined ESP32_PLATFORMIO_EXE if exist "%ESP32_PLATFORMIO_EXE%" set "PIO_EXE=%ESP32_PLATFORMIO_EXE%"
if not defined PIO_EXE if exist "%USERPROFILE%\.platformio\penv\Scripts\platformio.exe" set "PIO_EXE=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"

if not defined PIO_EXE (
    for /f "delims=" %%P in ('where platformio.exe 2^>nul') do if not defined PIO_EXE set "PIO_EXE=%%P"
)

if not defined PIO_EXE (
    for /f "delims=" %%P in ('where pio.exe 2^>nul') do if not defined PIO_EXE set "PIO_EXE=%%P"
)

if not defined PIO_EXE exit /b 1
exit /b 0

:platformio_missing
echo.
echo ERRO: PlatformIO nao foi encontrado.
echo Instale o PlatformIO Core ou defina ESP32_PLATFORMIO_EXE com o
echo caminho completo para platformio.exe e execute novamente.
goto failure

:build_failed
echo.
echo ============================================================
echo RESULTADO: FALHA NA COMPILACAO
echo O ESP32 nao foi alterado.
echo ============================================================
goto failure

:backup_failed
echo.
echo ============================================================
echo RESULTADO: FALHA NO BACKUP DOS LOGS
echo O apagamento nao foi iniciado e nenhum firmware foi gravado.
echo ============================================================
goto failure

:selective_erase_failed
echo.
echo ============================================================
echo RESULTADO: FALHA NO APAGAMENTO SELETIVO
echo A instalacao foi interrompida. O backup RAW permanece salvo.
echo A faixa reservada aos logs nao faz parte do apagamento.
echo ============================================================
goto failure

:preserved_logs_failed
echo.
echo ============================================================
echo RESULTADO: LOGS NAO CONFIRMADOS
echo O firmware leitor nao foi gravado.
echo O backup RAW permanece salvo para recuperacao e analise.
echo ============================================================
goto failure

:erase_failed
echo.
echo ============================================================
echo RESULTADO: FALHA AO APAGAR A FLASH
echo O apagamento nao foi concluido.
echo ============================================================
goto failure

:upload_failed
echo.
echo ============================================================
echo RESULTADO: FALHA NA GRAVACAO
if /i "%INSTALL_MODE%"=="logreader" (
    echo A area de firmware foi apagada, mas o leitor nao foi gravado.
    echo Os logs ja foram conferidos e o backup RAW permanece salvo.
) else (
    echo A flash foi apagada, mas o firmware nao foi gravado.
)
echo Execute o instalador novamente depois de conferir o USB.
echo ============================================================
goto failure

:final_logs_failed
echo.
echo ============================================================
echo RESULTADO: GRAVACAO SEM CONFIRMACAO FINAL DOS LOGS
echo O leitor foi gravado, mas a conferencia final nao foi concluida.
echo Preserve o backup RAW e nao sobrescreva a particao de logs.
echo ============================================================
goto failure

:cancelled
echo.
echo Operacao cancelada. Nenhuma alteracao foi feita no ESP32.
goto success

:failure
echo.
echo Pressione qualquer tecla para fechar.
pause >nul
exit /b 1

:success
echo.
echo Pressione qualquer tecla para fechar.
pause >nul
exit /b 0
