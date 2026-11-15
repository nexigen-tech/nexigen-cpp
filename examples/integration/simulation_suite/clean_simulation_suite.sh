#!/usr/bin/env bash
# Stop every process started by run_simulation_suite.sh and remove the demo
# root. Idempotent: it succeeds when the stack was never started, when it is
# still running, and when the demo root is already gone.
#
# Overridable via environment:
#   BUILD_DIR                 build tree (default <repo>/build-com)
#   NEXIGEN_SIM_DEMO_ROOT     demo root (default /tmp/opencode/nexigen-simulation-suite)
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../../.." && pwd)"
build_dir="${BUILD_DIR:-${repo_root}/build-com}"
# Normalise now: the driver changes into the demo root before starting anything.
build_dir="$(cd "${build_dir}" 2>/dev/null && pwd || printf '%s' "${build_dir}")"
demo_root="${NEXIGEN_SIM_DEMO_ROOT:-/tmp/opencode/nexigen-simulation-suite}"

# Stop the suite first so the Execution Manager terminates its applications.
exec_bin=""
for candidate in \
    "${build_dir}/tools/nexigen-exec" \
    "${build_dir}/debug/tools/nexigen-exec" \
    "${build_dir}/release/tools/nexigen-exec"; do
    if [[ -x "${candidate}" ]]; then
        exec_bin="${candidate}"
        break
    fi
done
if [[ -n "${exec_bin}" && -f "${demo_root}/suite.json" ]]; then
    "${exec_bin}" stop --config "${demo_root}/suite.json" >/dev/null 2>&1 || true
fi

stop_pid() {
    local pidfile="$1"
    if [[ ! -f "${pidfile}" ]]; then
        return 0
    fi
    local pid
    pid="$(cat "${pidfile}" 2>/dev/null || true)"
    if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
        kill -TERM "${pid}" 2>/dev/null || true
        for _ in $(seq 1 50); do
            kill -0 "${pid}" 2>/dev/null || break
            sleep 0.1
        done
        if kill -0 "${pid}" 2>/dev/null; then
            kill -KILL "${pid}" 2>/dev/null || true
        fi
    fi
}

for pidfile in "${demo_root}"/daemons/*.pid; do
    [[ -e "${pidfile}" ]] || continue
    stop_pid "${pidfile}"
done

rm -rf -- "${demo_root}"
echo "clean_simulation_suite: stopped the stack and removed ${demo_root}"
