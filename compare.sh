#!/bin/sh
set -eu

# Compare shipped configuration with the installed one

# Prefer git's colored diff; fall back to plain diff -u
if command -v git >/dev/null 2>&1; then
  color_diff() {
    git diff --no-index --color=always -- "$1" "$2" || true
  }
  color_diff_rec() {
    git diff --no-index --color=always --stat --patch -- "$1" "$2" || true
  }
else
  color_diff() {
    diff -u -- "$1" "$2" || true
  }
  color_diff_rec() {
    diff -ru -- "$1" "$2" || true
  }
fi

# ============
# === Home ===
# ============
echo
echo "============"
echo "=== Home ==="
echo "============"
echo
for i in Xresources utils/zshrc; do
  src="$i"
  src_base="$(basename "$src")"
  dst="$HOME/.$src_base"
  if [[ ! -e "$dst" ]]; then
    echo "MISSING: $dst"
  elif ! cmp -s -- "$src" "$dst"; then
    echo "--- diff: $src -> $dst"
    color_diff "$dst" "$src"
  else
    echo "OK: $dst"
  fi
done

# ===============
# === .config ===
# ===============
echo
echo "==============="
echo "=== .config ==="
echo "==============="
echo
for i in icewm nvim xtemplate.d utils/shsharedfuncs.sh utils/startup.service; do
  src="$i"
  src_base="$(basename "$src")"
  if [[ $i == utils/startup.service ]]; then
    dst="$HOME/.config/systemd/user/$src_base"
  else
    dst="$HOME/.config/$src_base"
  fi
  if [[ ! -e "$dst" ]]; then
    echo "MISSING: $dst"
  elif [[ -d "$src" ]]; then
    if ! diff -rq -- "$src" "$dst" >/dev/null 2>&1; then
      echo "--- diff: $src -> $dst"
      color_diff_rec "$dst" "$src"
    else
      echo "OK: $dst"
    fi
  elif ! cmp -s -- "$src" "$dst"; then
    echo "--- diff: $src -> $dst"
    color_diff "$dst" "$src"
  else
    echo "OK: $dst"
  fi
done

# ==================
# === .local/bin ===
# ==================
echo
echo "=================="
echo "=== .local/bin ==="
echo "=================="
echo
for i in utils/startup.sh utils/opencode.sh utils/fork-xterm.sh utils/text-utils.sh; do
  src="$i"
  src_base="$(basename "$src")"
  dst="$HOME/.local/bin/$src_base"
  if [[ ! -e "$dst" ]]; then
    echo "MISSING: $dst"
  elif ! cmp -s -- "$src" "$dst"; then
    echo "--- diff: $src -> $dst"
    color_diff "$dst" "$src"
  else
    echo "OK: $dst"
  fi
done

# =================
# === /usr/bin/ ===
# =================
echo
echo "================"
echo "=== /usr/bin ==="
echo "================"
echo
for i in navapp/build/navapp; do
  src="$i"
  src_base="$(basename "$src")"
  dst="/usr/bin/$src_base"
  if [[ ! -e "$dst" ]]; then
    echo "MISSING: $dst (build navapp first)"
  elif ! cmp -s -- "$src" "$dst"; then
    echo "--- diff: $src -> $dst (binary differs)"
  else
    echo "OK: $dst"
  fi
done

echo
echo "Comparison complete."
