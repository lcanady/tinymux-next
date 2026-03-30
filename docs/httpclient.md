# httpclient Module

The `httpclient` module provides softcode functions for making outbound HTTP and HTTPS requests. All functions are **Wizard-only**. Requires libcurl at build time.

Load it in `netmux.conf`:

```
module httpclient
```

---

## Table of Contents

- [Functions](#functions)
  - [httpget](#httpget)
  - [httppost](#httppost)
- [Error Returns](#error-returns)
- [Softcode Patterns](#softcode-patterns)
- [Security Notes](#security-notes)
- [Timeouts and Limits](#timeouts-and-limits)

---

## Functions

### httpget

```
httpget(url[, header1[, header2[, ...]]])
```

Makes an HTTP GET request to `url` and returns the response body as a string.

- Up to 10 extra request headers can be passed as additional arguments in `"Name: Value"` format.
- Follows redirects (up to 10 hops).
- Returns the raw response body, truncated at 8000 characters (LBUF size).
- User-Agent is `TinyMUX-HttpClient/1.0`.

**Examples**

```mush
think [httpget(https://api.example.com/status)]
→ {"status":"ok","version":"1.2"}

think [httpget(https://api.example.com/data,Authorization: Bearer mytoken)]
→ {"user":"Hero","level":10}
```

**With json_get** — the most common pattern:

```mush
think [json_get([httpget(https://api.example.com/player/123)],name)]
→ Hero

think [json_get([httpget(https://api.example.com/player/123)],vitals.hp)]
→ 100
```

**Error returns**

| Situation | Return value |
|---|---|
| Network / DNS error | `#-1 HTTP ERROR: <curl message>` |
| Non-2xx status code | `#-1 HTTP <status>` (e.g. `#-1 HTTP 404`) |

---

### httppost

```
httppost(url, body[, content-type[, header1[, header2[, ...]]]])
```

Makes an HTTP POST request to `url` with `body` as the request body.

- `content-type` defaults to `application/json` if omitted or empty.
- Up to 10 extra request headers can be passed after the content-type argument.
- Returns the response body as a string, truncated at 8000 characters.
- Follows redirects (up to 10 hops).

**Examples**

```mush
think [httppost(https://api.example.com/log,{"event":"login","player":"Hero"})]
→ {"accepted":true}

think [httppost(https://api.example.com/data,name=Hero&level=10,application/x-www-form-urlencoded)]
→ {"saved":true}

think [httppost(https://api.example.com/msg,{"text":"hello"},application/json,X-API-Key: abc123)]
→ {"id":42}
```

**Error returns**

| Situation | Return value |
|---|---|
| Network / DNS error | `#-1 HTTP ERROR: <curl message>` |
| Non-2xx status code | `#-1 HTTP <status>` |

---

## Error Returns

All error returns start with `#-1`. Use `startswith()` to check:

```mush
&FN_SAFE_GET me=
  [setq(0,[httpget(%0)])]
  [if(startswith(%q0,#-1),ERROR: %q0,%q0)]

think [u(me/FN_SAFE_GET,https://api.example.com/ok)]
→ {"status":"ok"}
think [u(me/FN_SAFE_GET,https://does.not.exist.example/)]
→ ERROR: #-1 HTTP ERROR: Could not resolve host
```

---

## Softcode Patterns

### Fetch JSON and extract a field

```mush
&FN_API_LOOKUP me=
  [setq(0,[httpget(%0)])]
  [if(startswith(%q0,#-1),%q0,[json_get(%q0,%1)])]

think [u(me/FN_API_LOOKUP,https://api.example.com/player/1,name)]
→ Hero
```

### POST with a dynamically built body

```mush
&CMD_LOG_EVENT me=$log/event *:
  @trigger me/FN_LOG=[json_encode(%0)]

&FN_LOG me=
  [httppost(https://api.example.com/events,{"player":[json_encode([name(%#)])],"event":%0})]
```

### Construct a JSON body from softcode data

```mush
&FN_POST_PLAYER me=
  [setq(0,{"name":[json_encode([name(%0)])],"dbref":"[dbref(%0)]","room":"[dbref(loc(%0))]"})]
  [httppost(https://api.example.com/players,%q0)]

think [u(me/FN_POST_PLAYER,%#)]
→ {"id":123,"created":true}
```

### Iterate over an API list

```mush
&CMD_SHOW_FRIENDS me=$friends:
  @trigger me/FN_FRIENDS=[httpget(https://api.example.com/friends/[dbref(%#)])]

&FN_FRIENDS me=
  [setq(0,[json_array_len(%0)])]
  [iter(lnum(%q0),
    [json_get(%0,[[sub(##,1)]].name)] ([json_get(%0,[[sub(##,1)]].status)])
  ,, %r)]
```

### Check for error before using result

```mush
&FN_SAFE_FETCH me=
  [setq(0,[httpget(%0)])]
  [switch(1,
    startswith(%q0,#-1), #-1 FETCH FAILED,
    %q0
  )]
```

---

## Security Notes

- Only Wizards can call `httpget` and `httppost`. Non-wizard callers receive `#-1 PERMISSION DENIED`.
- The URL is passed directly to libcurl. Do not pass unsanitized user input as a URL. Use `secure()` and validate the domain before constructing URLs from player input.
- Response bodies are capped at 8000 bytes (2× LBUF internally, then truncated to LBUF on output) to prevent memory exhaustion.
- SSRF (Server-Side Request Forgery): If your game is behind a firewall, `httpget` can reach internal services. Restrict URLs to known external domains in your softcode.

---

## Timeouts and Limits

| Parameter | Value |
|---|---|
| Connection timeout | 10 seconds |
| Total request timeout | 30 seconds |
| Max redirects | 10 |
| Response body cap | 8000 bytes (LBUF size) |
| Max extra headers | 10 |

---

## Related Topics

- In-game: `wizhelp httpget`, `wizhelp httppost`
- [`docs/jsonparse.md`](jsonparse.md) — parsing JSON responses with `json_get`
- [`docs/testing.md`](testing.md) — integration tests for this module
