# jsonparse Module

The `jsonparse` module provides softcode functions for parsing, navigating, and encoding JSON data. All functions are **Wizard-only**.

Load it in `netmux.conf`:

```
module jsonparse
```

---

## Table of Contents

- [Path Syntax](#path-syntax)
- [Functions](#functions)
  - [json_get](#json_get)
  - [json_keys](#json_keys)
  - [json_array_len](#json_array_len)
  - [json_type](#json_type)
  - [json_encode](#json_encode)
  - [json_decode](#json_decode)
- [Error Returns](#error-returns)
- [Softcode Patterns](#softcode-patterns)
- [Passing JSON in Softcode](#passing-json-in-softcode)

---

## Path Syntax

Path arguments use dot notation for object keys and bracket notation for array indices:

| Path | Meaning |
|---|---|
| `name` | Top-level key `name` |
| `vitals.hp` | Nested: object at `vitals`, then key `hp` |
| `items[0]` | Array `items`, first element (0-indexed) |
| `items[0].name` | Field `name` of the first element of `items` |
| `a.b[2].c` | Arbitrary depth |

An empty path refers to the root value.

---

## Functions

### json_get

```
json_get(json, path)
```

Returns the value at `path` within the JSON document `json`.

- String values are returned **unquoted** (the JSON `"Hero"` becomes `Hero`).
- Object and array values are returned as their **raw JSON text** (e.g. `{"b":1}`).
- Numbers, booleans, and null are returned as their literal text (`42`, `true`, `null`).

**Examples**

```mush
think [json_get({"name":"Hero","hp":100},name)]
→ Hero

think [json_get({"hp":100},hp)]
→ 100

think [json_get({"alive":true},alive)]
→ true

think [json_get({"x":null},x)]
→ null

think [json_get({"vitals":{"hp":42,"mp":7}},vitals.hp)]
→ 42

think [json_get({"a":{"b":1}},a)]
→ {"b":1}
```

**Array indexing** — wrap the array in an object when passing it inline (required because of MUX argument parsing):

```mush
think [json_get({"v":[10,20,30]},v[2])]
→ 30

think [json_get({"items":[{"name":"sword"},{"name":"shield"}]},items[0].name)]
→ sword
```

**With httpget** — the typical real-world usage:

```mush
think [json_get([httpget(https://api.example.com/player/1)],name)]
→ Hero
```

**Error returns**

| Situation | Return value |
|---|---|
| Path not found | `#-1 PATH NOT FOUND` |
| Invalid JSON | `#-1 JSON PARSE ERROR` |
| Non-numeric array index | `#-1 PATH NOT FOUND` |
| Array index out of range | `#-1 PATH NOT FOUND` |

---

### json_keys

```
json_keys(json[, path])
```

Returns a space-separated list of the keys of the object at `path` (or the root object if `path` is omitted).

```mush
think [json_keys({"a":1,"b":2,"c":3})]
→ a b c

think [json_keys({"stats":{"str":10,"dex":14}},stats)]
→ str dex
```

**Error returns**

| Situation | Return value |
|---|---|
| Value at path is not an object | `#-1 NOT AN OBJECT` |
| Invalid JSON | `#-1 JSON PARSE ERROR` |
| Path not found | `#-1 PATH NOT FOUND` |

---

### json_array_len

```
json_array_len(json[, path])
```

Returns the number of elements in the array at `path` (or the root array if omitted).

```mush
think [json_array_len({"items":[1,2,3,4]},items)]
→ 4

think [json_array_len({"items":[]},items)]
→ 0
```

**Error returns**

| Situation | Return value |
|---|---|
| Value is not an array | `#-1 NOT AN ARRAY` |
| Invalid JSON | `#-1 JSON PARSE ERROR` |
| Path not found | `#-1 PATH NOT FOUND` |

---

### json_type

```
json_type(json[, path])
```

Returns the JSON type of the value at `path` (or the root). One of: `object`, `array`, `string`, `number`, `bool`, `null`.

```mush
think [json_type({"a":1})]
→ object

think [json_type({"v":{"hp":1}},v)]
→ object

think [json_type("hello")]
→ string

think [json_type(42)]
→ number

think [json_type(true)]
→ bool

think [json_type(null)]
→ null
```

**Error returns**

| Situation | Return value |
|---|---|
| Invalid JSON | `#-1 JSON PARSE ERROR` |
| Path not found | `#-1 PATH NOT FOUND` |

---

### json_encode

```
json_encode(value)
```

JSON-encodes a plain string, producing a quoted JSON string with all special characters properly escaped. The returned value includes the surrounding double-quotes.

```mush
think [json_encode(hello)]
→ "hello"

think [json_encode(say "hi")]
→ "say \"hi\""

think [json_encode(a\b)]
→ "a\\b"
```

Use this when constructing JSON to send via `httppost`:

```mush
think [httppost(https://api.example.com/msg,{"text":[json_encode([name(%#)])]},application/json)]
```

---

### json_decode

```
json_decode(json_string)
```

Decodes a JSON string value: strips the surrounding quotes and unescapes all JSON escape sequences (`\"`, `\\`, `\n`, `\r`, `\t`, etc.). The input must be a quoted JSON string.

```mush
think [json_decode("hello")]
→ hello

think [json_decode("say \"hi\"")]
→ say "hi"

think [strlen([json_decode("line1\\nline2")])]
→ 11
```

**Note**: `json_get` already returns string values unquoted. Use `json_decode` when you have a raw JSON string value from another source (e.g., a field whose content is itself a JSON string).

---

## Error Returns

All functions return a string beginning with `#-1` on error. Use `startswith()` to check:

```mush
&FN_SAFE_GET me=
  [setq(0,[json_get(%0,%1)])]
  [if(startswith(%q0,#-1),DEFAULT VALUE,%q0)]

think [u(me/FN_SAFE_GET,{"hp":100},hp)]
→ 100
think [u(me/FN_SAFE_GET,{"hp":100},mp)]
→ DEFAULT VALUE
```

---

## Softcode Patterns

### Safe navigation with fallback

```mush
&FN_JGET me=[if(startswith([json_get(%0,%1)],#-1),%2,[json_get(%0,%1)])]

think [u(me/FN_JGET,{"hp":100},hp,0)]
→ 100
think [u(me/FN_JGET,{"hp":100},mp,0)]
→ 0
```

### Iterate over JSON array

```mush
&CMD_LISTLOOT me=$loot/list:
  @tr me/FN_SHOW_LOOT=[httpget(https://api.example.com/loot/[dbref(%#)])]

&FN_SHOW_LOOT me=
  [setq(0,[json_array_len(%0)])]
  [iter(lnum(%q0),
    [json_get(%0,[[sub(##,1)]].name)] - [json_get(%0,[[sub(##,1)]].value)] gp
  ,, %r)]
```

### Build a JSON object from softcode data

```mush
&FN_PLAYER_JSON me=
  {"name":[json_encode([name(%0)])],"dbref":"[dbref(%0)]","location":"[dbref(loc(%0))]"}

think [u(me/FN_PLAYER_JSON,%#)]
→ {"name":"Hero","dbref":"#123","location":"#1"}
```

### Check type before use

```mush
&FN_SAFE_ARRAY_LEN me=
  [setq(0,[json_type(%0)])]
  [switch(%q0,array,[json_array_len(%0)],0)]

think [u(me/FN_SAFE_ARRAY_LEN,[1,2,3])]  → 3  (via httpget result)
think [u(me/FN_SAFE_ARRAY_LEN,{"a":1})]  → 0
```

---

## Passing JSON in Softcode

MUX uses commas to separate function arguments. JSON objects like `{"a":1,"b":2}` work naturally because `{...}` groups the content (including inner commas) as one argument.

JSON **arrays** with multiple elements cannot be passed as bare literals in function arguments — the commas inside `[1,2,3]` would be parsed as argument separators. Always wrap arrays in an object when constructing inline:

```mush
# WRONG — comma in [1,2,3] splits the argument
think [json_array_len([1,2,3])]

# CORRECT — wrap in an object so commas are at depth >0
think [json_array_len({"v":[1,2,3]},v)]

# CORRECT — single-element arrays work fine (no commas)
think [json_array_len([1])]

# CORRECT — use httpget result (already a single string argument)
think [json_array_len([httpget(https://api.example.com/list)])]
```

---

## Related Topics

- In-game: `wizhelp json_get`, `wizhelp json_keys`, `wizhelp json_array_len`, `wizhelp json_type`, `wizhelp json_encode`, `wizhelp json_decode`
- [`docs/httpclient.md`](httpclient.md) — using httpget/httppost with json_get
- [`docs/testing.md`](testing.md) — integration tests for this module
