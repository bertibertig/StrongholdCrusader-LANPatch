#!/usr/bin/env bash
# Headless LAN lab for the SC LAN Patch (Linux, no root needed).
#
# Builds a virtual LAN out of unprivileged network namespaces and runs two
# Proton instances (host A, client B) in it - invisible (Xvfb) and with audio
# routed to a PulseAudio/PipeWire null sink.
#
#   test/netlab.sh <scenario> <patch:on|off> [command]
#
# Scenarios
#   flat      A 10.77.0.10/24 and B 10.77.0.20/24 on one switch. Should always work.
#   multihome B additionally has a "VPN" adapter holding the default route, so
#             DirectPlay's 255.255.255.255 broadcast leaves through the wrong
#             adapter (the classic Windows "we can't see each other" problem).
#   routed    A and B in different subnets behind a router (like Tailscale/VPN
#             or two switches). Broadcast can never work; the patch finds the
#             host via Peers= in sclanpatch.ini.
#
# command: "dptest" (default) runs build/dptest.exe host/join,
#          "game" starts the real game in both instances and takes screenshots.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCENARIO="${1:-flat}"
PATCH="${2:-on}"
CMD="${3:-dptest}"
PROTON_DIR="${PROTON_DIR:-$HOME/.local/share/Steam/compatibilitytools.d/GE-Proton11-12-x86_64}"
GAME_DIR="${GAME_DIR:-$ROOT/game_files/Stronghold Crusader Extreme HD}"
LAB="$ROOT/test/prefixes"
LOGS="$ROOT/test/logs/$SCENARIO-$PATCH-$CMD"
DISPLAY_NUM="${DISPLAY_NUM:-87}"
# Microsoft DirectPlay DLLs (Wine's builtin dpwsockx cannot host sessions).
# Extract with: test/get-native-dplay.sh
NATIVE_DP="${NATIVE_DP:-$ROOT/test/native-dplay}"

mkdir -p "$LAB" "$LOGS"

# ------------------------------------------------------------ outer part
if [[ "${NETLAB_INNER:-}" != 1 ]]; then
    # invisible X server
    XVFB_PIDS=""
    for d in "$DISPLAY_NUM" $((DISPLAY_NUM + 1)); do
        if [[ ! -S /tmp/.X11-unix/X$d ]]; then
            Xvfb ":$d" -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 &
            XVFB_PIDS="$XVFB_PIDS $!"
            for _ in $(seq 50); do [[ -S /tmp/.X11-unix/X$d ]] && break; sleep 0.1; done
        fi
    done
    trap 'kill $XVFB_PIDS 2>/dev/null || true' EXIT
    # audio -> null sink
    if ! pactl list short sinks | grep -q sclan_null; then
        NULL_MOD=$(pactl load-module module-null-sink sink_name=sclan_null \
                   sink_properties=device.description=SC-LAN-Null)
        trap 'kill $XVFB_PIDS 2>/dev/null || true; pactl unload-module '"$NULL_MOD"' || true' EXIT
    fi
    NETLAB_INNER=1 unshare --map-root-user --net "$0" "$@"
    exit $?
fi

# ------------------------------------------------- inside user+net ns
export DISPLAY=":$DISPLAY_NUM" PULSE_SINK=sclan_null
ip link set lo up
ip link add br0 type bridge && ip link set br0 up
ip link add br1 type bridge && ip link set br1 up
echo 1 > /proc/sys/net/ipv4/ip_forward 2>/dev/null || true
ip addr add 10.77.0.1/24 dev br0
ip addr add 10.78.0.1/24 dev br1

# spawn_ns NAME -> starts a sleeping process in a new netns, prints its PID
spawn_ns() {
    unshare --net sleep infinity >/dev/null 2>&1 &
    echo $!
}

attach() { # attach PID IFNAME BRIDGE CIDR GW
    local pid=$1 ifn=$2 br=$3 cidr=$4 gw=$5
    ip link add "h$ifn" type veth peer name "$ifn"
    ip link set "h$ifn" master "$br" up
    ip link set "$ifn" netns "$pid"
    nsenter -t "$pid" -n ip link set lo up
    nsenter -t "$pid" -n ip link set "$ifn" name eth0
    nsenter -t "$pid" -n ip addr add "$cidr" brd + dev eth0
    nsenter -t "$pid" -n ip link set eth0 up
    nsenter -t "$pid" -n ip route add default via "$gw"
}

PA=$(spawn_ns); PB=$(spawn_ns)
kill_wine() {
    local n
    for n in A B; do
        WINEPREFIX="$LAB/$n/pfx" "$PROTON_DIR/files/bin/wineserver" -k 2>/dev/null || true
    done
}
cleanup() { kill_wine; kill "$PA" "$PB" 2>/dev/null || true; }
kill_wine   # a wineserver/dplaysvr left over from an earlier run lives in a dead netns
trap cleanup EXIT
sleep 0.2

attach "$PA" va br0 10.77.0.10/24 10.77.0.1
case "$SCENARIO" in
    flat)      attach "$PB" vb br0 10.77.0.20/24 10.77.0.1 ;;
    multihome) attach "$PB" vb br0 10.77.0.20/24 10.77.0.1
               nsenter -t "$PB" -n ip link add vpn0 type dummy
               nsenter -t "$PB" -n ip addr add 10.200.0.2/24 brd + dev vpn0
               nsenter -t "$PB" -n ip link set vpn0 up
               nsenter -t "$PB" -n ip route replace default dev vpn0 ;;
    routed)    attach "$PB" vb br1 10.78.0.20/24 10.78.0.1 ;;
    *) echo "unknown scenario $SCENARIO"; exit 2 ;;
esac

# ----------------------------------------------------------- prefixes
prep_prefix() { # NAME -> sets up proton prefix + game copy, echoes nothing
    local n=$1 pfx="$LAB/$1"
    mkdir -p "$pfx"
    if [[ ! -d "$pfx/pfx/drive_c" ]]; then
        STEAM_COMPAT_DATA_PATH="$pfx" STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.steam/steam" \
            "$PROTON_DIR/proton" run wineboot -u >"$LOGS/wineboot-$n.log" 2>&1 || true
    fi
    if [[ ! -f "$NATIVE_DP/dpwsockx.dll" ]]; then
        echo "native DirectPlay missing - run test/get-native-dplay.sh" >&2; exit 2
    fi
    cp -f "$NATIVE_DP"/*.dll "$NATIVE_DP"/*.exe "$pfx/pfx/drive_c/windows/syswow64/"
    local app="$pfx/app"
    mkdir -p "$app"
    cp -f "$ROOT/build/dptest.exe" "$app/"
    if [[ "$CMD" == game ]]; then
        [[ -d "$pfx/game" ]] || cp -r "$GAME_DIR" "$pfx/game"
        app="$pfx/game"
    fi
    rm -f "$app/dplayx.dll" "$app/sclanpatch.log" "$app/dptest.log"
    if [[ "$PATCH" == on ]]; then
        cp -f "$ROOT/build/dplayx.dll" "$app/"
        cp -f "$ROOT/packaging/common/sclanpatch.ini" "$app/"
        if [[ "$SCENARIO" == routed && "$n" == B ]]; then
            sed -i 's/^Peers=.*/Peers=10.77.0.10/' "$app/sclanpatch.ini"
        fi
    fi
}

run_in() { # PID NAME logfile exe args...
    local pid=$1 n=$2 log=$3; shift 3
    nsenter -t "$pid" -n --preserve-credentials env \
        STEAM_COMPAT_DATA_PATH="$LAB/$n" STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.steam/steam" \
        WINEDLLOVERRIDES="dplayx,dpwsockx,dpmodemx,dpnhupnp,dpnhpast,dplaysvr.exe=n,b" WINEDEBUG="${WINEDEBUG:--all}" PROTON_LOG=0 \
        "$PROTON_DIR/proton" run "$@" >"$log" 2>&1
}

prep_prefix A
prep_prefix B

if [[ "$CMD" == dptest ]]; then
    cd "$LAB/A/app"
    run_in "$PA" A "$LOGS/host.log" ./dptest.exe host 40 &
    HOST=$!
    sleep 4
    cd "$LAB/B/app"
    set +e
    run_in "$PB" B "$LOGS/join.log" ./dptest.exe join 25
    RC=$?
    wait $HOST; HRC=$?
    set -e
    for n in A B; do
        [[ -f "$LAB/$n/app/sclanpatch.log" ]] && cp "$LAB/$n/app/sclanpatch.log" "$LOGS/sclanpatch-$n.log"
    done
    cp "$LAB/A/app/dptest.log" "$LOGS/dptest-host.log" 2>/dev/null || true
    cp "$LAB/B/app/dptest.log" "$LOGS/dptest-join.log" 2>/dev/null || true
    echo "--- host (A)"; cat "$LOGS/dptest-host.log" 2>/dev/null || true
    echo "--- join (B)"; cat "$LOGS/dptest-join.log" 2>/dev/null || true
    echo "==> scenario=$SCENARIO patch=$PATCH join_rc=$RC host_rc=$HRC"
    exit $RC
else
    # one invisible display per instance: A on :$DISPLAY_NUM, B on :$((DISPLAY_NUM+1))
    DB=$((DISPLAY_NUM + 1))
    rm -f "$LOGS/stop"
    cd "$LAB/A/game"; DISPLAY=":$DISPLAY_NUM" run_in "$PA" A "$LOGS/gameA.log" ./Stronghold_Crusader_Extreme.exe &
    cd "$LAB/B/game"; DISPLAY=":$DB" run_in "$PB" B "$LOGS/gameB.log" ./Stronghold_Crusader_Extreme.exe &
    sleep "${GAME_WAIT:-45}"
    import -display ":$DISPLAY_NUM" -window root "$LOGS/screenA.png" || true
    import -display ":$DB" -window root "$LOGS/screenB.png" || true
    if [[ -n "${GAME_INTERACTIVE:-}" ]]; then
        # drive the games with: DISPLAY=:87 xdotool ... / DISPLAY=:88 xdotool ...
        echo "games running on :$DISPLAY_NUM (A) and :$DB (B); touch $LOGS/stop to end"
        for _ in $(seq "${GAME_TIMEOUT:-1800}"); do [[ -f "$LOGS/stop" ]] && break; sleep 1; done
    fi
    for n in A B; do
        [[ -f "$LAB/$n/game/sclanpatch.log" ]] && cp "$LAB/$n/game/sclanpatch.log" "$LOGS/sclanpatch-$n.log"
    done
fi
