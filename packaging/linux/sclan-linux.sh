#!/usr/bin/env bash
# =====================================================================
#  SC LAN Patch for Linux (Proton / Wine)
#
#    ./sclan-linux.sh install [options]   patch game + prefix
#    ./sclan-linux.sh start   [options]   start the game with the patch
#    ./sclan-linux.sh status              show configuration and log
#    ./sclan-linux.sh uninstall           remove the patch
#
#  install options
#    --game DIR          game folder (contains Stronghold_Crusader_Extreme.exe)
#    --runner R          proton (default) | wine | steam
#    --proton DIR        Proton build to use (default: newest GE-Proton / Steam Proton)
#    --prefix DIR        proton: compatdata dir (contains pfx/), wine: WINEPREFIX
#                        default proton prefix: ~/.local/share/sclanpatch/prefix
#    --steam-appid ID    runner steam: use steamapps/compatdata/ID of the Steam game
#    --peers LIST        write Peers= into sclanpatch.ini (VPN/Tailscale IPs)
#    --redist FILE       use this directx_feb2010_redist.exe instead of downloading
#
#  start options
#    --classic           start "Stronghold Crusader.exe" instead of the Extreme exe
#    --headless          invisible (Xvfb) with audio on a null sink - for tests
# =====================================================================
set -euo pipefail

VERSION=1.0.0
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
CONF_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/sclanpatch"
CONF="${SCLAN_CONF:-$CONF_DIR/config}"
DATA="${SCLAN_DATA:-${XDG_DATA_HOME:-$HOME/.local/share}/sclanpatch}"

REDIST_NAME=directx_feb2010_redist.exe
REDIST_URL=https://download.microsoft.com/download/E/E/1/EE17FF74-6C45-4575-9CF4-7FC2597ACD18/directx_feb2010_redist.exe
REDIST_SHA=f6d191e89a963d7cca34f169d30f49eab99c1ed3bb92da73ec43617caaa1e93f
DP_DLLS=(dplayx.dll dpwsockx.dll dpmodemx.dll dpnhupnp.dll dpnhpast.dll dplaysvr.exe)
OVERRIDES="dplayx,dpwsockx,dpmodemx,dpnhupnp,dpnhpast,dplaysvr.exe=n,b"

c_info=$'\e[1;34m'; c_ok=$'\e[1;32m'; c_warn=$'\e[1;33m'; c_err=$'\e[1;31m'; c_off=$'\e[0m'
[[ -t 1 ]] || { c_info=; c_ok=; c_warn=; c_err=; c_off=; }
info() { echo "${c_info}==>${c_off} $*"; }
ok()   { echo "${c_ok} ok${c_off} $*"; }
warn() { echo "${c_warn}!!${c_off}  $*" >&2; }
die()  { echo "${c_err}error:${c_off} $*" >&2; exit 1; }

# ------------------------------------------------------------- helpers

has_game() { [[ -f "$1/Stronghold_Crusader_Extreme.exe" || -f "$1/Stronghold Crusader.exe" ]]; }

steam_libraries() {
    local vdf
    for vdf in "$HOME/.local/share/Steam/steamapps/libraryfolders.vdf" \
               "$HOME/.steam/steam/steamapps/libraryfolders.vdf" \
               "$HOME/.var/app/com.valvesoftware.Steam/data/Steam/steamapps/libraryfolders.vdf"; do
        [[ -f "$vdf" ]] || continue
        dirname "$vdf"
        grep -oP '"path"\s+"\K[^"]+' "$vdf" | sed 's|$|/steamapps|'
    done | sort -u
}

find_game() {
    local d lib
    local candidates=(
        "$PWD"
        "$HERE/.."
        "$HOME/GOG Games/Stronghold Crusader Extreme HD"
        "$HOME/Games/gog/stronghold-crusader-extreme-hd/drive_c/GOG Games/Stronghold Crusader Extreme HD"
        "$HOME/Games/Heroic/Stronghold Crusader Extreme HD"
        "$HOME/Games/Heroic/Stronghold Crusader HD"
    )
    for d in "${candidates[@]}"; do has_game "$d" && { readlink -f "$d"; return; }; done
    while read -r lib; do
        for d in "$lib/common/Stronghold Crusader Extreme" "$lib/common/Stronghold Crusader" \
                 "$lib/common/Stronghold Crusader HD"; do
            has_game "$d" && { readlink -f "$d"; return; }
        done
    done < <(steam_libraries)
    # last resort: search common game locations (bounded depth)
    find "$HOME/Games" "$HOME/GOG Games" -maxdepth 6 -name Stronghold_Crusader_Extreme.exe 2>/dev/null \
        | head -1 | xargs -r -d '\n' dirname
}

find_proton() {
    local d best=""
    for d in "$HOME/.local/share/Steam/compatibilitytools.d" "$HOME/.steam/root/compatibilitytools.d"; do
        [[ -d "$d" ]] || continue
        best=$(ls -d "$d"/GE-Proton* 2>/dev/null | sort -V | tail -1)
        [[ -n "$best" && -x "$best/proton" ]] && { echo "$best"; return; }
    done
    while read -r lib; do
        for d in "$lib/common/Proton - Experimental" "$lib/common/Proton 10.0" "$lib/common/Proton 9.0"; do
            [[ -x "$d/proton" ]] && { echo "$d"; return; }
        done
    done < <(steam_libraries)
}

steam_root() {
    local d
    for d in "$HOME/.local/share/Steam" "$HOME/.steam/steam" "$HOME/.var/app/com.valvesoftware.Steam/data/Steam"; do
        [[ -d "$d" ]] && { echo "$d"; return; }
    done
    echo "$HOME/.steam/steam"
}

load_conf() {
    [[ -f "$CONF" ]] || die "not installed yet - run: $0 install"
    # shellcheck disable=SC1090
    source "$CONF"
}

# run a windows program inside the configured prefix
run_win() {
    case "$RUNNER" in
        proton|steam)
            STEAM_COMPAT_DATA_PATH="$PREFIX" \
            STEAM_COMPAT_CLIENT_INSTALL_PATH="${STEAM_COMPAT_CLIENT_INSTALL_PATH:-$(steam_root)}" \
            WINEDLLOVERRIDES="$OVERRIDES${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}" \
                "$PROTON/proton" run "$@" ;;
        wine)
            WINEPREFIX="$PREFIX" WINEDLLOVERRIDES="$OVERRIDES${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}" \
                "${WINE:-wine}" "$@" ;;
    esac
}

stop_prefix() {
    case "$RUNNER" in
        proton|steam) WINEPREFIX="$PREFIX/pfx" "$PROTON/files/bin/wineserver" -k 2>/dev/null \
                      || WINEPREFIX="$PREFIX/pfx" "$PROTON/dist/bin/wineserver" -k 2>/dev/null || true ;;
        wine)         WINEPREFIX="$PREFIX" "${WINESERVER:-wineserver}" -k 2>/dev/null || true ;;
    esac
}

winedir() { # drive_c of the prefix
    case "$RUNNER" in proton|steam) echo "$PREFIX/pfx/drive_c" ;; wine) echo "$PREFIX/drive_c" ;; esac
}

get_native_dplay() {
    local out="$DATA/native-dplay" redist="${1:-}" tmp
    if [[ -f "$out/dpwsockx.dll" ]]; then echo "$out"; return; fi
    command -v cabextract >/dev/null || die "cabextract missing (Arch: pacman -S cabextract, Debian/Ubuntu: apt install cabextract, Fedora: dnf install cabextract)"
    if [[ -z "$redist" ]]; then
        for redist in "$HOME/.cache/winetricks/directx9/$REDIST_NAME" "$DATA/$REDIST_NAME" ""; do
            [[ -n "$redist" && -f "$redist" ]] && break
        done
    fi
    if [[ -z "$redist" || ! -f "$redist" ]]; then
        redist="$DATA/$REDIST_NAME"
        info "Downloading Microsoft DirectX redistributable (~95 MB, contains DirectPlay)" >&2
        mkdir -p "$DATA"
        curl -fL --progress-bar -o "$redist.part" "$REDIST_URL" >&2 || die "download failed"
        mv "$redist.part" "$redist"
    fi
    echo "$REDIST_SHA  $redist" | sha256sum -c --quiet - >&2 || die "checksum mismatch for $redist"
    tmp="$(mktemp -d)"
    cabextract -q -L -d "$tmp" -F dxnt.cab "$redist" >/dev/null 2>&1
    mkdir -p "$out"
    local args=() f
    for f in "${DP_DLLS[@]}"; do args+=(-F "$f"); done
    cabextract -q -L -d "$out" "${args[@]}" "$tmp/dxnt.cab" >/dev/null 2>&1
    rm -rf "$tmp"
    [[ -f "$out/dpwsockx.dll" ]] || die "could not extract DirectPlay from $redist"
    echo "$out"
}

firewall_hint() {
    if command -v ufw >/dev/null && ufw status 2>/dev/null | grep -q "Status: active"; then
        warn "ufw is active. Allow the LAN ports once:"
        echo "      sudo ufw allow 47624/udp && sudo ufw allow 47624,47630/tcp && sudo ufw allow 2300:2400/tcp && sudo ufw allow 2300:2400/udp"
    elif command -v firewall-cmd >/dev/null && firewall-cmd --state >/dev/null 2>&1; then
        warn "firewalld is active. Allow the LAN ports once:"
        echo "      sudo firewall-cmd --permanent --add-port=47624/udp --add-port=47624/tcp --add-port=47630/tcp --add-port=2300-2400/tcp --add-port=2300-2400/udp && sudo firewall-cmd --reload"
    fi
}

# ------------------------------------------------------------ commands

cmd_install() {
    local GAME="" RUNNER=proton PROTON="" PREFIX="" APPID="" PEERS="" REDIST=""
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --game) GAME="$2"; shift 2 ;;
            --runner) RUNNER="$2"; shift 2 ;;
            --proton) PROTON="$2"; shift 2 ;;
            --prefix) PREFIX="$2"; shift 2 ;;
            --steam-appid) APPID="$2"; RUNNER=steam; shift 2 ;;
            --peers) PEERS="$2"; shift 2 ;;
            --redist) REDIST="$2"; shift 2 ;;
            -h|--help) sed -n '2,25p' "$0"; exit 0 ;;
            *) die "unknown option $1" ;;
        esac
    done

    info "SC LAN Patch $VERSION - Linux installer"
    [[ -f "$HERE/dplayx.dll" ]] || die "dplayx.dll not found next to this script"

    [[ -n "$GAME" ]] || GAME="$(find_game || true)"
    [[ -n "$GAME" ]] || die "game folder not found - pass --game \"/path/to/Stronghold Crusader Extreme HD\""
    GAME="$(readlink -f "$GAME")"
    has_game "$GAME" || die "no Stronghold Crusader exe in $GAME"
    ok "game: $GAME"

    case "$RUNNER" in
        proton)
            [[ -n "$PROTON" ]] || PROTON="$(find_proton || true)"
            [[ -x "$PROTON/proton" ]] || die "no Proton found - pass --proton DIR or use --runner wine"
            [[ -n "$PREFIX" ]] || PREFIX="$DATA/prefix" ;;
        steam)
            [[ -n "$APPID" ]] || die "--runner steam needs --steam-appid ID"
            [[ -n "$PROTON" ]] || PROTON="$(find_proton || true)"
            [[ -x "$PROTON/proton" ]] || die "no Proton found - pass --proton DIR"
            if [[ -z "$PREFIX" ]]; then
                local lib
                while read -r lib; do
                    [[ -d "$lib/compatdata/$APPID/pfx" ]] && { PREFIX="$lib/compatdata/$APPID"; break; }
                done < <(steam_libraries)
            fi
            [[ -d "$PREFIX/pfx" ]] || die "Steam prefix for app $APPID not found - start the game once from Steam" ;;
        wine)
            command -v "${WINE:-wine}" >/dev/null || die "wine not found"
            [[ -n "$PREFIX" ]] || PREFIX="${WINEPREFIX:-$DATA/wineprefix}" ;;
        *) die "unknown runner $RUNNER" ;;
    esac
    PREFIX="$(mkdir -p "$PREFIX" && readlink -f "$PREFIX")"
    ok "runner: $RUNNER${PROTON:+ ($(basename "$PROTON"))}, prefix: $PREFIX"

    info "Copying patch into the game folder"
    cp -f "$HERE/dplayx.dll" "$GAME/dplayx.dll"
    if [[ ! -f "$GAME/sclanpatch.ini" ]]; then
        cp -f "$HERE/sclanpatch.ini" "$GAME/sclanpatch.ini"
    else
        ok "keeping existing sclanpatch.ini"
    fi
    if [[ -n "$PEERS" ]]; then
        sed -i "s|^Peers=[^\r]*|Peers=$PEERS|" "$GAME/sclanpatch.ini"
        ok "Peers=$PEERS"
    fi

    if [[ ! -d "$(winedir)/windows" ]]; then
        info "Creating prefix (takes a minute)"
        run_win wineboot -u >/dev/null 2>&1 || true
        stop_prefix
        [[ -d "$(winedir)/windows" ]] || die "prefix creation failed"
    fi

    info "Installing Microsoft DirectPlay into the prefix (Wine's own cannot host games)"
    local dp sys
    dp="$(get_native_dplay "$REDIST")"
    sys="$(winedir)/windows/syswow64"
    [[ -d "$sys" ]] || sys="$(winedir)/windows/system32"
    cp -f "$dp"/* "$sys/"
    ok "DirectPlay -> $sys"

    info "Setting DLL overrides in the prefix registry"
    local d
    for d in dplayx dpwsockx dpmodemx dpnhupnp dpnhpast dplaysvr.exe; do
        run_win reg add 'HKCU\Software\Wine\DllOverrides' /v "$d" /d native,builtin /f >/dev/null 2>&1 || true
    done
    stop_prefix
    ok "dplayx & co = native,builtin"

    mkdir -p "$(dirname "$CONF")"
    cat > "$CONF" <<EOF
# SC LAN Patch (written by sclan-linux.sh install)
GAME=$(printf %q "$GAME")
RUNNER=$RUNNER
PROTON=$(printf %q "$PROTON")
PREFIX=$(printf %q "$PREFIX")
APPID=$APPID
EOF
    ok "config: $CONF"
    firewall_hint

    echo
    if [[ "$RUNNER" == steam ]]; then
        echo "Done. Start the game normally from Steam - the overrides live in the prefix."
    else
        echo "Done. Start the game with:"
        echo "    $HERE/sclan-linux.sh start"
    fi
    echo "Multiplayer -> TCP/IP -> leave the IP field empty. Log: $GAME/sclanpatch.log"
}

cmd_start() {
    local EXE=Stronghold_Crusader_Extreme.exe HEADLESS=""
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --classic) EXE="Stronghold Crusader.exe"; shift ;;
            --headless) HEADLESS=1; shift ;;
            *) die "unknown option $1" ;;
        esac
    done
    load_conf
    [[ -f "$GAME/$EXE" ]] || die "$GAME/$EXE not found"
    [[ -f "$GAME/dplayx.dll" ]] || warn "dplayx.dll missing in game folder - run install again"

    if [[ "$RUNNER" == steam && -z "$HEADLESS" ]]; then
        info "Starting through Steam (app $APPID)"
        exec steam "steam://rungameid/$APPID"
    fi

    SCLAN_XPID="" SCLAN_SINKMOD=""   # globals: the EXIT trap runs after this function returns
    if [[ -n "$HEADLESS" ]]; then
        local n=90
        while [[ -e /tmp/.X11-unix/X$n || -e /tmp/.X$n-lock ]]; do n=$((n + 1)); done
        Xvfb ":$n" -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 & SCLAN_XPID=$!
        export DISPLAY=":$n"
        if command -v pactl >/dev/null; then
            SCLAN_SINKMOD=$(pactl load-module module-null-sink sink_name="sclan_null_$n" 2>/dev/null || true)
            export PULSE_SINK="sclan_null_$n"
        fi
        trap 'stop_prefix; [[ -n "${SCLAN_XPID:-}" ]] && kill "$SCLAN_XPID" 2>/dev/null; [[ -n "${SCLAN_SINKMOD:-}" ]] && pactl unload-module "$SCLAN_SINKMOD" 2>/dev/null; true' EXIT INT TERM
        info "headless on DISPLAY=$DISPLAY, audio -> null sink"
    fi

    info "Starting $EXE ($RUNNER)"
    cd "$GAME"
    if [[ -n "$HEADLESS" ]]; then
        # background + wait, so that SIGTERM/Ctrl+C runs the cleanup trap at once
        run_win "./$EXE" &
        wait $! || true
    else
        run_win "./$EXE"
    fi
}

cmd_status() {
    load_conf
    echo "config : $CONF"
    echo "game   : $GAME"
    echo "runner : $RUNNER ${PROTON:+($PROTON)}"
    echo "prefix : $PREFIX"
    [[ -f "$GAME/dplayx.dll" ]] && echo "patch  : installed" || echo "patch  : MISSING"
    local sys
    sys="$(winedir)/windows/syswow64"; [[ -d "$sys" ]] || sys="$(winedir)/windows/system32"
    cmp -s "$sys/dpwsockx.dll" "$DATA/native-dplay/dpwsockx.dll" 2>/dev/null \
        && echo "dplay  : Microsoft DirectPlay in prefix" || echo "dplay  : NOT installed (run install)"
    if [[ -f "$GAME/sclanpatch.log" ]]; then
        echo "--- last lines of sclanpatch.log"
        tail -n 15 "$GAME/sclanpatch.log"
    fi
}

cmd_uninstall() {
    load_conf
    rm -f "$GAME/dplayx.dll" "$GAME/sclanpatch.log"
    local d
    for d in dplayx dpwsockx dpmodemx dpnhupnp dpnhpast dplaysvr.exe; do
        run_win reg delete 'HKCU\Software\Wine\DllOverrides' /v "$d" /f >/dev/null 2>&1 || true
    done
    stop_prefix
    rm -f "$CONF"
    ok "patch removed (sclanpatch.ini kept in $GAME)"
}

case "${1:-}" in
    install)   shift; cmd_install "$@" ;;
    start)     shift; cmd_start "$@" ;;
    status)    shift; cmd_status "$@" ;;
    uninstall) shift; cmd_uninstall "$@" ;;
    -v|--version) echo "$VERSION" ;;
    *) sed -n '2,25p' "$0"; exit 1 ;;
esac
