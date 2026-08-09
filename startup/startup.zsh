#!/usr/bin/env zsh

# === Brightness ===
hour=$(date +%H)
hour=$((10#$hour))
if (( hour >= 7 && hour < 20 )); then
    brightnessctl s 10%
else
    brightnessctl s 1%
fi

# === Nav ====
source "$HOME/.config/zshsharedfuncs.zsh"
export DISPLAY=:0
export XAUTHORITY=/run/user/1000/lyxauth

for attempt in {1..30}; do
    if [[ -r "$XAUTHORITY" ]] &&
       /usr/bin/xset q >/dev/null 2>&1; then
        nav startup
	exit 0
    fi
    sleep 1
done

exit 1
