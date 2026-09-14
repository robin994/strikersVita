#!/bin/sh
cd "$(dirname "$0")" || exit 1

STRIKERS_BENCHMARK=1 \
STRIKERS_BENCHMARK_SECONDS="${1:-60}" \
STRIKERS_BENCH_RECORD=benchmark-frames.csv \
./strikers 2> benchmark.log
status=$?

sed -n '/=== strikers benchmark/,/^====/p' benchmark.log

if [ "$status" -ne 0 ]; then
    echo "strikers exited $status, see benchmark.log" >&2
    exit "$status"
fi
if ! grep -q '=== strikers benchmark' benchmark.log; then
    echo "no benchmark summary, see benchmark.log" >&2
    exit 1
fi
