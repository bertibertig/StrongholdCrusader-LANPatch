#!/usr/bin/env bash
# test/click.sh DISPLAY X Y  - reliable click into the (DirectDraw) game window
DISPLAY=":$1" xdotool mousemove "$2" "$3" sleep 0.2 mousedown 1 sleep 0.15 mouseup 1
