# execscript Module

The `execscript` module lets Wizard softcode run trusted shell scripts from the game's `scripts/` directory. All calls are **Wizard-only**. No shell is involved — arguments are passed directly to `execvp`.

Load it in `netmux.conf`:

```
module execscript
```

---

## Table of Contents

- [Function](#function)
  - [execscript](#execscript)
- [Script Directory](#script-directory)
- [Security Model](#security-model)
- [Error Returns](#error-returns)
- [Softcode Patterns](#softcode-patterns)
- [Writing Scripts](#writing-scripts)

---

## Function

### execscript

```
execscript(script[, arg1[, arg2[, ...]]])
```

Runs the script named `script` from the game's `scripts/` directory, passing any additional arguments directly to the process. Returns stdout as a string, truncated at 8000 characters (LBUF size).

- `script` must be a filename only — no path separators (`/`) or shell metacharacters are allowed.
- Arguments are passed via `execvp`, not through a shell. No shell expansion occurs.
- The script's stdout is captured and returned.
- Blocks until the script exits (synchronous).

**Examples**

```mush
think [execscript(echo.sh,hello world)]
→ hello world

think [execscript(lookup.sh,playerdata,Hero)]
→ {"level":10,"guild":"Rangers"}

think [execscript(timestamp.sh)]
→ 1743264000
```

**Combined with json_get:**

```mush
think [json_get([execscript(player_data.sh,Hero)],level)]
→ 10
```

---

## Script Directory

Scripts must reside in `game/scripts/` relative to the server working directory. The full path is constructed as:

```
<game_dir>/scripts/<script_name>
```

Scripts must be executable. On Linux/macOS:

```bash
chmod +x mux/game/scripts/my_script.sh
```

A minimal example script (`scripts/echo.sh`):

```bash
#!/bin/bash
echo "$@"
```

A JSON-producing script (`scripts/lookup.sh`):

```bash
#!/bin/bash
# Usage: lookup.sh playerdata <name>
type="$1"
name="$2"
case "$type" in
  playerdata)
    echo '{"level":10,"guild":"Rangers","name":"'"$name"'"}'
    ;;
  *)
    echo '{"error":"unknown type"}'
    ;;
esac
```

---

## Security Model

`execscript` is designed to be safer than a raw `system()` call, but still requires care:

1. **Wizard-only**: Non-wizard callers receive `#-1 PERMISSION DENIED`.
2. **Filename validation**: The script name is checked for path separators (`/`) and shell metacharacters. Invalid names return `#-1 INVALID SCRIPT NAME`.
3. **No shell involved**: Arguments are passed directly via `execvp`. There is no shell expansion, no glob expansion, no variable substitution. A user-provided argument of `; rm -rf /` is passed as a literal string to the script.
4. **Fixed directory**: Scripts must live in `game/scripts/`. Callers cannot navigate out of this directory.
5. **Sanitize arguments anyway**: Even without shell expansion, a script might interpret arguments in unexpected ways. Use `secure()` on any player-provided text before passing it to `execscript`.

**Example — sanitizing player input:**

```mush
&CMD_LOOKUP me=$lookup *:
  @pemit %#=[execscript(lookup.sh,playerdata,[secure(%0)])]
```

---

## Error Returns

| Situation | Return value |
|---|---|
| Script not found or not executable | `#-1 SCRIPT NOT FOUND OR NOT EXECUTABLE` |
| Invalid script name (path chars / bad chars) | `#-1 INVALID SCRIPT NAME` |
| Script exits non-zero | Output is still returned; non-zero exit is not an error from MUX's perspective |
| Output exceeds LBUF | Truncated to 8000 characters |
| Called by non-wizard | `#-1 PERMISSION DENIED` |

---

## Softcode Patterns

### Fetch data from an external service

```mush
&CMD_CHARDATA me=$chardata *:
  @pemit %#=[execscript(chardata.sh,[secure(%0)])]
```

### Combine with json_get for structured output

```mush
&FN_CHAR_LEVEL me=
  [setq(0,[execscript(chardata.sh,[secure(%0)])])]
  [if(startswith(%q0,#-1),%q0,[json_get(%q0,level)])]

think [u(me/FN_CHAR_LEVEL,Hero)]
→ 10
```

### Run a maintenance task

```mush
&CMD_BACKUP me=$@backup:
  @switch [execscript(backup.sh)]=
    done, @pemit %#=Backup complete.,
    @pemit %#=Backup result: [execscript(backup.sh)]
```

### Pass multiple arguments

```mush
&FN_AWARD me=
  [execscript(award.sh,[secure(%0)],[secure(%1)])]

think [u(me/FN_AWARD,Hero,500)]
→ Awarded 500 XP to Hero
```

---

## Writing Scripts

Scripts receive all `execscript` arguments as positional parameters (`$1`, `$2`, etc. in bash). stdout is captured by the MUX server. stderr is discarded.

**Tips:**

- Always `#!/usr/bin/env bash` or `#!/bin/bash` as the shebang.
- Output a single line or a JSON object for easy processing in softcode.
- Exit 0 on success; MUX does not treat non-zero exits specially, but it's good practice.
- Avoid long-running scripts — `execscript` blocks the MUX process until the script exits.
- Scripts inherit the server's environment (PATH, HOME, etc.). Set PATH explicitly if needed.

**Example — JSON output with error handling:**

```bash
#!/bin/bash
name="${1:-}"
if [ -z "$name" ]; then
    echo '{"error":"no name provided"}'
    exit 1
fi
result=$(some_external_tool "$name" 2>/dev/null)
if [ $? -ne 0 ]; then
    echo '{"error":"tool failed"}'
    exit 1
fi
echo "$result"
```

---

## Related Topics

- In-game: `wizhelp execscript`
- [`docs/testing.md`](testing.md) — integration tests for this module
- [`docs/httpclient.md`](httpclient.md) — outbound HTTP as an alternative to shell scripts
