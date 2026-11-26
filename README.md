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
| Spiel meldet die IP eines virtuellen Adapters | Eigene Adressen werden sortiert: echte LAN-Karte mit Gateway zuerst, virtuelle Adapter zuletzt (`PreferredIP=` erzwingt eine Adresse). |
| Verzögerungen / Abbrüche bei laufender Partie | `TCP_NODELAY` (kein Nagle-Puffer) und TCP-Keepalive auf DirectPlays Spielverbindungen. |
| Firewall / DirectPlay deaktiviert | `install.bat` aktiviert DirectPlay, legt Firewall-Regeln an und warnt bei „Öffentlich“-Netzprofil. |

Technisch ist `dplayx.dll` ein Proxy: Er reicht alle Aufrufe an das echte
DirectPlay aus `System32`/`SysWOW64` durch und setzt Import-Hooks in DirectPlays
TCP/IP-Provider (`sendto`/`WSASendTo`, `socket`/`accept`, `gethostbyname`).
Das Netzwerkprotokoll des Spiels bleibt **unverändert**. Spieler ohne Patch
können weiterhin mitspielen, sobald sie gefunden werden (sie selbst finden
aber nur über den normalen Weg).

## Installation (Windows 10 / 11)

Auf **allen** Mitspieler-PCs:

1. Release-ZIP entpacken (oder `build/dist` nach dem Bauen).
2. `install.bat` per **Rechtsklick → Als Administrator ausführen**.
   Das Skript sucht den Spielordner selbst (GOG/Steam-Standardpfade) oder
   fragt danach. Ihr könnt den Ordner auch angeben:
   `install.bat "D:\Spiele\Stronghold Crusader Extreme HD"`
3. Spiel starten → Mehrspieler → LAN/TCP-IP, **IP-Feld leer lassen**.

Das Skript erledigt:
- DirectPlay aktivieren (`dism /online /enable-feature /featurename:DirectPlay /all`),
- `dplayx.dll` + `sclanpatch.ini` in den Spielordner kopieren,
- Firewall-Regeln für das Spiel, `dplaysvr.exe` und die Ports
  TCP 47624/47630/2300–2400 und UDP 47624/2300–2400 anlegen.

Deinstallation: `uninstall.bat` (als Administrator).

### Über das Internet / VPN (Tailscale, ZeroTier, Hamachi …)

In `sclanpatch.ini` die VPN-Adressen der Mitspieler eintragen:

```ini
Peers=100.101.102.103, 100.64.0.7
```

Es reicht, wenn **eine** Seite die andere kennt. Gefundene Peers werden
weitergereicht.

## Linux (Proton / Wine)

Wines eigenes DirectPlay kann **keine Spiele hosten** („session creation is
not yet supported“). Daher braucht ihr Microsofts DirectPlay im Prefix:

```bash
WINEPREFIX=/pfad/zum/prefix winetricks directplay
```

Für Steam/Proton entsprechend `protontricks <appid> directplay`. Danach
`dplayx.dll` + `sclanpatch.ini` in den Spielordner kopieren und die
Startoption setzen:

```
WINEDLLOVERRIDES="dplayx=n,b" %command%
```

## Konfiguration

Alles in `sclanpatch.ini` neben der Spiel-EXE (dokumentiert in der Datei).
Jeder Wert lässt sich auch per Umgebungsvariable `SCLAN_<Schlüssel>`
überschreiben (z.B. `SCLAN_Peers=10.0.0.5`). Das Protokoll steht in
`sclanpatch.log`: welche Adapter erkannt wurden, welche Peers gefunden wurden
und ob die Hooks aktiv sind.

## Bauen

Benötigt MinGW-w64 (i686):

```bash
make            # build/dplayx.dll + build/dptest.exe
make dist       # build/SC-LANPatch.zip
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

Siehe auch [PLAYTEST.md](PLAYTEST.md) für die Prüfliste beim echten Anspielen.
