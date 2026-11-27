# SC LAN Patch – Anleitung Linux (Proton / Wine)

Stronghold Crusader ist ein Windows-Spiel. Unter Linux läuft es mit Proton oder
Wine, und der Patch (`dplayx.dll`) wird dort genauso geladen wie unter Windows.
Zusätzlich braucht ihr **Microsofts DirectPlay** im Prefix: Wines eigenes
DirectPlay kann keine Spiele hosten. Das Skript erledigt alles.

## Voraussetzungen

- Eine Kopie des Spiels (GOG, Steam o.ä.).
- Proton (z.B. GE-Proton in `~/.local/share/Steam/compatibilitytools.d`, oder
  Steams eigenes Proton) **oder** Wine.
- `cabextract` und `curl`:
  - Arch: `sudo pacman -S cabextract curl`
  - Debian/Ubuntu: `sudo apt install cabextract curl`
  - Fedora: `sudo dnf install cabextract curl`

## 1. Patch herunterladen

```bash
tar xzf SC-LANPatch-*-Linux.tar.gz
cd SC-LANPatch-*-Linux
```

## 2. Installieren

### a) GOG-/Standalone-Version mit Proton (empfohlen)

```bash
./sclan-linux.sh install --game "$HOME/GOG Games/Stronghold Crusader Extreme HD"
```

Ohne `--game` sucht das Skript in den üblichen Ordnern (GOG, Heroic, Steam).
Es

- kopiert `dplayx.dll` + `sclanpatch.ini` in den Spielordner,
- legt ein eigenes Proton-Prefix an (`~/.local/share/sclanpatch/prefix`),
- lädt einmalig Microsofts DirectX-Redistributable (~95 MB, Prüfsumme wird
  geprüft) und installiert daraus DirectPlay ins Prefix (liegt es schon im
  winetricks-Cache, wird nichts geladen),
- setzt die DLL-Overrides (`dplayx`, `dpwsockx`, … = native,builtin),
- weist auf eine aktive Firewall (ufw/firewalld) hin und zeigt die nötigen Befehle.

Bestimmtes Proton wählen: `--proton ~/.local/share/Steam/compatibilitytools.d/GE-Proton11-12-x86_64`

### b) Steam-Version

Das Spiel einmal über Steam starten (damit das Prefix existiert), dann:

```bash
./sclan-linux.sh install --steam-appid <AppID>
```

Die AppID steht in der Steam-Store-URL des Spiels. Danach das Spiel normal über
Steam starten. Die Overrides stehen im Prefix, Startoptionen sind nicht nötig.

### c) Wine / Lutris / Heroic mit eigenem Wine-Prefix

```bash
./sclan-linux.sh install --runner wine --prefix ~/Games/stronghold-crusader --game "/pfad/zum/Spiel"
```

Lutris/Heroic danach wie gewohnt starten (selbes Prefix).

## 3. Starten

```bash
./sclan-linux.sh start            # Stronghold Crusader Extreme HD
./sclan-linux.sh start --classic  # Stronghold Crusader HD
```

Im Spiel: **Mehrspieler → „Internet TCP/IP Connection For DirectPlay“**.
Host: **Host** klicken. Mitspieler: **IP-Feld leer lassen → Beitreten**.

## VPN / Internet

```bash
./sclan-linux.sh install --peers "100.101.102.103,100.64.0.7"
```

oder `Peers=` in `sclanpatch.ini` im Spielordner von Hand setzen.

## Größere Bedienoberfläche (UI-Skalierung)

Bei hohen Auflösungen (z.B. 1920×1080) ist die Leiste unten sehr klein. In
`sclanpatch.ini` im Spielordner:

```ini
UIScale=1.5
```

Damit werden Leiste, Menüs und Dialoge 1,5-fach vergrößert, die Karte bleibt in
voller Auflösung. `2` = doppelt, `1` = aus. Im Spiel unter **Optionen** die
native Auflösung des Monitors wählen.

## Firewall

Nur nötig, wenn ufw oder firewalld aktiv ist (das Skript sagt es dann):

```bash
sudo ufw allow 47624/udp && sudo ufw allow 47624,47630/tcp && sudo ufw allow 2300:2400/tcp && sudo ufw allow 2300:2400/udp
```

## Status, Log, Deinstallation

```bash
./sclan-linux.sh status      # Konfiguration + letzte Zeilen von sclanpatch.log
./sclan-linux.sh uninstall   # Patch + Overrides entfernen
```

## Headless testen (ohne Fenster, ohne Ton)

```bash
./sclan-linux.sh start --headless
```

Startet das Spiel auf einem unsichtbaren Xvfb-Display, der Ton geht auf eine
PulseAudio/PipeWire-Null-Sink. Gedacht für automatisierte Tests.
