@echo off
call "%~dp0firmwares\installer_common.bat" watchdog "%~dp0."
exit /b %errorlevel%
