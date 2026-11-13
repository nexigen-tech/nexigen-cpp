#!/usr/bin/env bash
# Stop the two-application integration demonstration: halt the suite and the
# three daemons, then remove the generated demo root. Idempotent: exiting
# successfully when the demo root is already absent.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../../.." && pwd)"
build_dir="${BUILD_DIR:-${repo_root}/build-com}"
demo_root="${NEXIGEN_DEMO_ROOT:-/tmp/opencode/nexigen-suite-2-apps}"

exec_bin="${build_dir}/tools/nexigen-exec"
manifest="${demo_root}/suite.json"

# Stop the suite first (graceful shutdown of both applications).
if [[ -f "${manifest}" && -x "${exec_bin}" ]]; then
    "${exec_bin}" stop --config "${manifest}" >/dev/null 2>&1 || true
fi

# Stop the daemons by pid when present.
for name in logd metricsd traced; do
    pid_file="${demo_root}/daemons/${name}.pid"
    if [[ -f "${pid_file}" ]]; then
        pid="$(cat "${pid_file}")"
        if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
            kill -TERM "${pid}" 2>/dev/null || true
            for _ in $(seq 1 20); do
                kill -0 "${pid}" 2>/dev/null || break
                sleep 0.1
            done
            kill -KILL "${pid}" 2>/dev/null || true
        fi
        rm -f "${pid_file}"
    fi
done

rm -rf -- "${demo_root}"
echo "clean_suite_2_apps: removed ${demo_root}"
