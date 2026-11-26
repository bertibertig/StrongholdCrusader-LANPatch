# Playtest-Prüfliste – SC LAN Patch

Beim Anspielen mit echten PCs prüfen. Format: `[ ]` offen, `[x]` bestanden (mit Datum).

## Installation (Windows 10)
- [ ] `install.bat` als Admin: DirectPlay wird aktiviert, `dplayx.dll` + `sclanpatch.ini` liegen im Spielordner, Firewall-Regeln „SC LAN Patch – …“ existieren (`wf.msc`) — noch nicht committet
- [ ] Hinweis bei Netzwerkprofil „Öffentlich“ erscheint — noch nicht committet
- [ ] `uninstall.bat` entfernt DLL und Firewall-Regeln — noch nicht committet

## Finden & Beitreten
- [ ] Zwei Win10-PCs im selben LAN, IP-Feld leer: Client findet Host sofort — noch nicht committet
- [ ] PC mit VirtualBox/Hyper-V/VPN-Adapter als Client UND als Host: wird gefunden — noch nicht committet
- [ ] Über Tailscale/ZeroTier mit `Peers=<IP>` auf nur einer Seite: beide finden sich — noch nicht committet
- [ ] 3+ Spieler: dritter PC findet Host über Gossip (Peers des anderen) — noch nicht committet
- [ ] Mitspieler ohne Patch kann einem gepatchten Host weiterhin beitreten — noch nicht committet
- [ ] Anzeige „Eure IP“ im Service-Provider-Dialog zeigt die echte LAN-Adresse zuerst — noch nicht committet
- [x] Linux-Lab (Proton, multihome): Client findet Session und sitzt mit Host in der Lobby (2026-11-26) — noch nicht committet

## Stabilität im Spiel
- [ ] Partie ≥ 30 min zu zweit ohne Verbindungsabbruch/Desync — noch nicht committet
- [ ] Partie mit 4+ Spielern: kein spürbares Einheiten-Lag (TCP_NODELAY) — noch nicht committet
- [ ] Spiel beenden → neue Partie hosten/beitreten ohne Neustart (DPWSockX wird neu geladen, Hooks greifen wieder; siehe `sclanpatch.log`) — noch nicht committet

## Log prüfen
- [ ] `sclanpatch.log` enthält „patched N imports in dpwsockx.dll“ und „peer found: …“ — noch nicht committet
