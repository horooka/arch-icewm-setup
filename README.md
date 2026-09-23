# Nav

App with shell wrapper for managing, operating and navigating through the destinations,
which are either a task or a navigation path (e.g filesystem path or url)

- Usage: navapp [COMMAND] [ARGS]

```
Commands:
  list                     lists dests in interactive mode, on dest click performs 'go <dest>' logic
  list <group>             lists dests of the group in interactive mode
  list-filter <cond>       lists dests satisfying condition in interactive mode
  get <dest> [args]        prints the path of the dest with args expanded
  get-filter <cond>        prints dest_name of the first dest satisfying condition
  brief-get <dest> [args]  prints the brief of the dest
  note-get <dest> [args]   prints the note of the dest
  <com above> -c           performs command with the filter compiled previously
  <com above> <..> -s      performs command without caching compiled filter
  query <query> [-f]       allows to query the navdict into stdout ([-f] adds the field header)
  get-startup              prints the startup command from config
  dates [--upcoming [N]]   lists dests with dates, sorted by date (also --past, --reverse, --all, --limit N)
```

## Nav file format

- Destination format:
`<destinatinon name> = [@P<destination path>][@B<task brief>][@N<task note path>][@D<date YYYY-MM-DD>][@C<command to run>][@L<priority level>][@F formatting num][@M dest mark]`.
Entry required to have either dest path, brief, note path or a command to run,
otherwise entry is ignored.
`@D` accepts `YYYY-MM-DD` (or `YYYY/M/D`) and is normalized; an invalid date is
a parse error. `@` introduces a field specifier, so a literal `@` inside a field
is written `@@`
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

Should be the first group in the file, designated as `[@SETTINGS]` ini-format group
each value denoted with @S setting specifier

- Destination fields priority:
Specifies the `go <dest>` fields priority from higher to lower (default order
is shown below)

```ini
priority=@Scommand,path,note,brief
```

- UI click response:
Specifies the default go-mode behaviour on dest click (go / brief-go / note-go)
(default option is shown below)

```ini
on_click=@Sgo
```

- Startup command:
Specifies the command to run on machine startup (unset on default)

```ini
on_startup=@Snav list todo
```

- Dest marks mappings:
Specifies mark -> symbol mappings (default ones are shown below)

```ini
marks_map=@Sclosed=X,started=' ',finished=-,ongoing=>
```

## Shell usage
Can be used as shell shorthands for the destinations by command substitution (e.g
$EDITOR $(navapp note-get <dest>) for opening dest's note)
Shell wrapper is provided by `./utils/shsharedfuncs.sh` as nav() func

- Destinations completion - completion of destinations with symbolic destination mark
icon by `./utils/_nav`

## Argument expansion

`@C`, `@P`, `@N` or `@B` fields can have positional args which are expanded if
returned by `get`, `brief-get` or `note-get` call (max args amount is 10,
not used vars placeholders are erased during expansion):

- `@0`, `@1`, ... , `@9` - the corresponding argument, zero-based
- `@@` - a literal `@`
- an out-of-range reference, e.g `@9` with a single provided arg, is erased;
a `@` not followed by a digit or `@` is kept as-is

Example: for the dest `fw = @Cfirefox --new-window @0`, `navapp get fw google.com`
returns `firefox --new-window google.com` with return code, corresponding to
command field, which can be executed by shell wrapper

## Query syntax
SQL-like syntax for querying the navdict, `<cond>` args should contain only
expression, `<query>` can use entire syntax

- Supported operators: `=`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `||`, `!`
  (`< > <= >=` compare strings lexicographically when both operands are strings)
- Supported funcs:
  - `IS_DIR(path)` - `yes` when the string argument is an existing directory
    (name is case-insensitive)
  - `TODAY()` - today's date as `YYYY-MM-DD`
  - `DAYS_UNTIL(date)` - whole days from today until `date` (negative if past)
- Supported keywords: `SELECT`, `WHERE`, `ORDER`, `BY`, `ASC`, `DESC`, `LIMIT`
- Columns:
  - `dest` - dest name
  - `path` - dest path
  - `brief` - dest brief
  - `note` - dest note filepath
  - `command` - dest command
  - `group` - group name of the dest
  - `level` - dest priority level
  - `format` - dest formatting
  - `mark` - dest mark
  - `date` - dest date as `YYYY-MM-DD` (empty when unset)

Output is comma-separated rows without a header. Pass `-f` (or `--fields`)
to prepend a header line with the selected field names, e.g.
`nav query "SELECT dest, level WHERE group = 'todo'" -f`.

Clauses follow SQL order:
`SELECT <cols> [WHERE <expr>] [ORDER BY <col> [ASC|DESC], ...] [LIMIT <n>]`.
`LIMIT` is applied after sorting, and `ORDER BY` defaults to `ASC`. Because
`date` is a normalized ISO string, `date >= TODAY()` / `date < TODAY()` work
directly; guard unset dates with `date != ''`.

### Compilation
All the filtering commands implemented using stack machine filtering, for instance,
`nav list <group>` is just shorthand for `nav list-filter "group = '<group>'"`,
so all the filtering commands emits opcodes and can reuse them

- Last compiled opcode is stored by default in `~/.cache/nav/opcode.bin`
and can be reused by passing the `-c` flag to a filtering command

- It is possible to pass the `-s` flag to a filtering command
to avoid caching opcode (empty filters, like in `nav list` without specified group,
are not cached)

### Examples

- `nav list-filter "group = 'todo' && priority > 0 && mark = 'suspended'"` - to
interactively list prioritized todo destinations with suspended mark

- `nav query "SELECT dest, level WHERE group = 'todo' && level > 0 && mark = 
'suspended'"` - to query prioritized todo destinations with suspended mark

- `nav query "SELECT dest WHERE IS_DIR(path)"` - to query destinations whose
path is an existing directory

- `nav query "SELECT dest WHERE !IS_DIR(path)"` - to query destinations whose
path is not an existing directory

- `nav query "SELECT dest, date WHERE date != '' ORDER BY date LIMIT 5"` - to
get the 5 nearest dates

- `nav query "SELECT dest WHERE DAYS_UNTIL(date) <= 7 && date != ''"` - to query
destinations due within a week

- `nav dates --upcoming 7` - same, via the dedicated dates command

## Usage examples

- Creating of a dest as a shortcut for per dir command execution of specified
group, so e.g `nav exec-dir todo "cd \$dpath && git diff --quiet"` will display
stage cleanness of the todo directory dests:
```txt
exec-dir=@Cnav query "SELECT path WHERE IS_DIR(path) && group = '@0'" | while IFS= read -r dpath; do (@1) && echo "\"$dpath\": +" || echo "\"$dpath\": -"; done
```

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
path1=<unused space>
path2=<unused space>
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
