# Playtest-Prüfliste – SC LAN Patch

Beim Anspielen mit echten PCs prüfen. Format: `[ ]` offen, `[x]` bestanden (mit Datum).

## Installation (Windows 10)
- [ ] `install.bat` als Admin: DirectPlay wird aktiviert, `dplayx.dll` + `sclanpatch.ini` liegen im Spielordner, Firewall-Regeln „SC LAN Patch – …“ existieren (`wf.msc`) — 6024c23
- [ ] Hinweis bei Netzwerkprofil „Öffentlich“ erscheint — 6024c23
- [ ] `uninstall.bat` entfernt DLL und Firewall-Regeln — 6024c23

## Linux (sclan-linux.sh)
- [ ] `sclan-linux.sh install --game …` auf einem frischen System: Prefix wird angelegt, DirectPlay geladen + installiert, `status` zeigt alles grün — 958baaa
- [ ] `sclan-linux.sh start`: Spiel startet mit Ton und Fenster, Mehrspieler → Host funktioniert — 958baaa
- [ ] Steam-Version: `install --steam-appid <id>`, Start über Steam, Host/Beitreten klappt — 958baaa
- [ ] Linux-Host + Windows-Client (und umgekehrt) finden sich und spielen zusammen — 958baaa
- [x] Headless (`start --headless`): Spiel hostet, Ton in Null-Sink, SIGTERM räumt Xvfb/Sink/Wine auf (2026-11-26) — 958baaa

## Finden & Beitreten
- [ ] Zwei Win10-PCs im selben LAN, IP-Feld leer: Client findet Host sofort — 6024c23
- [ ] PC mit VirtualBox/Hyper-V/VPN-Adapter als Client UND als Host: wird gefunden — 6024c23
- [ ] Über Tailscale/ZeroTier mit `Peers=<IP>` auf nur einer Seite: beide finden sich — 6024c23
- [ ] 3+ Spieler: dritter PC findet Host über Gossip (Peers des anderen) — 6024c23
- [ ] Mitspieler ohne Patch kann einem gepatchten Host weiterhin beitreten — 6024c23
- [ ] Anzeige „Eure IP“ im Service-Provider-Dialog zeigt die echte LAN-Adresse zuerst — 6024c23
- [x] Linux-Lab (Proton, multihome): Client findet Session und sitzt mit Host in der Lobby (2026-11-26) — 6024c23
- [x] Router mit UPnP (ohne NAT-Hairpinning): Client mit `BlockNATHelp=1` findet Win10-Host; Log zeigt „blocked DirectPlay NAT helper“ und „reply address <source address>“ (2026-11-27) — 0e938c3
- [ ] Router mit UPnP: Beitreten klappt, wenn BEIDE PCs `BlockNATHelp=1` haben (Host-Antwort ohne öffentliche Adresse) — 0e938c3
- [x] Router mit UPnP ohne Hairpinning, Host mit altem Patch (meldet öffentliche Adresse): Win10-Client mit `HairpinFix=1` tritt im echten Spiel bei; Log zeigt „hairpin: … is a UPnP mapping -> connecting to 192.168.0.39:2300“ (2026-11-27) — 0e938c3
- [x] Kein Absturz beim Öffnen von Mehrspieler (IAT-Patch-Race zwischen Watcher- und Hauptthread behoben) (2026-11-27) — 0e938c3

## Stabilität im Spiel
- [ ] Partie ≥ 30 min zu zweit ohne Verbindungsabbruch/Desync — 6024c23
- [ ] Partie mit 4+ Spielern: kein spürbares Einheiten-Lag (TCP_NODELAY) — 6024c23
- [ ] Spiel beenden → neue Partie hosten/beitreten ohne Neustart (DPWSockX wird neu geladen, Hooks greifen wieder; siehe `sclanpatch.log`) — 6024c23

## UI-Skalierung (`UIScale=` in sclanpatch.ini)
- [x] Extreme, 1920×1080, `UIScale=1.5`: Menüs, Leiste + Schreiber, Esc-Dialog vergrößert, Klicks treffen (Linux-Lab, 2026-11-27) — b1bcb89
- [x] Classic, 1920×1080, 1.5: Menüs, Burgenbau-Leiste, Esc-Dialog (Linux-Lab, 2026-11-27) — b1bcb89
- [x] Extreme, 1920×1200, `UIScale=2`: Menüs 2x scharf, Leiste 2x, Esc-Dialog (Linux-Lab, 2026-11-27) — b1bcb89
- [x] Classic, 1280×720, 1.5: Menükern auf 1.2x und Leiste auf 1.36x begrenzt, alles sichtbar (Linux-Lab, 2026-11-27) — b1bcb89
- [ ] Windows 10 echt, Vollbild 1920×1080, `UIScale=1.5`: flüssig, keine Bildfehler über der Leiste — b1bcb89
- [ ] 2560×1440 mit `UIScale=2` auf echtem Monitor (im Lab nicht möglich: Xvfb bietet den Modus nicht) — b1bcb89
- [x] Windows 10 echt, Extreme, Vollbild 1920×1080, `UIScale=1.25`: Gebäude anklicken (Bergfried, Söldnerposten) → Leiste vergrößert, Karte nicht gezoomt, Zeltbögen des Söldnerpostens vollständig (2026-11-27) — 9abb126
- [x] Win10 Extreme 1920×1080 `UIScale=1.25`: Kaserne (Zinnen, Türme) vollständig vergrößert; Baumeistergilde, Tunnelbauergilde, Stall, Waffenkammer haben die normale Leiste — uncommitted
- [ ] Weitere Gebäude-Leisten (Markt, Lager, Kornspeicher …): nichts ragt abgeschnitten über die Leiste
- [ ] Alle Leisten-Untermenüs, Gebäude-Infofenster, Schreiber-Bericht, Einheiten-Auswahl: Klicks treffen — b1bcb89
- [ ] Weitere Dialoge (Optionen, Speichern/Laden mit Liste, Missionsziele, Sieg/Niederlage): vergrößert und bedienbar — b1bcb89
- [ ] Kartenscrollen am unteren Rand über der vergrößerten Leiste, Auswahlrahmen ziehen bis in die Leiste — b1bcb89
- [ ] Auflösung im Spiel wechseln (Optionen): Skalierung passt sich an, keine Reste — b1bcb89
- [ ] Alt+Tab raus und zurück im Vollbild — b1bcb89
- [ ] Fenstermodus (falls das Spiel ihn anbietet) — Code vorhanden, ungetestet — b1bcb89
- [ ] Intro-/Kampagnen-Videos werden nicht verzerrt/beschnitten — b1bcb89
- [ ] LAN-Partie mit `UIScale` bei einem Spieler und ohne beim anderen: kein Desync (nur Anzeige betroffen) — b1bcb89
- [ ] Karteneditor — b1bcb89

## Log prüfen
- [ ] `sclanpatch.log` enthält „patched N imports in dpwsockx.dll“ und „peer found: …“ — 6024c23
- [ ] Mit `UIScale>1`: Log enthält „ui: scene variable at …“ und „ui: game frame …“ — b1bcb89
