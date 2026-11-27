#!/usr/bin/env bash
# Headless single-player lab for the UI scaling feature.
#
#   test/uilab.sh start [classic|extreme] [WxH]   start the game on an invisible X server
#   test/uilab.sh shot  NAME                       screenshot -> test/logs/ui/NAME.png
#   test/uilab.sh click X Y                        click at screen coordinates
#   test/uilab.sh key   KEY...                     send keys (xdotool names)
#   test/uilab.sh stop
#
# Env: UISCALE=1.5 (written to sclanpatch.ini), PROTON_DIR, DISPLAY_NUM (default 97).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROTON_DIR="${PROTON_DIR:-$HOME/.local/share/Steam/compatibilitytools.d/GE-Proton11-12-x86_64}"
GAME_DIR="${GAME_DIR:-$ROOT/game_files/Stronghold Crusader Extreme HD}"
PFX="$ROOT/test/prefixes/UI"
LOGS="$ROOT/test/logs/ui"
D="${DISPLAY_NUM:-97}"
export DISPLAY=":$D"
mkdir -p "$PFX" "$LOGS"

wine_env() {
    env STEAM_COMPAT_DATA_PATH="$PFX" STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.steam/steam" \
        WINEDLLOVERRIDES="dplayx=n,b" WINEDEBUG="${WINEDEBUG:--all}" PROTON_LOG=0 "$@"
}

case "${1:-}" in
start)
    which="${2:-extreme}" size="${3:-1920x1080}"
    exe=Stronghold_Crusader_Extreme.exe
    [[ "$which" == classic ]] && exe="Stronghold Crusader.exe"
    if [[ ! -S /tmp/.X11-unix/X$D ]]; then
        Xvfb ":$D" -screen 0 "${size}x24" -nolisten tcp >/dev/null 2>&1 &
        echo $! > "$LOGS/xvfb.pid"
        for _ in $(seq 50); do [[ -S /tmp/.X11-unix/X$D ]] && break; sleep 0.1; done
    fi
    # never play sound on the real speakers
    pactl list short sinks | grep -q sclan_ui_null || \
        pactl load-module module-null-sink sink_name=sclan_ui_null \
            sink_properties=device.description=SC-UI-Null > "$LOGS/sink.mod"
    export PULSE_SINK=sclan_ui_null
    if [[ ! -d "$PFX/pfx/drive_c" ]]; then
        wine_env "$PROTON_DIR/proton" run wineboot -u >"$LOGS/wineboot.log" 2>&1 || true
    fi
    [[ -d "$PFX/game" ]] || cp -r "$GAME_DIR" "$PFX/game"
    cp -f "$ROOT/build/dplayx.dll" "$PFX/game/"
    cp -f "$ROOT/packaging/common/sclanpatch.ini" "$PFX/game/"
    if [[ -n "${UISCALE:-}" ]]; then
        sed -i "s/^UIScale=.*/UIScale=$UISCALE/" "$PFX/game/sclanpatch.ini"
    fi
    rm -f "$PFX/game/sclanpatch.log"
    cd "$PFX/game"
    wine_env "$PROTON_DIR/proton" run "./$exe" >"$LOGS/game.log" 2>&1 &
    echo "game started on $DISPLAY ($exe, $size)"
    ;;
shot)
    import -display "$DISPLAY" -window root "$LOGS/${2:-shot}.png"
    echo "$LOGS/${2:-shot}.png"
    ;;
click)
    xdotool mousemove "$2" "$3" sleep 0.2 mousedown 1 sleep 0.15 mouseup 1
    ;;
move)
    xdotool mousemove "$2" "$3"
    ;;
key)
    shift; xdotool key --delay 150 "$@"
    ;;
stop)
    # kill the game first - unloading the null sink while it still plays
    # would move its sound to the real speakers
    WINEPREFIX="$PFX/pfx" "$PROTON_DIR/files/bin/wineserver" -k 2>/dev/null || true
    for _ in $(seq 50); do
        pgrep -f "Stronghold.*Crusader.*\.exe" >/dev/null || break
        sleep 0.2
    done
    pkill -9 -f "Stronghold.*Crusader.*\.exe" 2>/dev/null || true
    [[ -f "$LOGS/xvfb.pid" ]] && kill "$(cat "$LOGS/xvfb.pid")" 2>/dev/null || true
    rm -f "$LOGS/xvfb.pid"
    if ! pgrep -f "Stronghold.*Crusader.*\.exe" >/dev/null; then
        [[ -s "$LOGS/sink.mod" ]] && pactl unload-module "$(cat "$LOGS/sink.mod")" 2>/dev/null || true
        rm -f "$LOGS/sink.mod"
    else
        echo "game still running - null sink kept" >&2
    fi
    ;;
*) sed -n '2,10p' "$0"; exit 1 ;;
esac
