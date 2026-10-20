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
    return $?
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
  output_go_filter="$(navapp get-filter "$@" 2>&1)"
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
    return $?
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
    echo "  list [<group>] [-c|-s]      lists dests"
    echo "  list-filter <cond> [-c|-s]  lists dests satisfying condition"
    echo "  go <dest> [-c|-s]           cd/display/open/execute depends on dest's path / brief / note path / command fields priority"
    echo "  brief <dest> [-c|-s]        performs brief opening on dest"
    echo "  note <dest> [-c|-s]         performs note opening on dest"
    echo "  brief-go <dest> [-c|-s]     displays brief note and performs go"
    echo "  note-go <dest> [-c|-s]      displays note and performs go"
    echo "  go-filter <cond> [-c|-s]    performs go on first dest satisfying condition"
    echo "  edit <dest> [-c|-s]         edits a dest line in place (readline, prefilled)"
    echo "  rm <dest> [-f] [-c|-s]      removes a dest (asks for confirmation; -f skips)"
    echo "  add <group> [line]          appends a dest line to the group"
    echo "  edit-group <group>          edits a group body in \$EDITOR"
    echo "  edit-dict                   opens navdict.ini"
    echo "  query <query> [-f]          allows to query the navdict into stdout ([-f] adds the field header)"
    echo "  dates [opts]                lists dests with dates sorted by date (--upcoming [N], --past, --reverse, --all, --limit N)"
    echo "  dests-list                  emits dests for shell completion (color params, display, value)"
    echo "  <dest>                      gives a path to a dest"
    echo ""
    echo "Flags:"
    echo "  -c  reuse the previously compiled filter"
    echo "  -s  compile the filter but do not cache it"
    return 1
  fi

  case $1 in
  dests-list)
    navapp dests-list "${@:2}"
    return $?
    ;;
  list | list-filter)
    navapp "${@:1}"
    return $?
    ;;
  startup)
    output="$(navapp get-startup 2>&1)"
    rc=$?
    if [[ $rc -eq 0 ]]; then
      MESSAGE "$(eval "$output")"
      return 0
    else
      echo "ERROR: "on_startup" setting is not provided"
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
  note | brief)
    if [[ -z $2 ]]; then
      echo "Usage: $1 <dest>"
      return 1
    fi
    case $1 in
    brief)
      run_brief_go "${@:2}"
      ;;
    note)
      run_note_go "${@:2}"
      ;;
    esac
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
  add)
    navapp add "${@:2}"
    return $?
    ;;
  rm)
    navapp rm "${@:2}"
    return $?
    ;;
  edit)
    navapp edit "${@:2}"
    return $?
    ;;
  edit-group)
    navapp edit-group "${@:2}"
    return $?
    ;;
  edit-dict)
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
      [[ -n "$output" ]] && echo "$output"
      return 0
    else
      echo "ERROR: $output"
      return $rc
    fi
    ;;
  dates)
    navapp dates "${@:2}"
    return $?
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
