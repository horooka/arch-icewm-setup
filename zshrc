# Initialization code that may require console input (password prompts, [y/n]
# confirmations, etc.) must go above this block; everything else may go below.
if [[ -r "${XDG_CACHE_HOME:-$HOME/.cache}/p10k-instant-prompt-${(%):-%n}.zsh" ]]; then
  source "${XDG_CACHE_HOME:-$HOME/.cache}/p10k-instant-prompt-${(%):-%n}.zsh"
fi

export ZSH="$HOME/.oh-my-zsh"
export EDITOR="nvim"
export DEBUGINFOD_URLS="https://debuginfod.archlinux.org"
ZSH_THEME="powerlevel10k/powerlevel10k"
source $ZSH/oh-my-zsh.sh
plugins=(git)
[[ ! -f ~/.p10k.zsh ]] || source ~/.p10k.zsh

#=================================================
#                     UTILS
#=================================================

br() {
    if [[ -z $1 ]]; then
        echo "Usage: br <promiles> - change brightness"
    else
        brightnessctl s $[ 100 * $1]
    fi
}

calc() {
    read -r expr
    echo "$expr" | bc
}

grp() {
    local sel=$(
    rg --line-number --no-heading --color=never "$1" \
    | fzf \
        --height 50% --border \
        --delimiter ':' \
        --preview 'bat --style=numbers --color=always --highlight-line {2} {1}' \
        --preview-window 'right:60%' \
    ) || return

    local file=${sel%%:*}
    local rest=${sel#*:}
    local line=${rest%%:*}

    nvim +"$line" "$file"
}

grpr() {
    local old_text="$1" all=true
    [[ $# -gt 1 && $2 = -a ]] && { all=false; shift 2; }

    [[ -n "$old_text" ]] || { echo "Usage: grpr old new [-a]  # -a=select"; return 1; }
    shift; local new_text="$1"

    if $all; then
        rg "$old_text" -l | xargs -r sed -i "s/$old_text/$new_text/g"
        echo "Replaced ALL '$old_text' → '$new_text' in matching files."
    else
        local sel=$(rg --line-number --no-heading --color=never "$old_text" |
                     fzf -m --height 50% --border --delimiter ':' \
                         --header="TAB: multi | ENTER") || return
        echo "$sel" | cut -d: -f1,2 | while IFS=: read -r file line; do
            sed -i "${line}s/$old_text/$new_text/g" "$file"
        done
        echo "Replaced $(echo "$sel" | wc -l) lines."
    fi
}

opn() {
    local path=""

    if [[ -z $1 ]]; then
        path=$(/usr/bin/fzf --preview="/usr/bin/bat {} --color=always")
        if [[ $path ]]; then
            nvim $path
        fi
    else
        case $1 in
        m)
            /usr/bin/nvim $PWD/src/main.*
            ;;
        esac
    fi
}

carry() {
    local dest="${@: -1}"
    mkdir -p "$dest"
    cp -r "$@" && cd "$dest"
}

mkzip_chsum() {
  local dir="$1" zipfile="$2"
  (
    cd "$dir"
    find . -type f ! -name "checksums.sha256" -print0 \
      | sort -z \
      | xargs -0 sha256sum > "checksums.sha256"
    zip -r "$zipfile" .
  )
}

unzip_chsum() {
  local zipfile="$1" outdir="$2" tmp
  tmp="$(mktemp -d)"
  {
    unzip -p "$zipfile" "checksums.sha256" > "$tmp/checksums.sha256" || return 1
    unzip -q "$zipfile" -d "$outdir" || return 1
    ( cd "$outdir" && sha256sum -c "$tmp/checksums.sha256" ) || return 1
  } always {
    rm -rf -- "$tmp"
  }
}

aes128() {
    if [[ -z $1 || $# -ne 4 ]]; then
      echo "Usage: aes128 <mode(e/d)> <input> <output> <password>"
      return 1
    fi
    local mode=$1 input=$2 output=$3 password=$4
    case $mode in
        e)
            openssl enc -aes-128-cbc -pbkdf2 -iter 200000 -salt \
            -in $input -out $output \
            -pass pass:$password
            ;;
        d)
            openssl enc -aes-128-cbc -pbkdf2 -iter 200000 -d -salt \
            -in $input -out $output \
            -pass pass:$password
            ;;
        *)
            echo "Unknown mode: $mode"
            return 1
            ;;
    esac
}

nav() {
    if [[ -z $1 ]]; then
      echo "Usage: nav [COMMAND] [ARGS]\n"
      echo "Commands:"
      echo "  list          lists dests in interactive mode"
      echo "  list <group>  lists dests of group in interactive mode"
      echo "  edit          opens navdict.ini"
      echo "  go <dest>     cd to a dest"
      echo "  open <dest>   opens a dest"
      echo "  <dest>        gives a path to a dest"
      return 1
    fi
   
    case $1 in
      list)
        if [[ -z $2 ]]; then
          output="$(navapp list)"
        else
          output="$(navapp list "$2")"
        fi
        rc=$?
        if [[ $rc -eq 0 ]]; then
            cd "$output"
            return 0
        elif [[ $rc -eq 2 ]]; then
            $EDITOR "$output"
            return 0
        else 
            echo "ERROR: $output"
            return $rc
        fi
        ;;
      edit)
        $EDITOR ~/.config/navdict.ini
        return 0
        ;;
      go)
        if [[ -z $2 ]]; then
          echo "Usage: nav go <dest>"
          return 1
        fi
        output="$(navapp get "$2" 2>&1)"
        rc=$?
        if [[ $rc -eq 0 ]]; then
            cd "$output"
            return 0
        elif [[ $rc -eq 2 ]]; then
            $EDITOR "$output"
            return 0
        else
            echo "ERROR: $output"
            return $rc
        fi
        ;;
      open)
        if [[ -z $2 ]]; then
          echo "Usage: nav open <dest>"
          return 1
        fi
        output="$(navapp get "$2" 2>&1)"
        rc=$?
        if [[ $rc -eq 1 ]]; then
            echo "ERROR: $output"
            return $rc
        else 
            $EDITOR "$output"
            return 0
        fi
        ;;
    esac
    output="$(navapp get "$1" 2>&1)"
    rc=$?
    if [[ $rc -eq 0 ]]; then
        echo "$output"
        return 0
    else 
        echo "ERROR: $output"
        return $rc
    fi
}

#=================================================
#                    WORKFLOW
#=================================================

cmk () {
    if [[ -z $1 ]]; then
      echo "Usage: cmk [COMMAND]\n"
      echo "Commands:"
      echo "  regen  rebuild"
      echo "  rego   recompile"
      echo "  go     run"
      echo "  gdb    gdb run"
      echo "  hck    heaptrack run"
      echo "  cts    ctags gen from compile_commands.json"
      return 1
    fi

    case "$1" in
      regen)
        shift 1
        cmake -S . -B build
        ;;
      rego)
        shift 1
        cmake --build build -j $(nproc) "$@"
        ;;
      go)
        shift 1
        project=${PWD##*/}
        build/${project} "$@"
        ;;
      gdb)
        shift 1
        project=${PWD##*/}
        gdb build/${project} "$@"
        ;;
      hck)
        shift 1
        project=${PWD##*/}
        heaptrack build/${project} "$@"
        ;;
      cts)
        ctags --extras=+q --languages=C,C++ -R
        ;;
      *)
        print "Unknown command: $1"
        return 1
        ;;
    esac
}

cgo() {
    if [[ -z $1 ]]; then
      echo "Usage: cgo [COMMAND] [ARGS]\n"
      echo "Commands:"
      echo "  cts   ctags gen"
      return 1
    fi

    case $1 in
      cts)
        ctags -R -f ./tags --extras=+q --languages=Rust --exclude=target
        ;;
      *)
        print "Unknown command: $1"
        return 1
        ;;
    esac
}

mic() {
    if [ -z "$1" ]; then
      echo "Usage: mic [COMMAND] [ARG]"
      echo "Commands:"
      echo "  mon <env>  monitor env"
      echo "  nano-rm    recompile flash and monitor nano"
      echo "  esp-rm     recompile flash and monitor esp8266"
      exit 1
    fi
    command=$1
    case $command in
    "mon")
      if [ -z "$2" ]; then
        echo "Usage: mon <env>"
        exit 1
      fi
      pio device monitor -e $2
      ;;
    "nano-rm")
      PORT="/dev/ttyUSB0"
      BAUD="115200"
      pio run -e nano &&
        avrdude -v -patmega328p -c arduino -P "$PORT" -b "$BAUD" \
          -D -U flash:w:./.pio/build/nano/firmware.hex:i &&
        pio device monitor -e nano
      ;;
    "esp8266-rm")
      pio run -e esp8266 -t upload &&\
        pio device monitor -e esp8266
      ;;
    *)
      print "Unknown command: $1"
      return 1
      ;;
    esac
}
