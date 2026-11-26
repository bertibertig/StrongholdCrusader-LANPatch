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

## Stabilität im Spiel
- [ ] Partie ≥ 30 min zu zweit ohne Verbindungsabbruch/Desync — 6024c23
- [ ] Partie mit 4+ Spielern: kein spürbares Einheiten-Lag (TCP_NODELAY) — 6024c23
- [ ] Spiel beenden → neue Partie hosten/beitreten ohne Neustart (DPWSockX wird neu geladen, Hooks greifen wieder; siehe `sclanpatch.log`) — 6024c23

## Log prüfen
- [ ] `sclanpatch.log` enthält „patched N imports in dpwsockx.dll“ und „peer found: …“ — 6024c23
