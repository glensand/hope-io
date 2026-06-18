#!/bin/sh
# Run TCP event loop benchmark
# Usage: ./bench_tcp.sh [payload_size] [connections] [duration]

PAYLOAD=${1:-64}
CONNECTIONS=${2:-200}
DURATION=${3:-10}

cd "$(dirname "$0")/build_debug" 2>/dev/null || cd "$(dirname "$0")/build" 2>/dev/null || {
    echo "Build directory not found. Run: mkdir build && cd build && cmake .. && make"
    exit 1
}

echo "── TCP Benchmark ──────────────────────────────"
echo "  payload=$PAYLOAD  connections=$CONNECTIONS  duration=${DURATION}s"
echo ""

./bin/bench_event_loop --mode tcp \
    --payload "$PAYLOAD" \
    --connections "$CONNECTIONS" \
    --duration "$DURATION" \
    --warmup 2
