@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "INSTALL_MODE=%~1"
set "PROJECT_DIR=%~2"

if /i "%INSTALL_MODE%"=="minimal" goto mode_valid
if /i "%INSTALL_MODE%"=="zeroed" goto mode_valid
if /i "%INSTALL_MODE%"=="logreader" goto mode_valid

echo.
echo ERROR: invalid installation mode.
goto failure

:mode_valid
if not defined PROJECT_DIR (
    echo.
    echo ERROR: installer folder not provided.
    goto failure
)

call :find_platformio
if errorlevel 1 goto platformio_missing

cls
if /i "%INSTALL_MODE%"=="minimal" (
    title ESP32 - Install minimal firmware
    echo ============================================================
    echo                 ESP32 - MINIMAL FIRMWARE
    echo ============================================================
    echo.
    echo This process erases the entire flash, including firmware,
    echo settings, NVS, logs and all other partitions.
    echo Then the minimal firmware will be built and flashed.
) else if /i "%INSTALL_MODE%"=="zeroed" (
    title ESP32 - Erase completely
    echo ============================================================
    echo                   ESP32 - ERASE FLASH
    echo ============================================================
    echo.
    echo This process erases the entire flash, including firmware,
    echo settings, NVS, logs and all other partitions.
    echo No firmware will be flashed afterward.
) else (
    title ESP32 - Forensic log reader
    echo ============================================================
    echo              ESP32 - FORENSIC LOG READER
    echo ============================================================
    echo.
    echo This process saves a full RAW copy of the logs,
    echo erases firmware, settings, NVS and the other partitions,
    echo keeps the logs partition and installs the local reader.
    echo.
    echo The logs partition will be checked before and after flashing.
    echo No full flash erase will be performed.
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
if /i "%INSTALL_MODE%"=="minimal" (
    echo Action: erase the entire flash and install the minimal firmware.
) else if /i "%INSTALL_MODE%"=="zeroed" (
    echo Action: erase the entire flash and leave the ESP32 without firmware.
) else (
    echo Action: keep only the logs and install the forensic reader.
)
echo.
choice /c YN /n /m "Confirm the operation? [Y/N]: "
if errorlevel 2 goto cancelled

if /i "%INSTALL_MODE%"=="minimal" goto install_minimal
if /i "%INSTALL_MODE%"=="logreader" goto install_logreader
goto erase_only

:install_minimal
echo.
echo ------------------------------------------------------------
echo STEP 1/3 - BUILD MINIMAL FIRMWARE
echo ------------------------------------------------------------
call :run_platformio build
if errorlevel 1 goto build_failed
echo     Build complete.

echo.
echo ------------------------------------------------------------
echo STEP 2/3 - ERASE THE ENTIRE FLASH ON %ESP32_PORT%
echo ------------------------------------------------------------
call :run_platformio erase
if errorlevel 1 goto erase_failed

echo.
echo ------------------------------------------------------------
echo STEP 3/3 - FLASH MINIMAL FIRMWARE ON %ESP32_PORT%
echo ------------------------------------------------------------
call :run_platformio upload
if errorlevel 1 goto upload_failed

echo.
echo ============================================================
echo RESULT: DONE
echo Flash erased and minimal firmware flashed successfully.
echo ============================================================
goto success

:install_logreader
echo.
echo ------------------------------------------------------------
echo STEP 1/6 - BUILD LOG READER
echo ------------------------------------------------------------
call :run_platformio build
if errorlevel 1 goto build_failed
echo     Build complete.

echo.
echo ------------------------------------------------------------
echo STEP 2/6 - SAVE RAW BACKUP OF THE LOGS
echo ------------------------------------------------------------
call :run_platformio backup-logs
if errorlevel 1 goto backup_failed

echo.
echo ------------------------------------------------------------
echo STEP 3/6 - ERASE EVERYTHING EXCEPT THE LOGS
echo ------------------------------------------------------------
call :run_platformio erase-except-logs
if errorlevel 1 goto selective_erase_failed

echo.
echo ------------------------------------------------------------
echo STEP 4/6 - VERIFY PRESERVED LOGS
echo ------------------------------------------------------------
call :run_platformio verify-logs-after-erase
if errorlevel 1 goto preserved_logs_failed

echo.
echo ------------------------------------------------------------
echo STEP 5/6 - FLASH LOG READER
echo ------------------------------------------------------------
call :run_platformio upload
if errorlevel 1 goto upload_failed

echo.
echo ------------------------------------------------------------
echo STEP 6/6 - VERIFY LOGS AFTER FLASHING
echo ------------------------------------------------------------
call :run_platformio verify-logs-after-upload
if errorlevel 1 goto final_logs_failed

echo.
echo ============================================================
echo RESULT: DONE
echo Forensic reader installed and logs partition preserved.
echo The RAW backup stays saved in the backups folder.
echo.
echo Local network: http://esp32-log-reader.local/
echo Fallback network: ESP32-LogReader-XXXXXX
echo Fallback network password: Logs-XXXXXX
echo The password uses the same 6-character suffix shown in the Wi-Fi name.
echo On the access point, open http://192.168.4.1/
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

:backup_failed
echo.
echo ============================================================
echo RESULT: LOG BACKUP FAILED
echo The erase did not start and no firmware was flashed.
echo ============================================================
goto failure

:selective_erase_failed
echo.
echo ============================================================
echo RESULT: SELECTIVE ERASE FAILED
echo The installation was interrupted. The RAW backup stays saved.
echo The range reserved for logs is not part of the erase.
echo ============================================================
goto failure

:preserved_logs_failed
echo.
echo ============================================================
echo RESULT: LOGS NOT CONFIRMED
echo The reader firmware was not flashed.
echo The RAW backup stays saved for recovery and analysis.
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
if /i "%INSTALL_MODE%"=="logreader" (
    echo The firmware area was erased, but the reader was not flashed.
    echo The logs were already verified and the RAW backup stays saved.
) else (
    echo The flash was erased, but the firmware was not flashed.
)
echo Run the installer again after checking the USB connection.
echo ============================================================
goto failure

:final_logs_failed
echo.
echo ============================================================
echo RESULT: FLASHED WITHOUT FINAL LOG CONFIRMATION
echo The reader was flashed, but the final check did not complete.
echo Keep the RAW backup and do not overwrite the logs partition.
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
