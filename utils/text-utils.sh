#!/bin/sh

if [[ -z "$1" ]]; then
  echo "Usage: $0 MODE [ARGS]"
  echo "MODES:"
  echo "  header TEXT  - prints 80x3 block with aligned text"
fi
case "$1" in
header)
  text="$2"
  width=78
  if [[ -n "$3" ]]; then
    width=$3
  fi
  length=${#text}
  left=$(((width - length) / 2))
  right=$((width - length - left))
  printf '# %s\n' "$(printf '%*s' "$width" '' | tr ' ' '=')"
  printf '# %*s%s%*s\n' "$left" '' "$text" "$right" ''
  printf '# %s\n' "$(printf '%*s' "$width" '' | tr ' ' '=')"
  ;;
wrap)
  width=80
  if [[ -n "$2" ]]; then
    width=$2
  fi
  fold -s -w "$width"
  ;;
*)
  echo "Unknown mode: $1"
  exit 1
  ;;
esac
