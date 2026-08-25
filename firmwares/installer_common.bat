@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "INSTALL_MODE=%~1"
set "PROJECT_DIR=%~2"

if /i "%INSTALL_MODE%"=="minimal" goto mode_valid
if /i "%INSTALL_MODE%"=="zeroed" goto mode_valid

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
) else (
    title ESP32 - Zerar completamente
    echo ============================================================
    echo                   ESP32 - ZERAR FLASH
    echo ============================================================
    echo.
    echo Este processo apaga toda a flash, incluindo firmware,
    echo configuracoes, NVS, logs e demais particoes.
    echo Nenhum firmware sera gravado depois.
)

:ask_port
echo.
set "ESP32_PORT_INPUT="
set /p "ESP32_PORT_INPUT=Informe a porta serial do ESP32 (exemplo: COM5): "
set "ESP32_PORT="

for /f "usebackq delims=" %%P in (`powershell.exe -NoLogo -NoProfile -NonInteractive -Command "$p=$env:ESP32_PORT_INPUT.Trim().ToUpperInvariant(); if ($p -match '^COM[1-9][0-9]*$') { $p }"`) do set "ESP32_PORT=%%P"

if not defined ESP32_PORT (
    echo Porta invalida. Informe no formato COM seguido de um numero, como COM5.
    goto ask_port
)

echo.
echo Porta selecionada: %ESP32_PORT%
if /i "%INSTALL_MODE%"=="minimal" (
    echo Acao: apagar toda a flash e instalar o firmware minimo.
) else (
    echo Acao: apagar toda a flash e deixar o ESP32 sem firmware.
)
echo.
choice /c SN /n /m "Confirma a operacao? [S/N]: "
if errorlevel 2 goto cancelled

if /i "%INSTALL_MODE%"=="minimal" goto install_minimal
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
echo A flash foi apagada, mas o firmware nao foi gravado.
echo Execute o instalador novamente depois de conferir o USB.
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
