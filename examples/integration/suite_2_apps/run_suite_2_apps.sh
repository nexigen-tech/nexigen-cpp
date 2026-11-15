#!/usr/bin/env bash
# Build (when needed) and start the two-application integration demonstration:
# three telemetry daemons (logd, metricsd, traced) plus nexigen-exec running a
# suite that launches ex_receiver (first) and ex_generator over nexigen::com
# with cross-process trace propagation.
#
# The manifest, daemon configs, and per-app settings are generated under a demo
# root so no machine-specific absolute paths are committed (design D6).
#
# Overridable via environment:
#   BUILD_DIR            build tree (default <repo>/build-com)
#   NEXIGEN_DEMO_ROOT    demo root (default /tmp/opencode/nexigen-suite-2-apps)
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../../.." && pwd)"
build_dir="${BUILD_DIR:-${repo_root}/build-com}"
demo_root="${NEXIGEN_DEMO_ROOT:-/tmp/opencode/nexigen-suite-2-apps}"

find_bin() {
    local name="$1"
    local candidate
    for candidate in \
        "${build_dir}/tools/${name}" \
        "${build_dir}/debug/tools/${name}" \
        "${build_dir}/release/tools/${name}"; do
        if [[ -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done
    return 1
}

find_app() {
    local name="$1"
    local candidate
    for candidate in \
        "${build_dir}/examples/integration/suite_2_apps/${name}" \
        "${build_dir}/debug/examples/integration/suite_2_apps/${name}" \
        "${build_dir}/release/examples/integration/suite_2_apps/${name}"; do
        if [[ -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done
    return 1
}

generator_bin="$(find_app ex_generator || true)"
receiver_bin="$(find_app ex_receiver || true)"
logd_bin="$(find_bin nexigen-logd || true)"
metricsd_bin="$(find_bin nexigen-metricsd || true)"
traced_bin="$(find_bin nexigen-traced || true)"
exec_bin="$(find_bin nexigen-exec || true)"

if [[ -z "${generator_bin}" || -z "${receiver_bin}" || -z "${exec_bin}" ||
      -z "${logd_bin}" || -z "${metricsd_bin}" || -z "${traced_bin}" ]]; then
    echo "run_suite_2_apps: building examples and tools in ${build_dir}" >&2
    cmake --build "${build_dir}" --target ex_generator ex_receiver nexigen-exec \
        nexigen-logd nexigen-metricsd nexigen-traced -j 64
    generator_bin="$(find_app ex_generator || true)"
    receiver_bin="$(find_app ex_receiver || true)"
    logd_bin="$(find_bin nexigen-logd || true)"
    metricsd_bin="$(find_bin nexigen-metricsd || true)"
    traced_bin="$(find_bin nexigen-traced || true)"
    exec_bin="$(find_bin nexigen-exec || true)"
fi

for required in "${generator_bin}" "${receiver_bin}" "${logd_bin}" \
    "${metricsd_bin}" "${traced_bin}" "${exec_bin}"; do
    if [[ -z "${required}" ]]; then
        echo "run_suite_2_apps: a required binary was not found under ${build_dir}" >&2
        exit 1
    fi
done

# Prepare the demo root: control files, artifacts, per-app local settings, and
# daemon config/output directories.
rm -rf -- "${demo_root}"
mkdir -p "${demo_root}/run" "${demo_root}/artifacts" "${demo_root}/config" \
    "${demo_root}/daemons" "${demo_root}/logs" "${demo_root}/metrics" \
    "${demo_root}/traces"
cp -r "${script_dir}/config/." "${demo_root}/config/"

global_settings="${script_dir}/settings"
manifest="${demo_root}/suite.json"

cat > "${demo_root}/daemons/logd.json" <<EOF
{
  "service_name": "Nexigen/Log/nexigen",
  "log_directory": "${demo_root}/logs",
  "min_log_level_storage": "debug",
  "batch_flush_interval_ms": 50,
  "bg_cpu_affinity": { "cpus": [4], "name": "logd" }
}
EOF

cat > "${demo_root}/daemons/metricsd.json" <<EOF
{
  "service_name": "nexigen/metrics/stream",
  "storage_directory": "${demo_root}/metrics",
  "bind_address": "127.0.0.1",
  "port": 9100,
  "batch_flush_interval_ms": 100,
  "bg_cpu_affinity": { "cpus": [4], "name": "metricsd" },
  "http_thread": { "cpus": [5], "name": "metricsd-http" }
}
EOF

cat > "${demo_root}/daemons/traced.json" <<EOF
{
  "service_name": "nexigen/trace/stream",
  "dump_directory": "${demo_root}/traces",
  "drain_interval_ms": 20,
  "bg_cpu_affinity": { "cpus": [5], "name": "traced" }
}
EOF

cat > "${manifest}" <<EOF
{
  "version": 1,
  "suite_id": "suite-2-apps",
  "run_id": "run-1",
  "run_dir": "${demo_root}/run",
  "global_settings_path": "${global_settings}",
  "artifacts_path": "${demo_root}/artifacts",
  "log_level": "debug",
  "execution_manager_control_thread": { "cpus": [2], "name": "em-ctrl" },
  "execution_manager_logger_thread": { "cpus": [3], "name": "em-log" },
  "function_groups": [
    { "name": "RootFG", "states": ["Off", "Startup", "Verify", "Run", "Shutdown", "Restart"], "initial_state": "Startup" },
    { "name": "demo", "states": ["Off", "Running"], "initial_state": "Running" }
  ],
  "apps": [
    {
      "app": "receiver",
      "instance": "1",
      "command": "${receiver_bin}",
      "function_group": "demo",
      "startup_configs": [
        {
          "state": "Running",
          "args": ["--global-settings", "${demo_root}/run/receiver/1/settings"],
          "env": {"NEXIGEN_RUN_FOREVER": "1"}
        }
      ],
      "cpus": [8, 9],
      "local_settings_path": "config/receiver",
      "impact_on_app_suite_operability": "not_impact"
    },
    {
      "app": "generator",
      "instance": "1",
      "command": "${generator_bin}",
      "function_group": "demo",
      "startup_configs": [
        {
          "state": "Running",
          "args": ["--global-settings", "${demo_root}/run/generator/1/settings"],
          "env": {"NEXIGEN_RUN_FOREVER": "1"},
          "dependencies": [{ "process": "receiver", "state": "running" }]
        }
      ],
      "cpus": [6, 7],
      "local_settings_path": "config/generator",
      "impact_on_app_suite_operability": "not_impact"
    }
  ]
}
EOF

# Start the three daemons, then the suite.
"${logd_bin}" --config "${demo_root}/daemons/logd.json" \
    > "${demo_root}/daemons/logd.out" 2>&1 &
echo $! > "${demo_root}/daemons/logd.pid"
"${metricsd_bin}" --config "${demo_root}/daemons/metricsd.json" \
    > "${demo_root}/daemons/metricsd.out" 2>&1 &
echo $! > "${demo_root}/daemons/metricsd.pid"
"${traced_bin}" --config "${demo_root}/daemons/traced.json" \
    > "${demo_root}/daemons/traced.out" 2>&1 &
echo $! > "${demo_root}/daemons/traced.pid"

sleep 1

echo "run_suite_2_apps: manifest=${manifest}"
echo "run_suite_2_apps: starting suite 'suite-2-apps'"
"${exec_bin}" start --config "${manifest}"

echo
echo "run_suite_2_apps: observe with:"
echo "  ${exec_bin} status --config ${manifest}"
echo "  curl -s http://127.0.0.1:9100/metrics"
echo "run_suite_2_apps: logs at ${demo_root}/logs, metrics at ${demo_root}/metrics"
echo "run_suite_2_apps: clean with: ${script_dir}/clean_suite_2_apps.sh"
