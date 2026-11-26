#!/usr/bin/env bash
# Extracts Microsoft's DirectPlay DLLs from the DirectX (Feb 2010) redistributable
# for the Linux test lab. Uses the winetricks cache if present; nothing is
# downloaded unless you pass --download.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/test/native-dplay"
REDIST="${REDIST:-$HOME/.cache/winetricks/directx9/directx_feb2010_redist.exe}"
SHA=f6d191e89a963d7cca34f169d30f49eab99c1ed3bb92da73ec43617caaa1e93f
if [[ ! -f "$REDIST" ]]; then
    if [[ "${1:-}" == --download ]]; then
        mkdir -p "$(dirname "$REDIST")"
        curl -fL -o "$REDIST" https://download.microsoft.com/download/E/E/1/EE17FF74-6C45-4575-9CF4-7FC2597ACD18/directx_feb2010_redist.exe
    else
        echo "missing $REDIST (run 'winetricks directplay' once or pass --download)"; exit 1
    fi
fi
echo "$SHA  $REDIST" | sha256sum -c -
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
cabextract -q -L -d "$TMP" -F dxnt.cab "$REDIST"
mkdir -p "$OUT"
cabextract -q -L -d "$OUT" -F dplaysvr.exe -F dplayx.dll -F dpwsockx.dll -F dpmodemx.dll \
    -F dpnhupnp.dll -F dpnhpast.dll "$TMP/dxnt.cab"
ls -la "$OUT"
