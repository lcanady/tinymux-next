#!/bin/bash
# =============================================================================
# TinyMUX test container entrypoint
# =============================================================================
# When called with argument "test" (docker-compose default):
#   1. Run Catch2 unit tests
#   2. Start the MUX server
#   3. Run Python integration tests
#   4. Report results and exit with appropriate code
#
# When called with no argument (or "server"):
#   Start the MUX server in the foreground (for interactive use).
# =============================================================================

set -euo pipefail

# Flush Python output immediately (no line-buffering surprises)
export PYTHONUNBUFFERED=1

RESULTS_DIR=/mux/test_results
mkdir -p "$RESULTS_DIR"

PHASE_PASS=0
PHASE_FAIL=0

# ----------------------------------------------------------------------------
# Colour helpers
# ----------------------------------------------------------------------------
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

phase() { echo -e "\n${CYAN}${BOLD}━━━ $* ━━━${RESET}"; }
ok()    { echo -e "${GREEN}[PASS]${RESET} $*"; PHASE_PASS=$((PHASE_PASS+1)); }
fail()  { echo -e "${RED}[FAIL]${RESET} $*"; PHASE_FAIL=$((PHASE_FAIL+1)); }
info()  { echo -e "${YELLOW}[INFO]${RESET} $*"; }

# ----------------------------------------------------------------------------
# 1. Catch2 unit tests  (ws_proto / ws_gmcp / ws_config)
# ----------------------------------------------------------------------------
run_unit_tests() {
    phase "Phase 1: Catch2 Unit Tests"

    if /mux/tests/ws_tests \
           --reporter console \
           --reporter junit::out="$RESULTS_DIR/catch2_results.xml" \
           --colour-mode ansi \
           2>&1 | tee "$RESULTS_DIR/catch2_output.txt"; then
        ok "Catch2 unit tests passed"
    else
        fail "Catch2 unit tests FAILED — check $RESULTS_DIR/catch2_output.txt"
    fi
}

# ----------------------------------------------------------------------------
# 2. Start MUX server
# ----------------------------------------------------------------------------
start_mux() {
    phase "Phase 2: Starting TinyMUX Server"
    cd /mux/game

    # Ensure ws.conf is present
    cp /mux/game/ws.conf . 2>/dev/null || true

    # Start server in background
    ./bin/netmux -c netmux.conf &
    MUX_PID=$!
    echo $MUX_PID > /tmp/mux.pid
    info "netmux PID=$MUX_PID — waiting for port 4201 …"

    local waited=0
    while ! nc -z 127.0.0.1 4201 2>/dev/null; do
        sleep 1
        waited=$((waited+1))

        # Print a dot every second so it's clear we're alive, not hung
        printf "."

        if [ $waited -ge 60 ]; then
            echo ""
            fail "MUX server did not start within 60 seconds"
            info "Last 20 lines of server log:"
            tail -20 /mux/game/logs/netmux.log 2>/dev/null || true
            return 1
        fi
    done

    echo ""
    ok "MUX server is up after ${waited}s (PID=$MUX_PID)"
}

stop_mux() {
    if [ -f /tmp/mux.pid ]; then
        local pid
        pid=$(cat /tmp/mux.pid)
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
        rm -f /tmp/mux.pid
    fi
}

# ----------------------------------------------------------------------------
# 2b. Start mock HTTP server (used by httpclient integration tests)
# ----------------------------------------------------------------------------
start_mock_httpd() {
    local port="${MOCK_HTTP_PORT:-8765}"
    info "Starting mock HTTP server on port ${port} …"
    python3 /mux/tests/mock_httpd.py --port "${port}" &
    MOCK_HTTPD_PID=$!
    echo $MOCK_HTTPD_PID > /tmp/mock_httpd.pid

    local waited=0
    while ! nc -z 127.0.0.1 "${port}" 2>/dev/null; do
        sleep 0.5
        waited=$((waited+1))
        if [ $waited -ge 20 ]; then
            fail "Mock HTTP server did not start within 10 seconds"
            return 1
        fi
    done
    ok "Mock HTTP server up on port ${port} (PID=$MOCK_HTTPD_PID)"
}

stop_mock_httpd() {
    if [ -f /tmp/mock_httpd.pid ]; then
        local pid
        pid=$(cat /tmp/mock_httpd.pid)
        kill "$pid" 2>/dev/null || true
        rm -f /tmp/mock_httpd.pid
    fi
}

# ----------------------------------------------------------------------------
# 3. Python integration tests
# ----------------------------------------------------------------------------
run_integration_tests() {
    phase "Phase 3: Module Integration Tests"

    # Run with stdbuf so per-print flushes make it through tee immediately
    if stdbuf -oL python3 /mux/tests/mux_test_runner.py \
        --host 127.0.0.1 \
        --port 4201 \
        --mock-http-host "${MOCK_HTTP_HOST:-127.0.0.1}" \
        --mock-http-port "${MOCK_HTTP_PORT:-8765}" \
        --results "$RESULTS_DIR/integration_results.xml" \
        2>&1 | tee "$RESULTS_DIR/integration_output.txt"; then
        ok "Integration tests passed"
    else
        fail "Integration tests FAILED — check $RESULTS_DIR/integration_output.txt"
    fi
    # Propagate the python3 exit code, not tee's
    return "${PIPESTATUS[0]}"
}

# ----------------------------------------------------------------------------
# Entry point
# ----------------------------------------------------------------------------
_START=$(date +%s)

case "${1:-server}" in
    test)
        trap 'stop_mux; stop_mock_httpd' EXIT

        echo ""
        echo -e "${BOLD}TinyMUX Module Test Suite${RESET}"
        echo "================================================"

        run_unit_tests
        start_mux
        start_mock_httpd
        run_integration_tests
        stop_mux
        stop_mock_httpd

        _END=$(date +%s)
        _ELAPSED=$(( _END - _START ))

        echo ""
        echo "================================================"
        if [ $PHASE_FAIL -eq 0 ]; then
            echo -e "${GREEN}${BOLD}  ALL PHASES PASSED${RESET}  (${_ELAPSED}s)"
        else
            echo -e "${RED}${BOLD}  ${PHASE_FAIL} PHASE(S) FAILED${RESET}  (${_ELAPSED}s)"
        fi
        echo "================================================"

        [ $PHASE_FAIL -eq 0 ] && exit 0 || exit 1
        ;;

    server)
        info "Starting TinyMUX in server mode"
        cd /mux/game
        exec ./bin/netmux -c netmux.conf
        ;;

    *)
        exec "$@"
        ;;
esac
