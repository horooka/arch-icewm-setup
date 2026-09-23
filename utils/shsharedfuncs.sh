MESSAGE() {
  # "$1" | $EDITOR
  zenity --info --text="$1"
}

#=================================================
#              WORKFLOW continuation
#=================================================

run_brief_go() {
  output_mod="$(navapp brief-get "$@" 2>&1)"
  rc_mod=$?
  if [[ $rc_mod -ne 1 ]]; then
    MESSAGE "$output_mod"
    # else
    # MESSAGE "ERROR: $output_mod"
    # Do nothing if brief is not presented for dest
  fi
}

run_note_go() {
  output_mod="$(navapp note-get "$@" 2>&1)"
  rc_mod=$?
  if [[ $rc_mod -ne 1 ]]; then
    $EDITOR "$output_mod"
    # else
    # MESSAGE "ERROR: $output_mod"
    # Do nothing if note is not presented for dest
  fi
}

run_go() {
  output_go="$(navapp get "$@" 2>&1)"
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

run_go_filter() {
  output_go_filter="$(navapp get-filter "$1" 2>&1)"
  rc_go_filter=$?
  case $rc_go_filter in
  0)
    cd "$output_go_filter"
    return 0
    ;;
  2)
    $EDITOR "$output_go_filter"
    return 0
    ;;
  3)
    MESSAGE "$output_go_filter"
    return $rc
    ;;
  4)
    eval $output_go_filter
    return 0
    ;;
  *)
    echo "ERROR: $output_go_filter"
    return $rc_go_filter
    ;;
  esac
}

nav() {
  if [[ -z $1 ]]; then
    echo "Usage: nav [COMMAND] [ARGS]\n"
    echo "Commands:"
    echo "  list                lists dests in interactive mode, on dest click performs 'go <dest>' logic"
    echo "  list <group>        lists dests of group in interactive mode"
    echo "  list-filter <cond>  lists dests satisfying condition in interactive mode"
    echo "  go <dest>           cd/display/open/execute depends on dest's path / brief / note path / command fields priority"
    echo "  brief-go <dest>     displays brief note and performs go"
    echo "  note-go <dest>      displays note and performs go"
    echo "  go-filter <cond>    performs go on first dest satisfying condition"
    echo "  <com above> -       performs command with the filter compiled previously"
    echo "  <com above> <..> =  performs command without caching compiled opcode"
    echo "  query <query>       allows to query the navdict into stdout"
    echo "  dests-list          lists destinations separated by newlines, used by shell completion"
    echo "  edit                opens navdict.ini"
    echo "  <dest>              gives a path to a dest"
    return 1
  fi

  case $1 in
  dests-list)
    navapp dests-list
    return 0
    ;;
  list | list-filter)
    dest_name="$(navapp ${@:1})"
    rc=$?
    case $rc in
    100)
      run_go "$dest_name"
      ;;
    101)
      run_brief_go "$dest_name"
      run_go "$dest_name"
      ;;
    102)
      run_note_go "$dest_name"
      run_go "$dest_name"
      ;;
    esac
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
      echo "Usage: $1 <dest>"
      return 1
    fi
    case $1 in
    brief-go)
      run_brief_go "${@:2}"
      ;;
    note-go)
      run_note_go "${@:2}"
      ;;
    esac
    run_go "${@:2}"
    return $?
    ;;
  go-filter)
    if [[ -z $2 ]]; then
      echo "Usage: go-filter <cond>"
      return 1
    fi
    run_go_filter "${@:2}"
    return $?
    ;;
  edit)
    $EDITOR ~/.config/navdict.ini
    return 0
    ;;
  query)
    if [[ -z $2 ]]; then
      echo "Usage: query <query>"
      return 1
    fi
    output="$(navapp query "${@:2}" 2>&1)"
    rc=$?
    if [[ $rc -eq 0 ]]; then
      echo "$output"
      return 0
    else
      echo "ERROR: $output"
      return $rc
    fi
    ;;
  esac
  output="$(navapp get "${@:1}" 2>&1)"
  rc=$?
  if [[ $rc -ne 1 ]]; then
    echo "$output"
    return 0
  else
    echo "ERROR: $output"
    return $rc
  fi
}
