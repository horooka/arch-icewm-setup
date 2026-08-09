set -eu

sudo pacman -Suy bat brightnessctl firefox fzf git icewm nvim zsh cmake

# === Home ===
echo "=== Home ==="
for i in Xresources startup/zshrc; do
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

# === .config ===
echo "=== .config ==="
mkdir -p "$HOME/.config"
mkdir -p "$HOME/.config/systemd/user"
# ~/.config/icewm config can be overriden by /usr/share/icewm
for i in icewm nvim xtemplate.d startup/zshsharedfuncs.zsh startup/startup.service; do
  src="$i"
  src_base="$(basename "$src")"
  if [[ $i == startup/startup.service ]]; then
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

# === .local/bin ===
echo "=== .local/bin ==="
mkdir -p "$HOME/.local/bin/"
for i in startup/startup.zsh; do
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

# === navapp ===
echo "=== navapp ==="
cd navapp && cmake -S . -B build && cmake --build build && cd ..

# === /usr/bin/ ===
echo "=== /usr/bin ==="
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

# === services ===
echo "=== services ==="
systemctl --user daemon-reload
for i in startup/startup.service; do
  src_base="$(basename "$i")"
  systemctl --user enable --now "$src_base"
done

# === oh-my-zsh ===
echo "=== oh-my-zsh ==="
sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh)"
git clone https://github.com/romkatv/powerlevel10k.git "${ZSH_CUSTOM:-$HOME/.oh-my-zsh/custom}/themes/powerlevel10k"
sudo cp MesloLGSNerdFont-Regular.ttf /usr/share/fonts
chsh -s /usr/bin/zsh

# C++
#
# sudo pacman -S gdb cmake clang ctags

# Audio
#
# sudo pacman -S pipewire pipewire-pulse pipewire-alsa wireplumber sof-firmware

# Reboot after installing
#
# reboot
