MESSAGE() {
    zenity --info --text="$1"
}

#=================================================
#              WORKFLOW continuation
#=================================================

run_brief_go() {
    output_mod="$(navapp brief-get "$1" 2>&1)"
    rc_mod=$?
    if [[ $rc_mod -ne 1 ]]; then
      MESSAGE "$output_mod"
    else 
      MESSAGE "ERROR: $output_mod"
    fi
}

run_note_go() {
    output_mod="$(navapp note-get "$1" 2>&1)"
    rc_mod=$?
    if [[ $rc_mod -ne 1 ]]; then
      $EDITOR "$output_mod"
    else 
      MESSAGE "ERROR: $output_mod"
    fi
}

run_go() {
    output_go="$(navapp get "$1" 2>&1)"
    rc_go=$?
    case $rc_go in
      0)
        cd "$output_go"
        return 0
        ;;
      2)
        $EDITOR "$output_go"
        return 0
        ;;
      3)
        MESSAGE "$output_go"
        return $rc
        ;;
      4)
        eval $output_go
        return 0
        ;;
      *)
        echo "ERROR: $output_go"
        return $rc_go
        ;;
    esac
}

nav() {
    if [[ -z $1 ]]; then
      echo "Usage: nav [COMMAND] [ARGS]\n"
      echo "Commands:"
      echo "  list             lists dests in interactive mode, on dest click performs 'go <dest>' logic"
      echo "  list <group>     lists dests of group in interactive mode"
      echo "  edit             opens navdict.ini"
      echo "  go <dest>        cd/display/open/execute depends on dest's path / brief / note path / command fields priority"
      echo "  brief-go <dest>  displays brief note and performs go"
      echo "  note-go <dest>   displays note and performs go"
      echo "  <dest>           gives a path to a dest"
      return 1
    fi
   
    case $1 in
      list)
        if [[ -z $2 ]]; then
          dest_name="$(navapp list)"
        else
          dest_name="$(navapp list "$2")"
        fi
        if [[ -z $dest_name ]]; then
          return 0
        fi
        rc=$?
        case $rc in
          101)
            run_brief_go "$dest_name"
            ;;
          102)
            run_note_go "$dest_name"
            ;;
        esac
        run_go "$dest_name"
        return $?
        ;;
      startup)
        output="$(navapp get-startup 2>&1)"
        rc=$?
        if [[ $rc -eq 0 ]]; then
          eval "$output"
          return 0
        else
          echo "ERROR: on_startup setting is not provided"
          return $rc
        fi
        ;;
      go | brief-go | note-go)
        if [[ -z $2 ]]; then
          case $1 in
            go)
              echo "Usage: nav go <dest>"
              ;;
            brief-go)
              echo "Usage: nav brief-go <dest>"
              ;;
            note-go)
              echo "Usage: nav note-go <dest>"
              ;;
          esac
          return 1
        fi
        case $1 in
          brief-go)
            run_brief_go "$2"
            ;;
          note-go)
            run_note_go "$2"
            ;;
        esac
        run_go "$2"
        return $?
        ;;
      edit)
        $EDITOR ~/.config/navdict.ini
        return 0
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
