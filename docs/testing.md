# Testing Guide

TinyMUX has three layers of tests: Catch2 unit tests, Python integration tests, and legacy smoke tests. The Docker test suite runs all three automatically.

---

## Table of Contents

- [Quick Start (Docker)](#quick-start-docker)
- [Test Phases](#test-phases)
  - [Phase 1: Catch2 Unit Tests](#phase-1-catch2-unit-tests)
  - [Phase 2: Server Startup](#phase-2-server-startup)
  - [Phase 3: Integration Tests](#phase-3-integration-tests)
- [Running Tests Natively](#running-tests-natively)
- [Integration Test Reference](#integration-test-reference)
- [Writing New Tests](#writing-new-tests)
- [Troubleshooting](#troubleshooting)

---

## Quick Start (Docker)

The fastest way to run all tests — no dependencies needed beyond Docker:

```bash
# Build and run the full test suite
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
  [PASS] json_get: basic string field
  [PASS] json_get: numeric field
  ...
  TOTAL: 69/69 passed  ✓ all passed

  ALL PHASES PASSED
```

To start an interactive server for manual testing:

```bash
docker run --rm -p 4201:4201 -p 4202:4202 tinymux-test server
# Connect via telnet:
telnet localhost 4201
# Connect as Wizard:
connect Wizard testpass
```

To force a full rebuild (required after C++ source changes):

```bash
docker build --no-cache --target builder -f docker/Dockerfile -t tinymux-builder .
docker build -f docker/Dockerfile -t tinymux-test .
```

---

## Test Phases

### Phase 1: Catch2 Unit Tests

Located in `mux/src/tests/`. Tests the WebSocket protocol codec, GMCP bridge, TLS helpers, and related C++ components without needing a running server.

**What is tested:**

| Test suite | Description |
|---|---|
| WebSocket handshake | HTTP upgrade request/response parsing and `Sec-WebSocket-Accept` header |
| Frame encoder/decoder | RFC 6455 text and binary frames, masking/unmasking, multi-frame messages |
| GMCP bridge | GMCP telnet subnegotiation → JSON frame conversion and back |
| TLS helpers | Certificate loading, connection setup (when built with `--enable-ssl`) |

**Run natively:**

```bash
cmake -S mux/src/tests -B mux/src/tests/build -DCMAKE_CXX_STANDARD=17
cmake --build mux/src/tests/build
./mux/src/tests/build/ws_tests
```

---

### Phase 2: Server Startup

The entrypoint (`docker/entrypoint.sh`) starts the MUX server on port 4201 and waits up to 10 seconds for it to accept connections. If the server doesn't come up, the suite fails immediately.

The test server configuration is in `docker/game/netmux.conf` with:

```ini
port    4201
module  jsonparse
module  httpclient
module  execscript
```

---

### Phase 3: Integration Tests

Located in `mux/src/tests/mux_test_runner.py`. Connects to the running server as Wizard via a raw TCP socket, sends softcode `think` commands, and checks the output against expected values.

A mock HTTP server (`mux/src/tests/mock_httpd.py`) runs on port 8765 to service `httpget`/`httppost` test requests.

**Test suites:**

| Suite | Tests | Description |
|---|---|---|
| `jsonparse` | ~35 | json_get, json_keys, json_array_len, json_type, json_encode, json_decode |
| `httpclient` | ~20 | httpget, httppost, headers, error cases, JSON integration |
| `execscript` | ~14 | script execution, argument passing, error handling, security |

---

## Running Tests Natively

### Prerequisites

```bash
# Ubuntu / Debian
apt-get install build-essential libssl-dev libcurl4-openssl-dev libpcre2-dev cmake python3 netcat-openbsd

# macOS
brew install openssl curl pcre2 cmake python3 netcat
```

### Build the server and modules

```bash
cd mux/src
./configure --enable-ssl
make depend
make -j$(nproc)
make -C modules -j$(nproc)
cp modules/*.so ../game/bin/
```

### Build Catch2 tests

```bash
cmake -S mux/src/tests -B mux/src/tests/build -DCMAKE_CXX_STANDARD=17
cmake --build mux/src/tests/build
./mux/src/tests/build/ws_tests
```

### Run integration tests

```bash
# Start a mock HTTP server
python3 mux/src/tests/mock_httpd.py --port 8765 &

# Start the MUX server
cd mux/game
./bin/netmux netmux.conf &
MUX_PID=$!

# Wait for it to come up
sleep 2

# Run integration tests
python3 mux/src/tests/mux_test_runner.py --host localhost --port 4201

# Clean up
kill $MUX_PID
kill %1  # mock httpd
```

### Legacy smoke tests

```bash
cd testcases/tools
./Makesmoke
./Smoke
# Results in: testcases/smoke.log
```

---

## Integration Test Reference

The test runner (`mux_test_runner.py`) works by:

1. Opening a raw TCP connection to the MUX server
2. Sending `connect Wizard testpass\r\n` to log in
3. For each test: sending `think [<expression>]\r\n` and capturing output
4. Comparing captured output to the expected value (exact string match)

### Mock HTTP server

`mock_httpd.py` serves these endpoints used by the httpclient test suite:

| Endpoint | Method | Response |
|---|---|---|
| `/json/simple` | GET | `{"name":"Hero","hp":100}` |
| `/json/nested` | GET | `{"vitals":{"hp":42,"mp":7}}` |
| `/json/array` | GET | `{"items":[{"name":"sword"},{"name":"shield"}]}` |
| `/echo` | POST | Echoes request body |
| `/status/404` | GET | HTTP 404 |

---

## Writing New Tests

### Adding an integration test

Tests are defined in `mux_test_runner.py` as `(expression, expected_output)` tuples in the relevant suite list.

```python
# In the jsonparse suite:
(r'json_get({"x":42},x)', "42"),
(r'json_get({"x":42},missing)', "#-1 PATH NOT FOUND"),
```

**Key escaping rules:**

- Use raw strings (`r'...'`) to avoid Python backslash processing.
- To pass a literal `\` to MUX: write `\\` in the raw string (`r'json_encode(a\\b)'` → MUX receives `json_encode(a\b)`).
- To pass a literal `"` to MUX: write `\\\"` (`r'json_decode("say \\\"hi\\\"")'` → MUX receives `json_decode("say \"hi\"")`).
- JSON arrays with commas cannot be passed as bare literals to MUX functions — wrap in an object: `{"v":[1,2,3]}` with path `v`.

**Array argument pattern:**

```python
# WRONG — commas inside [] split the argument at depth 0
(r'json_array_len(%[1,2,3%])', "3"),

# CORRECT — wrap in object so commas are at depth > 0
(r'json_array_len({"v":%[1,2,3%]},v)', "3"),
```

### Adding a Catch2 test

Tests are in `mux/src/tests/ws_tests.cpp` (and similar). Add a `TEST_CASE` block:

```cpp
TEST_CASE("My new feature", "[ws_proto]") {
    REQUIRE(my_function(input) == expected);
    REQUIRE(edge_case(empty) == default_val);
}
```

Rebuild with:

```bash
cmake --build mux/src/tests/build
./mux/src/tests/build/ws_tests
```

---

## Troubleshooting

### Docker build doesn't pick up C++ changes

Docker caches the builder layer. Force a rebuild:

```bash
docker build --no-cache --target builder -f docker/Dockerfile -t tinymux-builder .
docker build -f docker/Dockerfile -t tinymux-test .
```

### Server fails to start

Check the MUX log:

```bash
docker run --rm tinymux-test bash -c "cd /mux/game && ./bin/netmux netmux.conf; cat data/netmux.log"
```

Common causes:
- Port 4201 already in use
- Missing module `.so` files in `bin/`
- Bad `netmux.conf` syntax

### Integration tests fail with "Broken pipe" or "Connection refused"

The MUX server crashed. This usually means a softcode expression triggered an unhandled C++ exception. Check:
- The server log for a crash backtrace
- Whether the failing test involves an edge case that throws in C++ (e.g., invalid array index before the stoi try/catch fix)

### `#-1 FUNCTION (X) NOT FOUND`

The module isn't loaded. Verify:
1. The `.so` is in `mux/game/bin/`
2. `module jsonparse` (or `httpclient` / `execscript`) is in `netmux.conf`
3. The server was restarted after editing `netmux.conf`

### Mock HTTP server not responding

The `httpget`/`httppost` tests need the mock server on port 8765. In Docker this is started automatically by `entrypoint.sh`. Natively, start it manually:

```bash
python3 mux/src/tests/mock_httpd.py --port 8765 &
```

---

## Related Topics

- [`docs/jsonparse.md`](jsonparse.md)
- [`docs/httpclient.md`](httpclient.md)
- [`docs/execscript.md`](execscript.md)
- [`docs/websocket.md`](websocket.md)
