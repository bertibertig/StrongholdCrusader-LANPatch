# SC LAN Patch – Anleitung Windows 10 / 11

Diese Schritte auf **jedem** PC ausführen, der mitspielen soll.

## 1. Patch herunterladen

`SC-LANPatch-<Version>-Windows.zip` von der Release-Seite laden und entpacken,
z.B. nach `Downloads\SC-LANPatch`.

## 2. Installieren (automatisch)

1. Spiel beenden.
2. Rechtsklick auf **`install.bat`** → **„Als Administrator ausführen“**.
   Kommt die SmartScreen-Warnung, auf „Weitere Informationen“ → „Trotzdem ausführen“ klicken.
3. Das Skript findet den Spielordner in den GOG- und Steam-Standardpfaden
   selbst. Sonst fragt es nach dem Pfad, dann z.B. eingeben:
   `D:\Spiele\Stronghold Crusader Extreme HD`
4. Das Skript
   - aktiviert **DirectPlay** (Windows-Feature „Legacykomponenten“),
   - kopiert `dplayx.dll` und `sclanpatch.ini` in den Spielordner,
   - legt **Firewall-Regeln** an,
   - warnt, wenn das Netzwerk auf „Öffentlich“ steht.

Erscheint der Hinweis „Netzwerk … ist Öffentlich“, stellt ihr es auf „Privat“:
*Einstellungen → Netzwerk und Internet → Ethernet/WLAN → Netzwerkprofil: Privat*.

## 2b. Installieren (manuell, ohne Skript)

1. *Systemsteuerung → Programme → Windows-Features aktivieren oder deaktivieren →
   Legacykomponenten → **DirectPlay*** anhaken.
2. `dplayx.dll` und `sclanpatch.ini` in den Spielordner kopieren, also dorthin, wo
   `Stronghold_Crusader_Extreme.exe` liegt.
3. Beim ersten Start des Mehrspielermodus fragt die Windows-Firewall nach.
   **„Private Netzwerke“ zulassen.**

## 3. Spielen

1. Spiel ganz normal starten (GOG Galaxy, Steam oder die EXE).
2. **Mehrspieler** → Service-Provider **„Internet TCP/IP Connection For DirectPlay“**.
3. Host: **Host** klicken.
   Mitspieler: Das Feld **„IP-Adresse des Hosts“ leer lassen** und auf **Beitreten** klicken.
   Das Spiel des Hosts taucht nach wenigen Sekunden in der Liste auf.

## Über VPN / Internet (Tailscale, ZeroTier, Hamachi, Radmin …)

`sclanpatch.ini` im Spielordner mit dem Editor öffnen und die VPN-IP(s) der
Mitspieler eintragen:

```ini
Peers=100.101.102.103, 100.64.0.7
```

Es reicht, wenn **eine** Seite die andere kennt.

## Größere Bedienoberfläche (UI-Skalierung)

Bei hohen Auflösungen (z.B. 1920×1080) ist die Leiste unten sehr klein. In
`sclanpatch.ini` im Spielordner:

```ini
UIScale=1.5
```

Damit werden Leiste, Menüs und Dialoge 1,5-fach vergrößert, die Karte bleibt in
voller Auflösung. `2` = doppelt, `1` = aus. Im Spiel unter **Optionen** die
native Auflösung des Monitors wählen.

## Prüfen, ob der Patch läuft

Nach einem Mehrspieler-Versuch liegt `sclanpatch.log` im Spielordner. Darin sollte stehen:

```
game: Stronghold Crusader.exe
patched 7 imports in dpwsockx.dll
discovery: listening on TCP 47630
peer found: 192.168.0.23
blocked DirectPlay NAT helper (dpnathlp.dll) - ...
enum: reply address <source address>:2300
```

Steht dort kein `peer found`, blockiert meist die Firewall auf dem
**anderen** PC: Ist der Patch dort installiert? Steht das Netzwerk auf „Privat“?

Alle PCs müssen **dieselbe EXE** starten: entweder überall `Stronghold Crusader.exe`
oder überall `Stronghold_Crusader_Extreme.exe`. Die beiden Versionen sehen sich
gegenseitig nicht (Zeile `game:` im Log).

Steht dort `enum: WARNING reply address … is not a local address`, meldet DirectPlay
per UPnP die öffentliche Adresse des Routers. Dann `BlockNATHelp=1` in
`sclanpatch.ini` setzen (Standard), und zwar auf **allen** PCs.

## Deinstallieren

Rechtsklick auf **`uninstall.bat`** → „Als Administrator ausführen“ und den
Spielordner als Parameter angeben, oder `uninstall.bat` in den Spielordner
legen und dort ausführen. Alternativ einfach `dplayx.dll` aus dem Spielordner
löschen.
