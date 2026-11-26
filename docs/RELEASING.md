# Release erstellen

1. Version in `src/sclan.h` (`SCLAN_VERSION`) und `packaging/linux/sclan-linux.sh` (`VERSION`) erhöhen.
2. `make clean && make all dist` → `build/SC-LANPatch-<ver>-Windows.zip`, `build/SC-LANPatch-<ver>-Linux.tar.gz`.
3. `cd build && sha256sum SC-LANPatch-*.zip SC-LANPatch-*.tar.gz > SHA256SUMS.txt`
4. Tests: `test/get-native-dplay.sh`, dann `test/netlab.sh {flat,multihome,routed} {off,on}`.
   Erwartung: mit Patch immer `RESULT: joined`, ohne Patch scheitern multihome und routed.
5. Archive prüfen (`unzip -l`, `tar tzf`): keine Spieldateien, keine Microsoft-DLLs.
6. Release-Notes nach `docs/release-notes/<version>.md` schreiben (Struktur der letzten übernehmen).
7. `gh release create <version> --verify-tag --latest --notes-file docs/release-notes/<version>.md build/SC-LANPatch-* build/SHA256SUMS.txt`
