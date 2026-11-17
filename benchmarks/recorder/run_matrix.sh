#!/usr/bin/env bash
# Run the storage-format benchmark matrix.
#
# Usage:
#   benchmarks/recorder/run_matrix.sh [--bin PATH] [--out DIR] [--iterations N]
#
# The default binary is build-storage-bench/benchmarks/recorder/nexigen_storage_bench.
# The default output directory is /var/tmp/nexigen-storage-bench (ext4 on the
# review machine). Use a directory on the filesystem you want to measure.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="${ROOT}/build-storage-bench/benchmarks/recorder/nexigen_storage_bench"
OUT="/var/tmp/nexigen-storage-bench"
ITERATIONS=5

while [ $# -gt 0 ]; do
    case "$1" in
        --bin) BIN="${2:?--bin needs a path}"; shift 2 ;;
        --out) OUT="${2:?--out needs a path}"; shift 2 ;;
        --iterations) ITERATIONS="${2:?--iterations needs a value}"; shift 2 ;;
        -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [ ! -x "$BIN" ]; then
    echo "benchmark binary not executable: $BIN" >&2
    exit 2
fi

mkdir -p "$OUT"
rm -f "$OUT/results.tsv"

telemetry=(logs-low logs-high logs-stress spans metrics)
mcap_modes=(mcap-chunked-256k mcap-chunked-1m mcap-unchunked mcap-no-message-index)
nx_modes=(nxrec-batched-64k nxrec-batched-1m)

run_one() { # workload mode sink seconds
    local workload="$1" mode="$2" sink="$3" seconds="$4"
    local verify=()
    if [ "$sink" = "file" ]; then
        verify=(--verify)
    fi
    "$BIN" --workload "$workload" --mode "$mode" --sink "$sink" \
        --iterations "$ITERATIONS" --seconds "$seconds" --out "$OUT" "${verify[@]}"
}

for sink in counting file; do
    for workload in "${telemetry[@]}"; do
        case "$workload" in
            logs-stress) seconds=0.2 ;;
            *) seconds=0.5 ;;
        esac
        for mode in "${mcap_modes[@]}" "${nx_modes[@]}"; do
            echo "=== telemetry ${workload} ${mode} ${sink} ==="
            run_one "$workload" "$mode" "$sink" "$seconds"
        done
    done
done

for sink in counting file; do
    for workload in video-50 video-100 video-200; do
        for mode in mcap-video nxvid-video; do
            echo "=== video ${workload} ${mode} ${sink} ==="
            run_one "$workload" "$mode" "$sink" 0.5
        done
    done
done

echo "=== publish path ==="
set +e
"$BIN" --workload logs-low --mode publish-copy --out "$OUT" | tee "$OUT/publish.txt"
publish_status=${PIPESTATUS[0]}
set -e
if [ "$publish_status" -ne 0 ]; then
    echo "publish benchmark unavailable in this build (COM gate off?)" | tee -a "$OUT/publish.txt"
fi

echo "results: $OUT/results.tsv"
