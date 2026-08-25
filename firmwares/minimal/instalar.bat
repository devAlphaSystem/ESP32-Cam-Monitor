@echo off
call "%~dp0..\installer_common.bat" minimal "%~dp0."
exit /b %errorlevel%
