@echo off
setlocal
rem SC LAN Patch - Deinstallation (als Administrator ausfuehren)
rem   uninstall.bat ["<Spielordner>"]
net session >nul 2>&1
if errorlevel 1 (
    echo Bitte als Administrator ausfuehren!
    pause
    exit /b 1
)
set "GAME=%~1"
if "%GAME%"=="" set "GAME=%~dp0"
if "%GAME:~-1%"=="\" set "GAME=%GAME:~0,-1%"

if exist "%GAME%\dplayx.dll" del /f "%GAME%\dplayx.dll"
if exist "%GAME%\sclanpatch.log" del /f "%GAME%\sclanpatch.log"
echo sclanpatch.ini bleibt erhalten (bei Bedarf von Hand loeschen).
call "%~dp0firewall.bat" remove
echo Patch entfernt.
pause
