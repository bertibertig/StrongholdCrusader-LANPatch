@echo off
setlocal EnableExtensions
rem SC LAN Patch - Deinstallation (als Administrator ausfuehren)
rem   uninstall.bat ["<Spielordner>"]
title SC LAN Patch - Deinstallation
net session >nul 2>&1
if errorlevel 1 (
    echo Bitte als Administrator ausfuehren!
    pause
    exit /b 1
)
set "GAME=%~1"
if "%GAME%"=="" if exist "%~dp0Stronghold_Crusader_Extreme.exe" set "GAME=%~dp0"
if "%GAME%"=="" if exist "%~dp0Stronghold Crusader.exe" set "GAME=%~dp0"
if "%GAME%"=="" set /p "GAME=Pfad zum Spielordner eingeben: "
if "%GAME:~-1%"=="\" set "GAME=%GAME:~0,-1%"

if not exist "%GAME%\dplayx.dll" echo Kein Patch in "%GAME%" gefunden.
if exist "%GAME%\dplayx.dll" del /f "%GAME%\dplayx.dll"
if exist "%GAME%\sclanpatch.log" del /f "%GAME%\sclanpatch.log"
echo sclanpatch.ini bleibt erhalten (bei Bedarf von Hand loeschen).
call "%~dp0firewall.bat" remove
echo Patch entfernt.
pause
