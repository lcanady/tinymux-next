#!/bin/bash
# =============================================================================
# run_tests.sh — TinyMUX module test orchestrator
#
# Usage:
#   ./scripts/run_tests.sh                 # full suite via Docker
#   ./scripts/run_tests.sh --unit-only     # Catch2 unit tests only (no Docker)
#   ./scripts/run_tests.sh --integ-only    # integration tests against running MUX
#   ./scripts/run_tests.sh --build-only    # build container image, don't run
#
# Requirements:
#   Docker + Docker Compose v2 (or docker compose v1 with 'docker-compose')
#   For --integ-only: running MUX on 127.0.0.1:4201 + mock server on :8765
# =============================================================================

set -euo pipefail
cd "$(dirname "$0")/.."

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BOLD='\033[1m'; RESET='\033[0m'
ok()   { echo -e "${GREEN}${BOLD}[OK]${RESET}    $*"; }
fail() { echo -e "${RED}${BOLD}[FAIL]${RESET}  $*"; exit 1; }
info() { echo -e "${YELLOW}${BOLD}[INFO]${RESET}  $*"; }

# ---- docker / compose detection --------------------------------------------
if command -v docker &>/dev/null && docker compose version &>/dev/null 2>&1; then
    COMPOSE="docker compose"
elif command -v docker-compose &>/dev/null; then
    COMPOSE="docker-compose"
else
    echo "ERROR: docker compose not found" >&2
    exit 1
fi

# ---- argument handling ------------------------------------------------------
MODE="full"
case "${1:-}" in
    --unit-only)  MODE="unit"   ;;
    --integ-only) MODE="integ"  ;;
    --build-only) MODE="build"  ;;
    "")           MODE="full"   ;;
    *)
        echo "Usage: $0 [--unit-only|--integ-only|--build-only]"
        exit 1 ;;
esac

# ---- unit tests (Catch2, run inside Docker) --------------------------------
run_unit_tests() {
    info "Building test container …"
    docker build -t tinymux-test:local \
        -f docker/Dockerfile \
        . 2>&1 | tail -20

    info "Running Catch2 unit tests …"
    docker run --rm tinymux-test:local /mux/tests/ws_tests \
        --reporter console \
        --colour-mode ansi \
        2>&1
    ok "Unit tests passed"
}

# ---- full Docker Compose run -----------------------------------------------
run_full() {
    info "Starting test stack (MUX + mock HTTP server) …"
    $COMPOSE -f docker/docker-compose.yml build 2>&1 | tail -20

    # Start mock server first, wait for health
    $COMPOSE -f docker/docker-compose.yml up -d mock_httpd
    info "Waiting for mock HTTP server to be healthy …"
    local waited=0
    until $COMPOSE -f docker/docker-compose.yml ps mock_httpd \
          | grep -q "healthy"; do
        sleep 2; waited=$((waited+2))
        [ $waited -gt 30 ] && fail "mock_httpd did not become healthy"
    done
    ok "mock_httpd healthy"

    # Run the MUX test container (exits when tests complete)
    info "Running MUX test container …"
    $COMPOSE -f docker/docker-compose.yml run --rm mux test
    local rc=$?

    $COMPOSE -f docker/docker-compose.yml down
    return $rc
}

# ---- integration only (against locally running MUX) ------------------------
run_integ_only() {
    info "Starting mock HTTP server …"
    python3 mux/src/tests/mock_httpd.py --port 8765 &
    HTTPD_PID=$!
    trap "kill $HTTPD_PID 2>/dev/null || true" EXIT

    sleep 1  # give httpd a moment to bind

    info "Running integration tests …"
    python3 mux/src/tests/mux_test_runner.py \
        --host 127.0.0.1 --port 4201 \
        --mock-http-host 127.0.0.1 --mock-http-port 8765 \
        --results /tmp/mux_test_results.xml

    local rc=$?
    kill $HTTPD_PID 2>/dev/null || true
    return $rc
}

# ---- dispatch ---------------------------------------------------------------
echo ""
echo -e "${BOLD}TinyMUX Module Test Suite${RESET}"
echo "================================"
echo "Mode: $MODE"
echo ""

case $MODE in
    unit)
        run_unit_tests
        ;;
    build)
        info "Building container only …"
        docker build -t tinymux-test:local -f docker/Dockerfile . 2>&1 | tail -30
        ok "Build complete"
        ;;
    integ)
        run_integ_only
        ;;
    full)
        run_full
        ;;
esac

echo ""
ok "All done."
