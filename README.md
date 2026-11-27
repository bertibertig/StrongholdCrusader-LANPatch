# SC LAN Patch – stabiler LAN-Modus für Stronghold Crusader (HD / Extreme HD)

Ein Drop-in-Patch (`dplayx.dll`) für den LAN-/Mehrspielermodus von
Stronghold Crusader. Er löst das Problem, dass sich die Mitspieler im LAN
**gegenseitig nicht finden**, und macht die Verbindung robuster.
Hauptziel ist **Windows 10**. Linux (Proton/Wine) wird ebenfalls unterstützt.

> Die Spieldateien sind urheberrechtlich geschützt und **nicht** Teil dieses
> Repositories (siehe `.gitignore`). Ihr braucht eine eigene Kopie des Spiels
> (getestet mit der GOG-Version, `Stronghold_Crusader_Extreme.exe` und
> `Stronghold Crusader.exe`). Die EXE wird **nicht** verändert.

## Warum findet man sich nicht?

Stronghold Crusader nutzt Microsoft **DirectPlay** (TCP/IP-Provider
`dpwsockx.dll`). Bleibt das IP-Feld leer, sucht DirectPlay Spiele mit einem
einzigen UDP-Broadcast an `255.255.255.255:47624`. Daran scheitert es oft:

1. **Mehrere Netzwerkadapter.** Windows schickt den Broadcast nur über
   *einen* Adapter hinaus. Das ist oft der falsche: VirtualBox, Hyper-V/WSL,
   Docker, VPN, Tailscale, ZeroTier, Hamachi, WLAN + LAN gleichzeitig …
2. **Getrennte Netze / VPN.** Broadcasts werden nicht geroutet.
3. **Firewall.** Windows blockiert eingehende DirectPlay-Pakete
   (UDP/TCP 47624, TCP/UDP 2300–2400), vor allem im Profil „Öffentlich“.
4. **DirectPlay fehlt.** Auf Windows 10/11 ist DirectPlay ein optionales
   Windows-Feature, das standardmäßig aus ist.

## Was der Patch macht

| Problem | Lösung im Patch |
|---|---|
| Broadcast geht über den falschen Adapter | Die Spielsuche wird zusätzlich als *gerichteter* Broadcast über **jeden** aktiven Adapter gesendet. |
| Broadcast erreicht den Host nicht (VPN, Subnetze, WLAN-Isolation) | **TCP-Nachbarsuche** (Port 47630): Jede Patch-Installation betreibt einen kleinen TCP-Dienst. Beim Suchen werden die eigenen Subnetze (max. /22, sonst das eigene /24) und feste `Peers=` abgefragt. Gefundene Mitspieler tauschen ihre Peer-Listen aus (Gossip). Die DirectPlay-Spielsuche geht dann **direkt (Unicast)** an jeden gefundenen PC. |
| DirectPlay meldet per UPnP die öffentliche Router-Adresse statt der LAN-Adresse (Router leitet sie nicht ins LAN zurück) | DirectPlays NAT-Helfer (`dpnathlp.dll`) wird blockiert (`BlockNATHelp=1`), damit die echte LAN-Adresse gilt. Das Log warnt, falls trotzdem eine fremde Adresse gemeldet wird. |
| Spiel meldet die IP eines virtuellen Adapters | Eigene Adressen werden sortiert: echte LAN-Karte mit Gateway zuerst, virtuelle Adapter zuletzt (`PreferredIP=` erzwingt eine Adresse). |
| Verzögerungen / Abbrüche bei laufender Partie | `TCP_NODELAY` (kein Nagle-Puffer) und TCP-Keepalive auf DirectPlays Spielverbindungen. |
| Firewall / DirectPlay deaktiviert | `install.bat` aktiviert DirectPlay, legt Firewall-Regeln an und warnt bei „Öffentlich“-Netzprofil. |

Technisch ist `dplayx.dll` ein Proxy: Er reicht alle Aufrufe an das echte
DirectPlay aus `System32`/`SysWOW64` durch und setzt Import-Hooks in DirectPlays
TCP/IP-Provider (`sendto`/`WSASendTo`, `socket`/`accept`, `gethostbyname`).
Das Netzwerkprotokoll des Spiels bleibt **unverändert**. Spieler ohne Patch
können weiterhin mitspielen, sobald sie gefunden werden (sie selbst finden
aber nur über den normalen Weg).

## Installation

Fertige Pakete gibt es auf der Release-Seite:

| System | Paket | Anleitung |
|---|---|---|
| Windows 10 / 11 | `SC-LANPatch-<Version>-Windows.zip` | [docs/ANLEITUNG-Windows.md](docs/ANLEITUNG-Windows.md) |
| Linux (Proton / Wine / Steam) | `SC-LANPatch-<Version>-Linux.tar.gz` | [docs/ANLEITUNG-Linux.md](docs/ANLEITUNG-Linux.md) |

**Kurzfassung Windows:** ZIP entpacken → `install.bat` per Rechtsklick
„Als Administrator ausführen“ → Spiel starten → Mehrspieler → TCP/IP →
IP-Feld leer lassen.

**Kurzfassung Linux:**

```bash
tar xzf SC-LANPatch-*-Linux.tar.gz && cd SC-LANPatch-*-Linux
./sclan-linux.sh install --game "/pfad/zu/Stronghold Crusader Extreme HD"
./sclan-linux.sh start
```

Das Skript legt ein Proton-Prefix an und installiert Microsofts DirectPlay
hinein. Das ist nötig, weil Wines eigenes DirectPlay keine Spiele hosten kann.
Außerdem setzt es die DLL-Overrides. Für die Steam-Version gibt es
`--steam-appid`, für Wine/Lutris/Heroic `--runner wine`.

Der Patch muss auf **allen** Mitspieler-PCs installiert sein. Windows- und
Linux-Spieler können gemischt spielen.

### Über das Internet / VPN (Tailscale, ZeroTier, Hamachi …)

In `sclanpatch.ini` die VPN-Adressen der Mitspieler eintragen:

```ini
Peers=100.101.102.103, 100.64.0.7
```

Es reicht, wenn **eine** Seite die andere kennt. Gefundene Peers werden
weitergereicht.

## Konfiguration

Alles in `sclanpatch.ini` neben der Spiel-EXE (dokumentiert in der Datei).
Jeder Wert lässt sich auch per Umgebungsvariable `SCLAN_<Schlüssel>`
überschreiben (z.B. `SCLAN_Peers=10.0.0.5`). Das Protokoll steht in
`sclanpatch.log`: welche Adapter erkannt wurden, welche Peers gefunden wurden
und ob die Hooks aktiv sind.

## UI-Skalierung (hohe Auflösungen)

Das Spiel läuft auch in 1920×1080, 2560×1440 usw. Die Karte nutzt dann die volle
Auflösung, aber Leiste und Menüs bleiben in ihrer Originalgröße und werden
winzig. Mit `UIScale=` in `sclanpatch.ini` vergrößert der Patch genau diese
Bereiche, die Karte bleibt scharf:

```ini
UIScale=1.5     ; 1 = aus, 1.5, 2, 2.5 ... (auch "1,5" geht)
```

| Bereich | was vergrößert wird |
|---|---|
| Menüs | die Menüseite in der Bildmitte (1024×768 mit Steinrahmen, oder der 800×600-Kern, wenn die ganze Seite nicht passt) |
| Im Spiel | untere Leiste samt Schreiber und Minimap |
| Dialoge im Spiel | Esc-Menü, Optionen, Speichern/Laden, Rückfragen … |

Mausklicks in vergrößerten Bereichen werden auf die Originalposition
umgerechnet. Der Faktor wird pro Bereich automatisch so weit verkleinert, dass
alles auf den Bildschirm passt (z.B. Menüs auf 1280×720 höchstens 1.2x).
Ganzzahlige Faktoren (2, 3) werden pixelgenau scharf vergrößert, krumme (1.5)
weich interpoliert. Funktioniert mit `Stronghold Crusader.exe` und
`Stronghold_Crusader_Extreme.exe`; die EXE wird dabei nicht verändert. Die
Skalierung ist unabhängig vom LAN-Teil, `Enabled=0` schaltet sie nicht ab.

Unterstützte Spielauflösungen (fest im Spiel, 16 Bit): 800×600, 1024×600,
1024×768, 1280×720, 1280×1024, 1360×768, 1366×768, 1440×900, 1600×900,
1600×1200, 1680×1050, 1920×1080, 1920×1200, 2560×1440, 2560×1600.
Empfehlung: native Auflösung des Monitors wählen und `UIScale` passend setzen,
z.B. 1920×1080 → 1.5, 2560×1440 → 2. 4K kennt das Spiel nicht; dort 1920×1080
(Monitor skaliert 2x) oder 2560×1440 wählen.

Nicht vergrößert werden kleine Einblendungen direkt über der Karte
(Rohstoffliste oben links, Taktik-Leiste rechts in Extreme, Tooltips).

## Bauen

Benötigt MinGW-w64 (i686). Das Spiel ist ein 32-Bit-Windows-Programm, daher
ist der Patch auf beiden Systemen dieselbe 32-Bit-DLL.

```bash
make            # build/dplayx.dll + build/dptest.exe
make dist       # build/SC-LANPatch-<ver>-Windows.zip + build/SC-LANPatch-<ver>-Linux.tar.gz
```

Unter Windows: MSYS2 installieren, die Shell „MSYS2 MINGW32“ öffnen,
`pacman -S make zip mingw-w64-i686-gcc` und dann `make CROSS=` bzw. `make dist CROSS=`.

### Aufbau

```
src/                  Patch (dplayx.dll-Proxy, Hooks, TCP-Nachbarsuche, UI-Skalierung)
packaging/windows/    install.bat, uninstall.bat, firewall.bat
packaging/linux/      sclan-linux.sh (install / start / status / uninstall)
packaging/common/     sclanpatch.ini
docs/                 Anleitungen Windows / Linux
tools/dptest.c        DirectPlay-Testprogramm (verhält sich wie das Spiel)
test/                 Linux-Testlabor (Network-Namespaces + Proton, headless)
```

## Testen ohne zweiten PC (Linux)

`test/netlab.sh` baut aus unprivilegierten Network-Namespaces ein virtuelles
LAN mit zwei Proton-Instanzen. Es braucht kein root. Die Instanzen laufen
unsichtbar unter Xvfb, der Ton geht auf eine Null-Sink.

```bash
test/get-native-dplay.sh                  # Microsoft-DirectPlay aus dem winetricks-Cache
test/netlab.sh flat      on               # gleiches Subnetz
test/netlab.sh multihome off|on           # Client mit zusätzlichem "VPN"-Adapter als Default-Route
test/netlab.sh routed    off|on           # Host und Client in verschiedenen Subnetzen
GAME_INTERACTIVE=1 test/netlab.sh multihome on game   # echtes Spiel, Displays :87 / :88
```

`dptest.exe` (aus `tools/dptest.c`) nutzt DirectPlay genau wie das Spiel
(gleiche Interfaces, Adressaufbau und Session-Flags `0xA044`).

Ergebnisse (Proton GE 11-12 + MS DirectPlay):

| Szenario | ohne Patch | mit Patch |
|---|---|---|
| flat | gefunden + beigetreten | gefunden + beigetreten |
| multihome | **nicht gefunden** | gefunden + beigetreten |
| routed (`Peers=`) | **nicht gefunden** | gefunden + beigetreten |

`test/uilab.sh` startet das Spiel einzeln und unsichtbar (Xvfb, Ton auf einer
Null-Sink) zum Prüfen der UI-Skalierung:

```bash
UISCALE=1.5 test/uilab.sh start extreme 1920x1080   # oder classic, andere Größe
test/uilab.sh click 955 500                          # Klick, shot NAME = Screenshot
test/uilab.sh stop
```

Siehe auch [PLAYTEST.md](PLAYTEST.md) für die Prüfliste beim echten Anspielen.

## Lizenz

Der Patch-Code steht unter der [GNU GPL v3](LICENSE). Stronghold Crusader gehört
FireFly Studios. Dies ist ein inoffizielles Fan-Projekt ohne Verbindung zu FireFly.
Microsoft DirectPlay wird nicht mitgeliefert.
