set -eu

sudo pacman -Suy bat brightnessctl firefox fzf git icewm nvim zsh cmake opencode

# ============
# === Home ===
# ============
echo "\n\n============"
echo "=== Home ==="
echo "============\n\n"
for i in Xresources utils/zshrc; do
  src="$i"
  src_base="$(basename "$src")"
  dst="$HOME/.$src_base"
  if [[ -e "$dst" ]] && ! cmp -s -- "$src" "$dst"; then
    echo "Path $dst (part of a setup) already exists, aborting"
    exit 1
  fi
  if ! cmp -s -- "$src" "$dst"; then
    cp $i ~/.$i
  fi
done
sudo mv MesloLGSNerdFont-Regular.ttf /usr/share/fonts
chsh -s /usr/bin/zsh

# ===============
# === .config ===
# ===============
echo "\n\n==============="
echo "=== .config ==="
echo "===============\n\n"
mkdir -p "$HOME/.config"
mkdir -p "$HOME/.config/systemd/user"
# ============================================================================
# ~/.config/icewm/preferences can be overriden by /usr/share/icewm/preferences
# ============================================================================
for i in icewm nvim xtemplate.d utils/zshsharedfuncs.sh utils/startup.service; do
  src="$i"
  src_base="$(basename "$src")"
  if [[ $i == utils/startup.service ]]; then
    dst="$HOME/.config/systemd/user/$src_base"
  else
    dst="$HOME/.config/$src_base"
  fi
  if [[ -e "$dst" ]] && ! cmp -s -- "$src" "$dst"; then
    echo "Path $dst (part of a setup) already exists, aborting"
    exit 1
  fi
  if ! cmp -s -- "$src" "$dst"; then
    cp -r "$src" "$dst"
  fi
done

# ==================
# === .local/bin ===
# ==================
echo "\n\n=================="
echo "=== .local/bin ==="
echo "==================\n\n"
mkdir -p "$HOME/.local/bin/"
for i in utils/startup.sh utils/opencode.sh utils/fork-xterm.sh; do
  src="$i"
  src_base="$(basename "$src")"
  dst="$HOME/.local/bin/$src_base"
  if [[ -e "$dst" ]] && ! cmp -s -- "$src" "$dst"; then
    echo "Path $dst (part of a setup) already exists, aborting"
    exit 1
  fi
  if ! cmp -s -- "$src" "$dst"; then
    cp "$src" "$dst"
  fi
  chmod +x "$dst"
done

# ==============
# === navapp ===
# ==============
echo "\n\n=============="
echo "=== navapp ==="
echo "==============\n\n"
cd navapp && cmake -S . -B build && cmake --build build && cd ..

# =================
# === /usr/bin/ ===
# =================
echo "\n\n================"
echo "=== /usr/bin ==="
echo "================\n\n"
for i in navapp/build/navapp; do
  src="$i"
  src_base="$(basename "$src")"
  dst="/usr/bin/$src_base"
  if [[ -e "$dst" ]] && ! cmp -s -- "$src" "$dst"; then
    echo "Path $dst (part of a setup) already exists, aborting"
    exit 1
  fi
  if ! cmp -s -- "$src" "$dst"; then
    cp "$src" "$dst"
  fi
  chmod +x "$dst"
done

# ================
# === services ===
# ================
echo "\n\n================"
echo "=== services ==="
echo "================\n\n"
systemctl --user daemon-reload
for i in utils/startup.service; do
  src_base="$(basename "$i")"
  systemctl --user enable --now "$src_base"
done

# =================
# === oh-my-zsh ===
# =================
echo "\n\n================="
echo "=== oh-my-zsh ==="
echo "=================\n\n"
sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh)"
git clone https://github.com/romkatv/powerlevel10k.git "${ZSH_CUSTOM:-$HOME/.oh-my-zsh/custom}/themes/powerlevel10k"
sudo cp MesloLGSNerdFont-Regular.ttf /usr/share/fonts
chsh -s /usr/bin/zsh

# ===============
# === secrets ===
# ===============
echo "\n\n==============="
echo "=== secrets ==="
echo "===============\n\n"
echo " - ~/.local/bin/opencode.sh needs "pass"-managed deepseek key"

# ==============
# === finish ===
# ==============
echo "\n\n=============="
echo "=== finish ==="
echo "==============\n\n"
echo "Installation is complete, make reboot"

# =============
# === audio ===
# =============
# echo "\n\n============="
# echo "=== audio ==="
# echo "=============\n\n"
# sudo pacman -S pipewire pipewire-pulse pipewire-alsa wireplumber sof-firmware
