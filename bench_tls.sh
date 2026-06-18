#!/bin/sh
# Run TLS event loop benchmark
# Usage: ./bench_tls.sh [payload_size] [connections] [duration]

PAYLOAD=${1:-1024}
CONNECTIONS=${2:-10}
DURATION=${3:-5}

cd "$(dirname "$0")/build_debug" 2>/dev/null || cd "$(dirname "$0")/build" 2>/dev/null || {
    echo "Build directory not found. Run: mkdir build && cd build && cmake .. && make"
    exit 1
}

# Auto-detect cert paths
CERT=""
KEY=""
for p in "../test/certs/cert.pem" "../../test/certs/cert.pem" "test/certs/cert.pem"; do
    [ -f "$p" ] && CERT="$p" && break
done
for p in "../test/certs/key.pem" "../../test/certs/key.pem" "test/certs/key.pem"; do
    [ -f "$p" ] && KEY="$p" && break
done

if [ -z "$CERT" ] || [ -z "$KEY" ]; then
    echo "TLS certificates not found. Generate with: cd test/certs && sh gen_certs.sh"
    exit 1
fi

echo "── TLS Benchmark ──────────────────────────────"
echo "  payload=$PAYLOAD  connections=$CONNECTIONS  duration=${DURATION}s"
echo ""

./bin/bench_event_loop --mode tls \
    --payload "$PAYLOAD" \
    --connections "$CONNECTIONS" \
    --duration "$DURATION" \
    --warmup 2 \
    --cert "$CERT" \
    --key "$KEY"
