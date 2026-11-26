@echo off
rem SC LAN Patch - Windows-Firewall-Regeln
rem   firewall.bat add "<Spielordner>"
rem   firewall.bat remove
setlocal
set "MODE=%~1"
set "GAME=%~2"

for %%N in ("SC LAN Patch - Spiel" "SC LAN Patch - Extreme" "SC LAN Patch - DirectPlay Helper" "SC LAN Patch - TCP" "SC LAN Patch - UDP") do (
    netsh advfirewall firewall delete rule name=%%N >nul 2>&1
)
if /i "%MODE%"=="remove" (
    echo       Firewall-Regeln entfernt
    exit /b 0
)

set "DPSVR=%SystemRoot%\SysWOW64\dplaysvr.exe"
if not exist "%DPSVR%" set "DPSVR=%SystemRoot%\System32\dplaysvr.exe"

rem Programme (eingehend, alle Profile)
if exist "%GAME%\Stronghold Crusader.exe" netsh advfirewall firewall add rule name="SC LAN Patch - Spiel" dir=in action=allow program="%GAME%\Stronghold Crusader.exe" profile=any enable=yes >nul
if exist "%GAME%\Stronghold_Crusader_Extreme.exe" netsh advfirewall firewall add rule name="SC LAN Patch - Extreme" dir=in action=allow program="%GAME%\Stronghold_Crusader_Extreme.exe" profile=any enable=yes >nul
if exist "%DPSVR%" netsh advfirewall firewall add rule name="SC LAN Patch - DirectPlay Helper" dir=in action=allow program="%DPSVR%" profile=any enable=yes >nul

rem Ports: 47624 DirectPlay-Suche, 2300-2400 DirectPlay-Spieldaten, 47630 Patch-Nachbarsuche
netsh advfirewall firewall add rule name="SC LAN Patch - TCP" dir=in action=allow protocol=TCP localport=47624,47630,2300-2400 profile=any enable=yes >nul
netsh advfirewall firewall add rule name="SC LAN Patch - UDP" dir=in action=allow protocol=UDP localport=47624,2300-2400 profile=any enable=yes >nul
echo       OK (Spiel, dplaysvr.exe, TCP 47624/47630/2300-2400, UDP 47624/2300-2400)
exit /b 0
