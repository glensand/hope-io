#!/bin/sh
# TLS Read Overhead Benchmark — OpenSSL vs BoringSSL comparison
# Usage: ./bench_tls_latency.sh [iterations] [port]
#
# Server writes 200B payload at max speed (no sleep), client reads
# and measures one-way TLS read latency via timestamp embedded in
# the payload itself.

set -e

ITERATIONS=${1:-10000}
PORT=${2:-14443}
WARMUP=${3:-2000}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# ── Certificate check ──────────────────────────────────────────────────
CERT="${SCRIPT_DIR}/test/certs/cert.pem"
KEY="${SCRIPT_DIR}/test/certs/key.pem"
if [ ! -f "$CERT" ] || [ ! -f "$KEY" ]; then
    echo "Generating self-signed certificates..."
    (cd "${SCRIPT_DIR}/test/certs" && sh gen_certs.sh)
fi

# ── Build + run with OpenSSL ──────────────────────────────────────────
echo ""
echo "══════════════════════════════════════════════════════════════"
echo "  1/2  Building & running with OpenSSL..."
echo "══════════════════════════════════════════════════════════════"

mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR"
cmake "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Release -DTLS_LIB_LABEL="OpenSSL" >/dev/null 2>&1
make bench_tls_latency -j"$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)" >/dev/null 2>&1

./bin/bench_tls_latency \
    -n "$ITERATIONS" -w "$WARMUP" -p "$PORT" \
    -c "$CERT" -k "$KEY"

# ── Build + run with BoringSSL (if available) ─────────────────────────
if [ -n "$BORINGSSL_HOME" ] && [ -f "${BORINGSSL_HOME}/libcrypto.a" ]; then
    echo ""
    echo "══════════════════════════════════════════════════════════════"
    echo "  2/2  Building & running with BoringSSL..."
    echo "══════════════════════════════════════════════════════════════"

    PORT=$((PORT + 1))
    cmake "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Release \
        -DTLS_LIB_LABEL="BoringSSL" \
        -DOPENSSL_ROOT_DIR="$BORINGSSL_HOME" \
        -DOPENSSL_INCLUDE_DIR="${BORINGSSL_HOME}/include" \
        -DOPENSSL_LIBRARIES="${BORINGSSL_HOME}/libssl.a;${BORINGSSL_HOME}/libcrypto.a" \
        >/dev/null 2>&1
    make bench_tls_latency -j"$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)" >/dev/null 2>&1

    ./bin/bench_tls_latency \
        -n "$ITERATIONS" -w "$WARMUP" -p "$PORT" \
        -c "$CERT" -k "$KEY"
else
    echo ""
    echo "═══ BoringSSL not found — skipping ════════════════════════"
    echo "  Set \$BORINGSSL_HOME to the BoringSSL build directory"
    echo "  (containing include/, libssl.a, libcrypto.a) to enable."
    echo "════════════════════════════════════════════════════════════"
fi
