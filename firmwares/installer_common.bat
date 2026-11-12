@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "INSTALL_MODE=%~1"
set "PROJECT_DIR=%~2"

if /i "%INSTALL_MODE%"=="minimal" goto mode_valid
if /i "%INSTALL_MODE%"=="zeroed" goto mode_valid
if /i "%INSTALL_MODE%"=="watchdog" goto mode_valid

echo.
echo ERROR: invalid installation mode.
goto failure

:mode_valid
set "FIRMWARE_NAME=MINIMAL"
set "INSTALL_STEPS=3"
if /i "%INSTALL_MODE%"=="watchdog" (
    set "FIRMWARE_NAME=WATCHDOG"
    set "INSTALL_STEPS=2"
)

if not defined PROJECT_DIR (
    echo.
    echo ERROR: installer folder not provided.
    goto failure
)

call :find_platformio
if errorlevel 1 goto platformio_missing

cls
if /i "%INSTALL_MODE%"=="watchdog" (
    title ESP32 - Install Watchdog firmware
    echo ============================================================
    echo                 ESP32 - WATCHDOG FIRMWARE
    echo ============================================================
    echo.
    echo This process builds and flashes the Watchdog firmware.
    echo Saved settings, Wi-Fi credentials and logs are preserved.
) else if /i "%INSTALL_MODE%"=="minimal" (
    title ESP32 - Install minimal firmware
    echo ============================================================
    echo                 ESP32 - MINIMAL FIRMWARE
    echo ============================================================
    echo.
    echo This process erases the entire flash, including firmware,
    echo settings, NVS, logs and all other partitions.
    echo Then the minimal firmware will be built and flashed.
) else (
    title ESP32 - Erase completely
    echo ============================================================
    echo                   ESP32 - ERASE FLASH
    echo ============================================================
    echo.
    echo This process erases the entire flash, including firmware,
    echo settings, NVS, logs and all other partitions.
    echo No firmware will be flashed afterward.
)

:ask_port
echo.
set "ESP32_PORT_INPUT="
set /p "ESP32_PORT_INPUT=Enter the ESP32 serial port (example: COM3): "
set "ESP32_PORT="

for /f "usebackq delims=" %%P in (`powershell.exe -NoLogo -NoProfile -NonInteractive -Command "$p=$env:ESP32_PORT_INPUT.Trim().ToUpperInvariant(); if ($p -match '^COM[1-9][0-9]*$') { $p }"`) do set "ESP32_PORT=%%P"

if not defined ESP32_PORT (
    echo Invalid port. Use the format COM followed by a number, such as COM3.
    goto ask_port
)

echo.
echo Selected port: %ESP32_PORT%
if /i "%INSTALL_MODE%"=="watchdog" (
    echo Action: install the Watchdog firmware and preserve saved data.
) else if /i "%INSTALL_MODE%"=="minimal" (
    echo Action: erase the entire flash and install the minimal firmware.
) else (
    echo Action: erase the entire flash and leave the ESP32 without firmware.
)
echo.
choice /c YN /n /m "Confirm the operation? [Y/N]: "
if errorlevel 2 goto cancelled

if /i "%INSTALL_MODE%"=="zeroed" goto erase_only
goto install_firmware

:install_firmware
echo.
echo ------------------------------------------------------------
echo STEP 1/%INSTALL_STEPS% - BUILD %FIRMWARE_NAME% FIRMWARE
echo ------------------------------------------------------------
call :run_platformio build
if errorlevel 1 goto build_failed
echo     Build complete.

if /i "%INSTALL_MODE%"=="watchdog" goto upload_firmware

echo.
echo ------------------------------------------------------------
echo STEP 2/3 - ERASE THE ENTIRE FLASH ON %ESP32_PORT%
echo ------------------------------------------------------------
call :run_platformio erase
if errorlevel 1 goto erase_failed

:upload_firmware
echo.
echo ------------------------------------------------------------
echo STEP %INSTALL_STEPS%/%INSTALL_STEPS% - FLASH %FIRMWARE_NAME% FIRMWARE ON %ESP32_PORT%
echo ------------------------------------------------------------
call :run_platformio upload
if errorlevel 1 goto upload_failed

echo.
echo ============================================================
echo RESULT: DONE
if /i "%INSTALL_MODE%"=="watchdog" (
    echo Watchdog firmware flashed successfully. Saved data preserved.
) else (
    echo Flash erased and minimal firmware flashed successfully.
)
echo ============================================================
goto success

:erase_only
echo.
echo ------------------------------------------------------------
echo STEP 1/1 - ERASE THE ENTIRE FLASH ON %ESP32_PORT%
echo ------------------------------------------------------------
call :run_platformio erase
if errorlevel 1 goto erase_failed

echo.
echo ============================================================
echo RESULT: DONE
echo ESP32 without firmware, settings or saved partitions.
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
echo ERROR: PlatformIO was not found.
echo Install PlatformIO Core or set ESP32_PLATFORMIO_EXE to the
echo full path of platformio.exe and run again.
goto failure

:build_failed
echo.
echo ============================================================
echo RESULT: BUILD FAILED
echo The ESP32 was not changed.
echo ============================================================
goto failure

:erase_failed
echo.
echo ============================================================
echo RESULT: FLASH ERASE FAILED
echo The erase did not complete.
echo ============================================================
goto failure

:upload_failed
echo.
echo ============================================================
echo RESULT: FLASHING FAILED
if /i "%INSTALL_MODE%"=="watchdog" (
    echo The Watchdog firmware upload did not complete.
) else (
    echo The flash was erased, but the firmware was not flashed.
)
echo Run the installer again after checking the USB connection.
echo ============================================================
goto failure

:cancelled
echo.
echo Operation cancelled. No change was made to the ESP32.
goto success

:failure
echo.
echo Press any key to close.
pause >nul
exit /b 1

:success
echo.
echo Press any key to close.
pause >nul
exit /b 0
