#!/usr/bin/env python3
"""
TinyMUX Module Integration Tests
=================================
Connects to a live TinyMUX instance via socket and validates that the
jsonparse, httpclient, and execscript softcode functions work correctly.

Follows the @rhost/testkit pattern: each test is a named assertion that
returns the result of a softcode expression, compared to an expected value.

Usage:
    python3 mux_test_runner.py [--host HOST] [--port PORT]
                               [--mock-http-host H] [--mock-http-port P]
                               [--results FILE]
"""

import argparse
import socket
import sys
import time
import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from typing import Optional
from datetime import datetime

# ---------------------------------------------------------------------------
# MUX socket connection
# ---------------------------------------------------------------------------

class MuxConnection:
    """Thin wrapper around a raw TCP socket to a TinyMUX game port."""

    RECV_TIMEOUT  = 8.0   # seconds to wait for a marker (default)
    SEND_DELAY    = 0.05  # seconds between sends
    TICK_INTERVAL = 2.0   # seconds between "still waiting" prints

    # Set this before eval() so recv_until() can label its ticks
    current_test: str = ""

    def __init__(self, host: str, port: int):
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.connect((host, port))
        self._sock.settimeout(2.0)
        self._buf = ""

    def _drain(self) -> str:
        """Read everything currently available without blocking."""
        data = ""
        self._sock.settimeout(0.3)
        try:
            while True:
                chunk = self._sock.recv(4096).decode("utf-8", errors="replace")
                if not chunk:
                    break
                data += chunk
        except (socket.timeout, BlockingIOError):
            pass
        self._buf += data
        return data

    def recv_until(self, marker: str, timeout: float = RECV_TIMEOUT) -> str:
        """
        Block until `marker` appears in the receive buffer.
        Prints elapsed-time ticks every TICK_INTERVAL seconds so the user
        can distinguish a slow test from a hang.
        """
        deadline  = time.monotonic() + timeout
        start     = time.monotonic()
        last_tick = start

        while time.monotonic() < deadline:
            wait = min(0.5, deadline - time.monotonic())
            if wait <= 0:
                break
            self._sock.settimeout(wait)
            try:
                chunk = self._sock.recv(4096).decode("utf-8", errors="replace")
                if chunk:
                    self._buf += chunk
            except (socket.timeout, BlockingIOError):
                pass

            if marker in self._buf:
                idx = self._buf.index(marker) + len(marker)
                result = self._buf[:idx]
                self._buf = self._buf[idx:]
                return result

            now = time.monotonic()
            if now - last_tick >= self.TICK_INTERVAL:
                elapsed   = now - start
                remaining = deadline - now
                label = self.current_test or "…"
                # Rewrite the current "[ RUN ]" line with elapsed time
                print(f"\r  [WAIT ] {label}  ({elapsed:.0f}s elapsed, "
                      f"{remaining:.0f}s remaining)   ",
                      end="", flush=True)
                last_tick = now

        raise TimeoutError(f"Timed out after {timeout:.0f}s waiting for marker")

    def send(self, cmd: str):
        self._sock.sendall((cmd + "\r\n").encode())
        time.sleep(self.SEND_DELAY)

    def eval(self, expr: str, timeout: float = 10.0) -> str:
        """
        Evaluate a MUX softcode expression and return its output.

        Uses a start+end marker pair embedded in the think command so that
        any pre-existing output in the buffer (e.g. room description after
        login) is ignored.  Supports multi-line results.

        MUX evaluates: think {sm}{expr}{em}
        Output: ...garbage...<sm><result><em>...
        We extract the text between the two markers.
        """
        uid = int(time.monotonic() * 1000) % 999999
        sm  = f"__EV{uid}S__"
        em  = f"__EV{uid}E__"
        # Wrap expr in [] so MUX evaluates it via the bracket mechanism
        # regardless of what precedes it (the sm prefix would otherwise
        # cause TinyMUX to treat sm+funcname as one identifier).
        self.send(f"think {sm}[{expr}]{em}")
        raw = self.recv_until(em, timeout)
        raw = raw.replace("\r\n", "\n").replace("\r", "\n")
        si  = raw.find(sm)
        ei  = raw.rfind(em)
        if si >= 0 and ei >= si + len(sm):
            return raw[si + len(sm):ei].rstrip()
        # Fallback: strip trailing garbage around end marker
        return raw[:ei].strip() if ei >= 0 else raw.strip()

    def cmd(self, mux_cmd: str, timeout: float = 10.0) -> str:
        """Run a MUX command (no return value expected); drain output."""
        marker = f"__CMD_{int(time.monotonic()*1000)%999999}__"
        self.send(f"{mux_cmd}")
        self.send(f"think {marker}")
        self.recv_until(marker, timeout)

    def login(self, player: str = "Wizard", password: str = "Nyctasia"):
        # Wait for the connect banner (ends with "---...---" dashes).
        # "CONNECTED" is not in the TinyMUX banner, so we look for the dashes.
        try:
            self.recv_until("---", timeout=10.0)
        except TimeoutError:
            pass  # banner may have already arrived; proceed anyway
        self._drain()
        self.send(f"connect {player} {password}")
        time.sleep(1.5)
        self._drain()
        # Verify login by evaluating a trivial expression
        result = self.eval("1", timeout=8.0)
        if result != "1":
            raise RuntimeError(f"Login failed or softcode unavailable (got {result!r})")

    def close(self):
        try:
            self._sock.close()
        except Exception:
            pass


# ---------------------------------------------------------------------------
# Test framework (@rhost/testkit compatible)
# ---------------------------------------------------------------------------

@dataclass
class TestResult:
    name: str
    passed: bool
    expected: str
    actual: str
    error: Optional[str] = None
    elapsed_ms: float = 0.0


@dataclass
class TestSuite:
    name: str
    results: list = field(default_factory=list)

    def record(self, r: TestResult):
        self.results.append(r)
        status = "PASS" if r.passed else "FAIL"
        color  = "\033[32m" if r.passed else "\033[31m"
        reset  = "\033[0m"
        ms     = f"  ({r.elapsed_ms:.0f}ms)"
        # \r overwrites the "[ RUN ]" line that was printed before eval
        print(f"\r  {color}[{status}]{reset} {r.name}{ms}" + " " * 10)
        if not r.passed:
            print(f"         expected: {r.expected!r}")
            print(f"         actual:   {r.actual!r}", end="")
            if r.error:
                print(f"\n         error:    {r.error}", end="")
            print()

    @property
    def passed(self) -> int:
        return sum(1 for r in self.results if r.passed)

    @property
    def failed(self) -> int:
        return sum(1 for r in self.results if not r.passed)


def _run(mux: MuxConnection, suite: TestSuite, name: str,
         expr: str, timeout: float,
         check_fn, expected_repr: str) -> TestResult:
    """
    Core test runner.  Prints the test name *before* eval so the user
    always knows which test is executing.  If the server is slow, the
    recv_until ticks will update the same line.
    """
    mux.current_test = name
    print(f"  [ RUN ] {name}", end="", flush=True)
    t0 = time.monotonic()
    try:
        actual  = mux.eval(expr, timeout=timeout)
        elapsed = (time.monotonic() - t0) * 1000
        passed  = check_fn(actual)
        r = TestResult(name, passed, expected_repr, actual, elapsed_ms=elapsed)
    except Exception as e:
        elapsed = (time.monotonic() - t0) * 1000
        r = TestResult(name, False, expected_repr, "", error=str(e),
                       elapsed_ms=elapsed)
    mux.current_test = ""
    suite.record(r)
    return r


def assert_eval(mux: MuxConnection, suite: TestSuite,
                name: str, expr: str, expected: str,
                timeout: float = 10.0):
    """Assert that expr evaluates to exactly `expected`."""
    _run(mux, suite, name, expr, timeout,
         lambda a: a == expected, repr(expected))


def assert_startswith(mux: MuxConnection, suite: TestSuite,
                      name: str, expr: str, prefix: str,
                      timeout: float = 10.0):
    _run(mux, suite, name, expr, timeout,
         lambda a: a.startswith(prefix), f"starts with {prefix!r}")


def assert_not_startswith(mux, suite, name, expr, prefix, timeout=10.0):
    _run(mux, suite, name, expr, timeout,
         lambda a: not a.startswith(prefix), f"not starting with {prefix!r}")


# ---------------------------------------------------------------------------
# Test Suites
# ---------------------------------------------------------------------------

def suite_jsonparse(mux: MuxConnection) -> TestSuite:
    s = TestSuite("jsonparse")
    print(f"\n[{s.name}]")

    # ---- json_get -----------------------------------------------------------
    assert_eval(mux, s, "json_get: top-level string",
                r'json_get({"name":"Hero"},name)', "Hero")
    assert_eval(mux, s, "json_get: top-level number",
                r'json_get({"hp":100},hp)', "100")
    assert_eval(mux, s, "json_get: top-level bool true",
                r'json_get({"alive":true},alive)', "true")
    assert_eval(mux, s, "json_get: top-level bool false",
                r'json_get({"alive":false},alive)', "false")
    assert_eval(mux, s, "json_get: top-level null",
                r'json_get({"x":null},x)', "null")
    assert_eval(mux, s, "json_get: nested path",
                r'json_get({"vitals":{"hp":42}},vitals.hp)', "42")
    assert_eval(mux, s, "json_get: array index [0]",
                r'json_get({"v":%[10,20,30%]},v%[0%])', "10")
    assert_eval(mux, s, "json_get: array index [2]",
                r'json_get({"v":%[10,20,30%]},v%[2%])', "30")
    assert_eval(mux, s, "json_get: nested array field",
                r'json_get({"items":%[{"name":"sword"}%]},items%[0%].name)', "sword")
    assert_eval(mux, s, "json_get: returns object as JSON",
                r'json_get({"a":{"b":1}},a)', '{"b":1}')
    assert_eval(mux, s, "json_get: missing path returns error",
                r'json_get({"a":1},z)', "#-1 PATH NOT FOUND")
    assert_eval(mux, s, "json_get: bad JSON returns error",
                r'json_get({broken},a)', "#-1 JSON PARSE ERROR")
    assert_eval(mux, s, "json_get: escaped string value",
                r'json_get({"msg":"say \\\"hi\\\""},msg)', 'say "hi"')

    # ---- json_keys ----------------------------------------------------------
    assert_eval(mux, s, "json_keys: two keys",
                r'json_keys({"a":1,"b":2})', "a b")
    assert_eval(mux, s, "json_keys: single key",
                r'json_keys({"only":true})', "only")
    assert_eval(mux, s, "json_keys: nested path",
                r'json_keys({"v":{"hp":1,"mp":2}},v)', "hp mp")
    assert_eval(mux, s, "json_keys: on non-object returns error",
                r'json_keys(%[1%])', "#-1 NOT AN OBJECT")

    # ---- json_array_len -----------------------------------------------------
    assert_eval(mux, s, "json_array_len: 3 elements",
                r'json_array_len({"v":%[1,2,3%]},v)', "3")
    assert_eval(mux, s, "json_array_len: empty array",
                r'json_array_len(%[%])', "0")
    assert_eval(mux, s, "json_array_len: nested path",
                r'json_array_len({"items":%[1,2,3,4%]},items)', "4")
    assert_eval(mux, s, "json_array_len: on non-array returns error",
                r'json_array_len({"a":1})', "#-1 NOT AN ARRAY")

    # ---- json_type ----------------------------------------------------------
    assert_eval(mux, s, "json_type: object",   r'json_type({"a":1})',  "object")
    assert_eval(mux, s, "json_type: array",    r'json_type(%[1%])',    "array")
    assert_eval(mux, s, "json_type: string",   r'json_type("hello")',  "string")
    assert_eval(mux, s, "json_type: number",   r'json_type(42)',       "number")
    assert_eval(mux, s, "json_type: bool",     r'json_type(true)',     "bool")
    assert_eval(mux, s, "json_type: null",     r'json_type(null)',     "null")
    assert_eval(mux, s, "json_type: nested path",
                r'json_type({"v":{"hp":1}},v)', "object")

    # ---- json_encode / json_decode ------------------------------------------
    assert_eval(mux, s, "json_encode: basic string",
                r'json_encode(hello)', '"hello"')
    assert_eval(mux, s, "json_encode: with quotes",
                r'json_encode(say "hi")', r'"say \"hi\""')
    assert_eval(mux, s, "json_encode: with backslash",
                r'json_encode(a\\b)', r'"a\\b"')
    assert_eval(mux, s, "json_decode: strips quotes",
                r'json_decode("hello")', "hello")
    assert_eval(mux, s, "json_decode: unescapes quotes",
                r'json_decode("say \\\"hi\\\"")', 'say "hi"')
    assert_eval(mux, s, "json_decode: unescapes newline",
                r'strlen(json_decode("line1\\nline2"))', "11")

    # ---- safety: bad array index --------------------------------------------
    assert_startswith(mux, s, "json_get: non-numeric array index is error",
                      r'json_get(%[1%],%[ bad %])', "#-1")

    return s


def suite_execscript(mux: MuxConnection) -> TestSuite:
    s = TestSuite("execscript")
    print(f"\n[{s.name}]")

    assert_eval(mux, s, "execscript: echo one arg",
                "execscript(echo.sh,hello)", "hello")
    assert_eval(mux, s, "execscript: echo multiple args",
                "execscript(echo.sh,foo,bar,baz)", "foo bar baz")
    assert_eval(mux, s, "execscript: uppercase script",
                "execscript(upper.sh,hello)", "HELLO")
    assert_eval(mux, s, "execscript: trailing newline stripped",
                "execscript(echo.sh,clean)", "clean")
    assert_eval(mux, s, "execscript: no args returns error",
                "execscript()", "#-1 EXECSCRIPT REQUIRES SCRIPT NAME")
    assert_eval(mux, s, "execscript: path traversal rejected",
                "execscript(../etc/passwd)", "#-1 INVALID SCRIPT NAME")
    assert_eval(mux, s, "execscript: slash in name rejected",
                "execscript(sub/script.sh)", "#-1 INVALID SCRIPT NAME")
    assert_eval(mux, s, "execscript: dot-prefixed name rejected",
                "execscript(.hidden.sh)", "#-1 INVALID SCRIPT NAME")
    assert_eval(mux, s, "execscript: nonexistent script returns error",
                "execscript(no_such_script.sh)",
                "#-1 SCRIPT NOT FOUND OR NOT EXECUTABLE")
    assert_startswith(mux, s, "execscript: bad exec returns #-1",
                      "execscript(no_such_script_xyz.sh)", "#-1")

    return s


def suite_httpclient(mux: MuxConnection, mock_host: str, mock_port: int) -> TestSuite:
    s = TestSuite("httpclient")
    base = f"http://{mock_host}:{mock_port}"
    print(f"\n[{s.name}]  (mock server: {base})")

    # ---- httpget: success ---------------------------------------------------
    assert_eval(mux, s, "httpget: simple JSON body",
                f'httpget({base}/json/simple)',
                '{"name": "Hero", "hp": 100, "alive": true}')

    assert_eval(mux, s, "httpget+json_get: extract name",
                f'json_get([httpget({base}/json/simple)],name)', "Hero")
    assert_eval(mux, s, "httpget+json_get: extract hp",
                f'json_get([httpget({base}/json/simple)],hp)', "100")
    assert_eval(mux, s, "httpget+json_get: nested path",
                f'json_get([httpget({base}/json/nested)],vitals.hp)', "42")
    assert_eval(mux, s, "httpget+json_get: array element",
                f'json_get([httpget({base}/json/nested)],items%[0%].name)', "sword")
    assert_eval(mux, s, "httpget+json_get: array from root",
                f'json_get([httpget({base}/json/array)],%[1%])', "20")

    # ---- httpget: error status codes ----------------------------------------
    assert_eval(mux, s, "httpget: 404 returns #-1 HTTP 404",
                f'httpget({base}/status/404)', "#-1 HTTP 404")
    assert_eval(mux, s, "httpget: 500 returns #-1 HTTP 500",
                f'httpget({base}/status/500)', "#-1 HTTP 500")

    # ---- httpget: auth header -----------------------------------------------
    assert_eval(mux, s, "httpget: 401 without header",
                f'httpget({base}/auth)', "#-1 HTTP 401")
    assert_not_startswith(mux, s, "httpget: 200 with Authorization header",
                          f'httpget({base}/auth,Authorization: Bearer testtoken)',
                          "#-1")

    # ---- httpget: bad URL ---------------------------------------------------
    assert_startswith(mux, s, "httpget: empty URL returns error",
                      "httpget()", "#-1")
    assert_startswith(mux, s, "httpget: unreachable host returns curl error",
                      "httpget(http://127.0.0.1:19999/x)", "#-1 CURL")

    # ---- httppost -----------------------------------------------------------
    assert_not_startswith(mux, s, "httppost: POST echo body",
                          f'httppost({base}/echo,{{"hello":"world"}})', "#-1")
    assert_eval(mux, s, "httppost+json_get: echo body method",
                f'json_get([httppost({base}/echo,{{"x":1}})],method)', "POST")
    assert_eval(mux, s, "httppost: default content-type is application/json",
                f'json_get([httppost({base}/echo,body)],ct)', "application/json")
    assert_eval(mux, s, "httppost: custom content-type",
                f'json_get([httppost({base}/echo,data,text/plain)],ct)', "text/plain")
    assert_eval(mux, s, "httppost: 201 created",
                f'json_get([httppost({base}/status/201,body)],created)', "true")
    assert_eval(mux, s, "httppost: no URL returns error",
                "httppost()", "#-1 HTTPPOST REQUIRES URL AND BODY")

    # ---- output size capping (inline, needs its own pre-print) ---------------
    _cap_name = "httpget: output capped at 8000 bytes"
    mux.current_test = _cap_name
    print(f"  [ RUN ] {_cap_name}", end="", flush=True)
    _t0 = time.monotonic()
    try:
        large_result = mux.eval(f'httpget({base}/large)', timeout=15.0)
        _elapsed = (time.monotonic() - _t0) * 1000
        cap_ok = (0 < len(large_result) <= 8000)
        s.record(TestResult(_cap_name, cap_ok, "0 < len <= 8000",
                            f"len={len(large_result)}", elapsed_ms=_elapsed))
    except Exception as e:
        _elapsed = (time.monotonic() - _t0) * 1000
        s.record(TestResult(_cap_name, False, "0 < len <= 8000", "",
                            error=str(e), elapsed_ms=_elapsed))
    mux.current_test = ""

    # ---- end-to-end: fetch JSON + use in softcode ---------------------------
    assert_eval(mux, s, "e2e: fetch HP and compute half",
                f'sub([json_get([httpget({base}/json/simple)],hp)],50)', "50")

    return s


def suite_websocket_port(mux: MuxConnection) -> TestSuite:
    """Verify the WS port (4202) is open and accepts a TCP connection."""
    s = TestSuite("websocket_port")
    print(f"\n[{s.name}]")

    print("  [ RUN ] ws: port 4202 accepts connection", end="", flush=True)
    try:
        ws_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        ws_sock.settimeout(3.0)
        ws_sock.connect(("127.0.0.1", 4202))
        ws_sock.sendall(
            b"GET / HTTP/1.1\r\n"
            b"Host: localhost\r\n"
            b"Upgrade: websocket\r\n"
            b"Connection: Upgrade\r\n"
            b"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            b"Sec-WebSocket-Version: 13\r\n"
            b"\r\n"
        )
        ws_sock.settimeout(5.0)
        resp = b""
        try:
            while b"\r\n\r\n" not in resp:
                resp += ws_sock.recv(512)
        except socket.timeout:
            pass
        ws_sock.close()

        has_101     = b"101" in resp
        has_upgrade = b"websocket" in resp.lower()
        has_accept  = b"Sec-WebSocket-Accept" in resp

        s.record(TestResult("ws: port 4202 accepts connection",
                            True, "connected", "connected"))
        print("  [ RUN ] ws: responds with 101 Switching Protocols",
              end="", flush=True)
        s.record(TestResult("ws: responds with 101 Switching Protocols",
                            has_101, "101 in response",
                            resp[:100].decode(errors="replace")))
        print("  [ RUN ] ws: response contains Upgrade: websocket",
              end="", flush=True)
        s.record(TestResult("ws: response contains Upgrade: websocket",
                            has_upgrade, "websocket in response",
                            resp[:200].decode(errors="replace")))
        print("  [ RUN ] ws: response contains Sec-WebSocket-Accept",
              end="", flush=True)
        s.record(TestResult("ws: response contains Sec-WebSocket-Accept",
                            has_accept, "Sec-WebSocket-Accept present",
                            resp[:300].decode(errors="replace")))
    except Exception as e:
        s.record(TestResult("ws: port 4202 accepts connection",
                            False, "connected", "", error=str(e)))

    return s


# ---------------------------------------------------------------------------
# JUnit XML reporter
# ---------------------------------------------------------------------------

def write_junit(suites: list, path: str):
    root = ET.Element("testsuites")
    for suite in suites:
        ts = ET.SubElement(root, "testsuite",
                           name=suite.name,
                           tests=str(len(suite.results)),
                           failures=str(suite.failed))
        for r in suite.results:
            tc = ET.SubElement(ts, "testcase",
                               name=r.name,
                               time=f"{r.elapsed_ms/1000:.3f}")
            if not r.passed:
                f = ET.SubElement(tc, "failure", message=r.error or "")
                f.text = (f"Expected: {r.expected!r}\n"
                          f"Actual:   {r.actual!r}\n"
                          + (f"Error:    {r.error}" if r.error else ""))
    tree = ET.ElementTree(root)
    ET.indent(tree, space="  ")
    tree.write(path, encoding="unicode", xml_declaration=True)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="TinyMUX module integration tests")
    parser.add_argument("--host",           default="127.0.0.1")
    parser.add_argument("--port",           type=int, default=4201)
    parser.add_argument("--mock-http-host", default="127.0.0.1", dest="mock_host")
    parser.add_argument("--mock-http-port", type=int, default=8765, dest="mock_port")
    parser.add_argument("--results",        default=None)
    parser.add_argument("--wizard-pass",    default="potrzebie", dest="wizard_pass")
    args = parser.parse_args()

    print(f"Connecting to TinyMUX at {args.host}:{args.port} …", flush=True)
    try:
        mux = MuxConnection(args.host, args.port)
    except Exception as e:
        print(f"Connection failed: {e}", file=sys.stderr)
        sys.exit(1)

    try:
        print("Waiting for CONNECTED banner …", end="", flush=True)
        mux.login("Wizard", args.wizard_pass)
        print(f"\rLogged in as Wizard.          \n", flush=True)
    except Exception as e:
        print(f"\nLogin failed: {e}", file=sys.stderr)
        sys.exit(1)

    suites = []
    try:
        suites.append(suite_jsonparse(mux))
        suites.append(suite_execscript(mux))
        suites.append(suite_httpclient(mux, args.mock_host, args.mock_port))
        suites.append(suite_websocket_port(mux))
    finally:
        mux.close()

    # Summary
    total_pass = sum(s.passed for s in suites)
    total_fail = sum(s.failed for s in suites)
    total      = total_pass + total_fail

    print(f"\n{'='*60}")
    for s in suites:
        bar   = "\033[32m" if s.failed == 0 else "\033[31m"
        reset = "\033[0m"
        print(f"  {bar}{s.name:20s}{reset}  "
              f"{s.passed}/{len(s.results)} passed"
              + (f"  ({s.failed} FAILED)" if s.failed else ""))
    print(f"{'='*60}")
    print(f"  TOTAL: {total_pass}/{total} passed", end="")
    if total_fail:
        print(f"  \033[31m*** {total_fail} FAILED ***\033[0m")
    else:
        print("  \033[32m✓ all passed\033[0m")
    print(f"{'='*60}", flush=True)

    if args.results:
        write_junit(suites, args.results)
        print(f"JUnit results written to: {args.results}", flush=True)

    sys.exit(0 if total_fail == 0 else 1)


if __name__ == "__main__":
    main()
