#!/bin/sh

window_id=$(xdotool getactivewindow 2>/dev/null) || exit 1
xterm_pid=$(xprop -id "$window_id" _NET_WM_PID 2>/dev/null |
  awk '{print $3}')

[ -n "$xterm_pid" ] || exit 1

find_shell() {
  parent=$1

  for child in $(pgrep -P "$parent" 2>/dev/null); do
    command=$(ps -o comm= -p "$child" 2>/dev/null)

    case "$command" in
    bash | zsh | fish | dash | sh | ksh | tcsh)
      echo "$child"
      return 0
      ;;
    esac

    result=$(find_shell "$child")
    [ -n "$result" ] && {
      echo "$result"
      return 0
    }
  done
}

shell_pid=$(find_shell "$xterm_pid")
[ -n "$shell_pid" ] || shell_pid="$xterm_pid"

directory=$(readlink "/proc/$shell_pid/cwd") || exit 1
[ -d "$directory" ] || exit 1

exec xterm -e sh -c 'cd "$1" && exec "${SHELL:-/bin/sh}" -i' sh "$directory" &
