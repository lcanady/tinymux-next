# TinyMUX 2.13

TinyMUX is a high-performance MUSH/MUD server written in C++. This fork adds a **WebSocket bridge**, **JSON manipulation**, **outbound HTTP/HTTPS**, and **trusted script execution** as loadable modules — all fully tested with a self-contained Docker test suite.

---

## Table of Contents

- [What's New](#whats-new)
- [Quick Start (Docker)](#quick-start-docker)
- [Native Build](#native-build)
- [Running the Server](#running-the-server)
- [Module System](#module-system)
- [New Modules](#new-modules)
- [WebSocket Bridge](#websocket-bridge)
- [Testing](#testing)
- [Configuration Reference](#configuration-reference)
- [Directory Structure](#directory-structure)
- [Version & Release](#version--release)

---

## What's New

| Feature | Module | Functions |
|---|---|---|
| JSON parsing and manipulation | `jsonparse.so` | `json_get`, `json_keys`, `json_array_len`, `json_type`, `json_encode`, `json_decode` |
| Outbound HTTP/HTTPS | `httpclient.so` | `httpget`, `httppost` |
| Trusted script execution | `execscript.so` | `execscript` |
| WebSocket bridge (RFC 6455) | core (`bsd.cpp`) | — |
| GMCP↔WebSocket JSON bridge | core (`ws_gmcp.cpp`) | — |

All new softcode functions are **Wizard-only**. See [`docs/`](docs/) for full reference.

---

## Quick Start (Docker)

Build and run the complete test suite — no dependencies required beyond Docker:

```bash
docker build -f docker/Dockerfile -t tinymux-test .
docker run --rm tinymux-test test
```

Expected output:

```
━━━ Phase 1: Catch2 Unit Tests ━━━
All tests passed (139 assertions in 59 test cases)
[PASS] Catch2 unit tests passed

━━━ Phase 2: Starting TinyMUX Server ━━━
[PASS] MUX server is up after 1s

━━━ Phase 3: Module Integration Tests ━━━
...
  TOTAL: 69/69 passed  ✓ all passed

  ALL PHASES PASSED
```

To start an interactive server with Docker:

```bash
docker run --rm -p 4201:4201 -p 4202:4202 tinymux-test server
```

Then connect via telnet on port 4201 or a WebSocket client on port 4202.

---

## Native Build

### Prerequisites

```bash
# Ubuntu / Debian
apt-get install build-essential libssl-dev libcurl4-openssl-dev libpcre2-dev

# macOS (Homebrew)
brew install openssl curl pcre2
```

### Build

```bash
cd mux/src

# Standard configuration (SSL + WebSocket + modules)
./configure --enable-ssl

# Or with optional features
./configure --enable-realitylvls --enable-wodrealms --enable-stubslave --enable-ssl

# Generate dependencies, then build
make depend
make -j$(nproc)

# Build loadable modules
make -C modules -j$(nproc)
```

Output: `mux/src/netmux` (server binary), `mux/src/libmux.so`, `mux/src/modules/*.so`

### Build Options

| Flag | Description |
|---|---|
| `--enable-ssl` | TLS support for player connections and WSS |
| `--enable-realitylvls` | Reality levels (players see different things) |
| `--enable-wodrealms` | World of Darkness realm system |
| `--enable-stubslave` | Stub slave process for background tasks |
| `--enable-memorybased` | In-memory database (no disk I/O) |

---

## Running the Server

```bash
cd mux/game

# Copy your configuration
cp netmux.conf.example netmux.conf   # edit as needed

# Start
./bin/netmux -c netmux.conf

# With custom port
./bin/netmux -c netmux.conf -p 4201
```

Default ports: **4201** (telnet/MUSH), **4202** (WebSocket).

---

## Module System

Modules are loadable shared libraries that extend the server at runtime. Each module registers new softcode functions, commands, or background workers via a COM-style interface.

### Enabling Modules

Add `module <name>` lines to `netmux.conf`:

```
module jsonparse
module httpclient
module execscript
```

### Module Files

Place `.so` files in the same directory as the `netmux` binary, or set `LD_LIBRARY_PATH` accordingly.

```bash
# Copy compiled modules to game bin
cp mux/src/modules/*.so mux/game/bin/
```

---

## New Modules

### jsonparse

JSON parsing and path navigation for softcode.

```mush
think [json_get({"name":"Hero","hp":100},name)]
→ Hero

think [json_get({"vitals":{"hp":42}},vitals.hp)]
→ 42

think [json_get({"items":[{"name":"sword"}]},items[0].name)]
→ sword

think [json_keys({"a":1,"b":2})]
→ a b

think [json_array_len({"items":[1,2,3]},items)]
→ 3

think [json_type({"x":1})]
→ object

think [json_encode(say "hello")]
→ "say \"hello\""

think [json_decode("say \"hello\"")]
→ say "hello"
```

Full reference: [`docs/jsonparse.md`](docs/jsonparse.md)

---

### httpclient

Outbound HTTP/HTTPS requests from softcode. Requires libcurl.

```mush
think [httpget(https://api.example.com/status)]
→ {"status":"ok","version":"1.2"}

think [json_get([httpget(https://api.example.com/player/123)],name)]
→ Hero

think [httppost(https://api.example.com/log,{"event":"login","player":"Hero"})]
→ {"accepted":true}
```

Full reference: [`docs/httpclient.md`](docs/httpclient.md)

---

### execscript

Run trusted shell scripts from the game's `scripts/` directory.

```mush
think [execscript(echo.sh,hello world)]
→ hello world

think [execscript(lookup.sh,playerdata,Hero)]
→ {"level":10,"guild":"Rangers"}
```

Scripts must live in `game/scripts/` and use only safe filename characters. No shell is involved — arguments are passed directly to `execvp`. Wizard-only.

Full reference: [`docs/execscript.md`](docs/execscript.md)

---

## WebSocket Bridge

The server opens a second port (default **4202**) accepting RFC 6455 WebSocket connections. Clients connect and interact with the game exactly as a telnet client would — the bridge transparently converts between WebSocket frames and the MUSH byte stream.

**GMCP support**: GMCP telnet subnegotiations (IAC SB GMCP ... IAC SE) are translated into JSON WebSocket frames:

```json
// WebSocket → Game
{ "type": "gmcp", "package": "Core.Hello", "data": "{\"client\":\"MyClient\"}" }

// Game → WebSocket
{ "type": "gmcp", "package": "Char.Vitals", "data": "{\"hp\":100,\"max_hp\":100}" }
```

Configure in `netmux.conf`:

```ini
ws_enabled     yes
ws_port        4202
wss_port       0        # TLS WebSocket port; 0 = disabled
ws_max_clients 100
```

Full reference: [`docs/websocket.md`](docs/websocket.md)

---

## Testing

### Docker (recommended — zero setup)

```bash
docker build -f docker/Dockerfile -t tinymux-test .
docker run --rm tinymux-test test
```

Three test phases:
1. **Catch2 unit tests** — WebSocket protocol, GMCP bridge, TLS helpers (139 assertions)
2. **MUX server** starts on port 4201
3. **Python integration tests** — connects as Wizard, exercises all 3 modules (69 tests)

### Native

```bash
# Catch2 unit tests
cmake -S mux/src/tests -B mux/src/tests/build -DCMAKE_CXX_STANDARD=17
cmake --build mux/src/tests/build
mux/src/tests/build/ws_tests

# Smoke tests (requires a running server)
cd testcases/tools && ./Makesmoke && ./Smoke
```

Full guide: [`docs/testing.md`](docs/testing.md)

---

## Configuration Reference

### netmux.conf

```ini
port            4201          # Telnet port
port_ssl        0             # TLS telnet port (0 = disabled)
mud_name        MyMUSH        # Server name shown in banners

input_database  data/netmux.db
output_database data/netmux.db.new

# Load modules
module jsonparse
module httpclient
module execscript

# Logging
log_file        data/netmux.log
log_options     all

# WebSocket bridge
ws_enabled      yes           # Set to "no" to disable
ws_port         4202          # Plain WebSocket port (0 = disabled)
wss_port        0             # TLS WebSocket port   (0 = disabled)
ws_certfile                   # PEM certificate path (wss only)
ws_keyfile                    # PEM private key path (wss only)
ws_bind                       # Bind address (empty = all interfaces)
ws_max_clients  100           # Connection limit
```

Full parameter reference: `mux/docs/CONFIGURATION`

---

## Directory Structure

```
tinymux/
├── README.md
├── CLAUDE.md               # Build guidance for Claude Code
├── docs/                   # New system documentation
│   ├── websocket.md
│   ├── jsonparse.md
│   ├── httpclient.md
│   ├── execscript.md
│   └── testing.md
├── docker/
│   ├── Dockerfile          # 2-stage build: builder + runtime
│   ├── entrypoint.sh       # Test runner: Catch2 → MUX → integration tests
│   ├── docker-compose.yml
│   └── game/
│       ├── netmux.conf     # Test server configuration (includes WebSocket settings)
│       └── scripts/        # execscript sandbox scripts
├── mux/
│   ├── docs/               # Legacy server documentation
│   │   ├── CONFIGURATION
│   │   ├── LIMITS
│   │   └── ...
│   ├── game/
│   │   ├── data/           # Database files
│   │   ├── text/           # In-game help and message files
│   │   │   ├── help.txt
│   │   │   ├── wizhelp.txt
│   │   │   └── ...
│   │   └── bin/            # Compiled binaries and modules
│   └── src/
│       ├── netmux          # Main server binary
│       ├── ws_proto.cpp/h  # WebSocket RFC 6455 codec
│       ├── ws_gmcp.cpp/h   # GMCP↔WebSocket bridge
│       ├── ws_config.cpp/h # WebSocket config parser (reads from netmux.conf)
│       └── modules/
│           ├── jsonparse.cpp/h
│           ├── httpclient.cpp/h
│           └── execscript.cpp/h
├── testcases/              # Legacy smoke test suite
└── specs/                  # Protocol specifications
```

---

## Version & Release

Current version: **2.13.0.6** (ALPHA, 2025-04-02)

### Updating version numbers

```bash
# 1. Edit dounix.sh / dowin32.sh: update OLD_BUILD and NEW_BUILD
# 2. Edit mux/src/_build.h: update MUX_VERSION and MUX_RELEASE_DATE
```

### Building a release package

```bash
./dounix.sh    # Linux/macOS — produces .tar.gz, .tar.bz2, .patch.gz, .sha256
./dowin32.sh   # Windows
```

---

## In-Game Help

New functions are documented in the in-game help system:

```
> wizhelp json_get
> wizhelp httpget
> wizhelp httppost
> wizhelp execscript
> wizhelp websocket
```
