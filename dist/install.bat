@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem ===================================================================
rem  SC LAN Patch - Installation fuer Windows 10 / 11
rem  Als Administrator ausfuehren (Rechtsklick > Als Administrator ausfuehren)
rem  Aufruf:  install.bat ["C:\Pfad\zu\Stronghold Crusader Extreme HD"]
rem ===================================================================
title SC LAN Patch - Installation

net session >nul 2>&1
if errorlevel 1 (
    echo Bitte als Administrator ausfuehren!
    echo Rechtsklick auf install.bat ^> "Als Administrator ausfuehren"
    pause
    exit /b 1
)

set "SRC=%~dp0"
set "GAME=%~1"

if "%GAME%"=="" if exist "%SRC%Stronghold_Crusader_Extreme.exe" set "GAME=%SRC%"
if "%GAME%"=="" if exist "%SRC%..\Stronghold_Crusader_Extreme.exe" set "GAME=%SRC%.."
if "%GAME%"=="" for %%D in (
    "C:\GOG Games\Stronghold Crusader Extreme HD"
    "C:\Program Files (x86)\GOG Galaxy\Games\Stronghold Crusader Extreme HD"
    "C:\Program Files (x86)\GOG.com\Stronghold Crusader Extreme HD"
    "C:\Program Files (x86)\Steam\steamapps\common\Stronghold Crusader Extreme"
    "C:\Program Files (x86)\Steam\steamapps\common\Stronghold Crusader"
) do if "!GAME!"=="" if exist "%%~D\Stronghold Crusader.exe" set "GAME=%%~D"

if "%GAME%"=="" (
    echo Spielordner nicht gefunden.
    set /p "GAME=Pfad zum Spielordner eingeben: "
)
if "%GAME:~-1%"=="\" set "GAME=%GAME:~0,-1%"

if not exist "%GAME%\Stronghold Crusader.exe" if not exist "%GAME%\Stronghold_Crusader_Extreme.exe" (
    echo In "%GAME%" liegt kein Stronghold Crusader.
    pause
    exit /b 1
)
echo Spielordner: %GAME%
echo.

echo [1/4] DirectPlay aktivieren (Windows-Feature "Legacykomponenten")...
dism /online /enable-feature /featurename:LegacyComponents /all /norestart /quiet >nul 2>&1
dism /online /enable-feature /featurename:DirectPlay /all /norestart /quiet >nul 2>&1
if exist "%SystemRoot%\SysWOW64\dplayx.dll" (
    echo       OK
) else if exist "%SystemRoot%\System32\dplayx.dll" (
    echo       OK
) else (
    echo       WARNUNG: dplayx.dll nicht gefunden. Bitte manuell aktivieren:
    echo       Systemsteuerung ^> Programme ^> Windows-Features ^> Legacykomponenten ^> DirectPlay
)

echo [2/4] Patch kopieren...
if /i not "%SRC%"=="%GAME%\" (
    copy /y "%SRC%dplayx.dll" "%GAME%\dplayx.dll" >nul || goto :copyfail
)
if not exist "%GAME%\sclanpatch.ini" (
    copy /y "%SRC%sclanpatch.ini" "%GAME%\sclanpatch.ini" >nul
) else (
    echo       vorhandene sclanpatch.ini bleibt erhalten
)
echo       OK

echo [3/4] Firewall-Regeln anlegen...
call "%SRC%firewall.bat" add "%GAME%"

echo [4/4] Netzwerkprofil pruefen...
powershell -NoProfile -Command "Get-NetConnectionProfile | ForEach-Object { if ($_.NetworkCategory -eq 'Public') { Write-Host ('      HINWEIS: Netzwerk ' + $_.Name + ' ist \"Oeffentlich\". Fuer LAN-Spiele besser auf \"Privat\" stellen:'); Write-Host ('      Set-NetConnectionProfile -InterfaceIndex ' + $_.InterfaceIndex + ' -NetworkCategory Private') } }" 2>nul

echo.
echo Fertig. Auf ALLEN Mitspieler-PCs installieren.
echo Logdatei beim Spielen: "%GAME%\sclanpatch.log"
pause
exit /b 0

:copyfail
echo Kopieren fehlgeschlagen - laeuft das Spiel noch?
pause
exit /b 1
