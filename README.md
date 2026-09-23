# Nav

App with shell wrapper for managing, operating and navigating through the destinations,
which are either a task or a navigation path (e.g filesystem path or url)

- Usage: nav [COMMAND] [ARGS]

```
Commands:
  list                lists dests in interactive mode, on dest click performs 'go <dest>' logic
  list <group>        lists dests of the group in interactive mode
  list-filter <cond>  lists dests satisfying condition in interactive mode
  get <dest>          prints the path of the dest
  get-filter <cond>   prints dest_name of the first dest satisfying condition
  brief-get <dest>    prints the brief of the dest
  note-get <dest>     prints the note of the dest
  <com above> -       performs command with the filter compiled previously
  <com above> <..> =  performs command without caching compiled filter
  query <query>       allows to query the navdict into stdout
  get-startup         prints the startup command from config
```

## Nav file format

- Destination format:
`<destinatinon name> = [$P<destination path>][$B<task brief>][$N<task note path>][$C<command to run>][$L<priority level>][$F formatting num][$M dest mark]`.
Entry required to have either dest path, brief, note path or a command to run,
otherwise entry is ignored
  - Dest mark is a symbolic icon which are placed instead of bullets in bulleted
lists of gui mode or tab-completion if mark is presented. There is some hardcoded
mappings (they also can be overriden)
    - closed=`X`
    - started=` `
    - finished=`-`
    - ongoing=`>`
    - dest - displays prioritized return type of `get <dest>` logic:
      - `C` - command
      - `P` - path
      - `N` - note
      - `B` - brief

- Destination group format:
`[<group name>, <formatting>]`. Formatting is the background and foreground colors idxs

### Settings group format

Should be the first group in the file, designated as `[$SETTINGS]` ini-format group
each value denoted with $S setting specifier

- Destination fields priority:
Specifies the `go <dest>` fields priority from higher to lower (default order
is shown below)

```ini
priority=$Scommand,path,note,brief
```

- UI click response:
Specifies the default go-mode behaviour on dest click (go / brief-go / note-go)
(default option is shown below)

```ini
on_click=$Sgo
```

- Startup command:
Specifies the command to run on machine startup (unset on default)

```ini
on_startup=$Snav list todo
```

- Dest marks mappings:
Specifies mark -> symbol mappings (default ones are shown below)

```ini
marks_map=$Sclosed=X,started=' ',finished=-,ongoing=>
```

## Shell usage
Can be used as shell shorthands for the destinations by command substitution (e.g
$EDITOR $(navapp note-get <dest>) for opening dest's note)
Working shell wrapper for navapp is nav() func in ./utils/shsharedfuncs.sh

- Destinations completion - completion of destinations with symbolic destination mark
icon by `./utils/_nav`

## Query syntax
SQL-like syntax for querying the navdict, `<cond>` args should contain only
expression, `<query>` can use entire syntax

- Supported operators: `=`, `!=`, `<`, `<=`, `>`, `>=`
- Supported funcs: `ICO`
- Supported keywords: `SELECT`, `WHERE`, `LIMIT`
- Columns:
  - `dest` - dest name
  - `path` - dest path
  - `brief` - dest brief
  - `command` - dest command
  - `group` - group name of the dest
  - `level` - dest priority level
  - `format` - dest formatting
  - `mark` - dest mark

### Compilation
All the filtering commands implemented using stack machine filtering, for instance,
`nav list <group>` is just shorthand for `nav list-filter "group = '<group>'"`,
so all the filtering commands emits opcodes and can reuse them

- Last compiled opcode is stored by default in `~/.cache/nav/opcode.bin`
and can be reused by passing "-" argument for filtering commands

- It is possible to provide additional "=" argument to the filtering command
to avoid caching opcode

### Examples

- `nav list-filter "group = 'todo' && priority > 0 && mark = 'suspended'"` - to
interactively list prioritized todo destinations with suspended mark

- `nav query "SELECT dest, level WHERE group = 'todo' && level > 0 && mark = 
'suspended'"` - to query prioritized todo destinations with suspended mark

# XTemplate

Gtkmm3 app for using and creating clipboard xtemplates

## Features

- Storing of xtemplates in a file at ~/.config/xtemplate.d/xtemplate.txt with
opportunity to manage multiple xtemplate files
    1. Location of the default file can be changed in config
- Opportunity to use hardcoded config as include/xtemplate/xtemplate_hardcoded.h file
    1. Hardcoded xtemplates' displaying can be toggled via toolbar checkbox
- Conditional blocks in xtemplate bodies via an AST (`##IF_*` / `##END`)
- Possibility to reuse templates by recursive rendering with ##XTEMPLATE directive
- Exposed rendering API via CLI mode (rendering, xtemplates listing, xtemplates' vars listing)

## Config file format

Config file is a .ini file with the following sections

| Key | Value |
| --- | ----- |
| DefaultPath | Path to fallback xtemplate file location, default is `~/.config/xtemplate.ini` |
| LastPathIdx | Idx of a last used xfile from cache list paths, preffered over DefaultPath |
| RenderEmptyVals | If set to 'true', $varname with we rendered even for an empty string |

- Cache list - Ini group, representing set of ever opened xfiles:

```ini
[CacheList]
path1=
path2=
```

## Template file format

File contains xtemplates records separated by empty lines

### Template record format

- First line is the xtemplate name
- Second line is the "TAGS:" with comma separated xtemplate's tags
- Third line is the "VARS:" with comma separated xtemplate's vars records
  - Each var record is `type; name` (semicolon-separated) with snake-case name
  - If only one word is given, it is considered as a var name with empty type
- Rest of the lines is the xtemplate's body in `"<TEMPLATE_BODY>"` tags
- Inside the body, variables are referenced by name as `$varname`

### Template vars' types

Full type grammar (left → right, no overlapping separators): `base[:options][=deps][tags]; name`

| Type | Description |
| --- | --- |
| PlainText | Conventional for actual plaintext, gray highlighting |
| XCHECKBOX | Logical variable for conditions; rendered as checkbox (`ON`/`OFF`) |
| XVARIANT:a-b-c | Choice variable for conditions; rendered as combobox with options `a`, `b`, `c` |
| XVARIANT | Same, but free-text entry when no options are listed |
| (any other) | Rendered as a text entry in the render menu |

#### Depends-on syntax

Append `=controller[-...]` to the type (after any `XVARIANT:` options). Controllers are separated by `-` so commas stay reserved for the VARS record list. Example: `XVARIANT:a-b-c=ctrl1-!ctrl2`

| Token | Meaning |
| --- | --- |
| `=var` | Enable when `var` is active/non-empty for checkbox/other type |
| `=var(value)` | Enable when `var` is equal to `value` for all the types except checkbox (otherwise value will be ignored) |
| `=!var` | Inverts condition |
| `=a-!b` | Multiple controllers — dependent is active only when **all** conditions hold (AND) |

#### Tag syntax

| Tag | Meaning |
| --- | --- |
| `REQUIRED` | Blocks GUI and CLI rendering if the var is missing/empty. For checkboxes/variants is meaningful in cli mode |

### Directives

Template's variables can be used in conditional blocks and conditional variables can be used outside of conditions too.
`$` vars' name prefix is optional here

| Directive | Meaning |
| --- | --- |
| `##ERROR message` | Abort render; message is the rest of the line (`$vars` expanded), manual way to implement complex logic |
| `##SET var rest...` | Set/replace `var` to the rest of the line (expanded) |
| `##CLEAR var` | Clear `var` |
| `##XTEMPLATE name [--xfile PATH] [--var val]` | Render xtemplate inside. `-xfile` to use different file. Indent is controlled by `--indent`. |
| `##IF_ON var` | Start a chain; take when checkbox var is `ON` |
| `##IF_OFF var` | Start a chain; take when checkbox var is `OFF` |
| `##IF_EQ var value` | Start a chain; take when var equals value |
| `##IF_NEQ var value` | Start a chain; take when var differs from value |
| `##IF_EMPTY var` | Start a chain; take when var is empty (equivalent to `##IF_EQ var ""`) |
| `##IF_NEMPTY var` | Start a chain; take when var is not empty (equivalent to `##IF_NEQ var ""`) |
| `##ELIF_ON/OFF/EQ/NEQ/EMPTY/NEMPTY` | Next branch in the same chain |
| `##ELSE` | Final fallback branch |
| `##END` | Closes the whole IF/ELIF/ELSE chain |


## Cli mode

- Usage: xtemplate [COMMAND] [ARG]

```
Commands:
  -h, --help          Show this help message and exit
  --xfile [PATH]      Xtemplate file to use
  --xtemplate [TEXT]  Xtemplate to use, (default: last cached or stored default from ~/.config/xtemplate.ini accordignly to their priorities)
  --indent [INT]      Additional indent for xtemplates' bodies
  --list-xtemplates   List xtemplates
  --list-variables    List vars for the choosen xtempalte
  --[var] [value]     Set variable to value. Vars tagged "[REQUIRED]" must be set (non-empty) for rendering
```
