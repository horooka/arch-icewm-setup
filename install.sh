set -eu

sudo pacman -Suy bat brightnessctl firefox fzf git icewm nvim zsh

# === Home ===
echo "=== Home ==="
for i in Xresources; do
  src="./$i"
  dst="$HOME/.Xresources"

  if [[ -e "$dst" ]]; then
    echo "Path $dst (part of a setup) already exists, aborting"
    exit 1
  fi

  cp $i ~/.$i
done
if [[ -e "$HOME/.zshrc" ]]; then
  echo "Path $HOME/.zshrc (part of a setup) already exists, aborting"
  exit 1
fi
cp zshrc ~/.zshrc
sudo mv MesloLGSNerdFont-Regular.ttf /usr/share/fonts
chsh -s /usr/bin/zsh

# === .config ===
echo "=== .config ==="
mkdir -p "$HOME/.config"
# ~/.config/icewm config can be overriden by /usr/share/icewm
for i in icewm nvim xtemplate.d; do
  src="./$i"
  dst="$HOME/.config/$i"

  if [[ -e "$dst" ]]; then
    echo "Path $dst (part of a setup) already exists, aborting"
    exit 1
  fi

  cp -r "$src" "$dst"
done

=== oh-my-zsh ===
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
