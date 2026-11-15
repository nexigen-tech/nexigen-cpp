#!/usr/bin/env bash
# Build (when needed) and start the full-stack simulation suite demonstration:
# three telemetry daemons, nexigen-exec running the suite, nexigen-sm owning the
# Simulation Function Group, and nexigen-phmd supervising all three
# applications.
#
# The suite manifest, the State Manager manifest, the Platform Health
# Management health manifest, and the daemon configs are generated under a demo
# root so no machine-specific absolute paths are committed.
#
# Overridable via environment:
#   BUILD_DIR                 build tree (default <repo>/build-com)
#   NEXIGEN_SIM_DEMO_ROOT     demo root (default /tmp/opencode/nexigen-simulation-suite)
#   NEXIGEN_SIM_EXIT_AFTER_S  flaky receiver crash period (default 15)
#   NEXIGEN_SIM_PERIOD_MS     vector period (default 1000)
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../../.." && pwd)"
build_dir="${BUILD_DIR:-${repo_root}/build-com}"
# Normalise now: the driver changes into the demo root before starting anything.
build_dir="$(cd "${build_dir}" 2>/dev/null && pwd || printf '%s' "${build_dir}")"
demo_root="${NEXIGEN_SIM_DEMO_ROOT:-/tmp/opencode/nexigen-simulation-suite}"
exit_after_s="${NEXIGEN_SIM_EXIT_AFTER_S:-15}"
period_ms="${NEXIGEN_SIM_PERIOD_MS:-1000}"
sim_spin="${NEXIGEN_SIM_SPIN:-0}"
rt_priority="${NEXIGEN_SIM_RT_PRIORITY:-0}"

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
        "${build_dir}/examples/integration/simulation_suite/${name}" \
        "${build_dir}/debug/examples/integration/simulation_suite/${name}" \
        "${build_dir}/release/examples/integration/simulation_suite/${name}"; do
        if [[ -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done
    return 1
}

collect_binaries() {
    generator_bin="$(find_app ex_sim_generator || true)"
    receiver_bin="$(find_app ex_sim_receiver || true)"
    flaky_bin="$(find_app ex_sim_flaky_receiver || true)"
    exec_bin="$(find_bin nexigen-exec || true)"
    sm_bin="$(find_bin nexigen-sm || true)"
    phmd_bin="$(find_bin nexigen-phmd || true)"
    logd_bin="$(find_bin nexigen-logd || true)"
    metricsd_bin="$(find_bin nexigen-metricsd || true)"
    traced_bin="$(find_bin nexigen-traced || true)"
}

collect_binaries
if [[ -z "${generator_bin}" || -z "${receiver_bin}" || -z "${flaky_bin}" ||
      -z "${exec_bin}" || -z "${sm_bin}" || -z "${phmd_bin}" ||
      -z "${logd_bin}" || -z "${metricsd_bin}" || -z "${traced_bin}" ]]; then
    echo "run_simulation_suite: building examples and tools in ${build_dir}" >&2
    cmake --build "${build_dir}" --target ex_sim_generator ex_sim_receiver \
        ex_sim_flaky_receiver nexigen-exec nexigen-sm nexigen-phmd \
        nexigen-logd nexigen-metricsd nexigen-traced -j 64
    collect_binaries
fi

for required in "${generator_bin}" "${receiver_bin}" "${flaky_bin}" "${exec_bin}" \
    "${sm_bin}" "${phmd_bin}" "${logd_bin}" "${metricsd_bin}" "${traced_bin}"; do
    if [[ -z "${required}" ]]; then
        echo "run_simulation_suite: a required binary was not found under ${build_dir}" >&2
        exit 1
    fi
done

# A Debug tree measures several times the latency of a Release tree, because
# the application-side work between the timestamp and the publish is compiled
# without optimisation. Say so instead of letting the numbers mislead.
build_type="$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "${build_dir}/CMakeCache.txt" 2>/dev/null)"
if [ "${build_type}" != "Release" ]; then
    echo "run_simulation_suite: note: ${build_dir} is '${build_type:-unspecified}'; latency is several times higher than in a Release tree" >&2
fi

# Prepare the demo root: control files, artifacts, per-app settings, daemon
# output, and the generated configuration.
rm -rf -- "${demo_root}"
mkdir -p "${demo_root}/run" "${demo_root}/artifacts" "${demo_root}/config" \
    "${demo_root}/daemons" "${demo_root}/logs" "${demo_root}/metrics" \
    "${demo_root}/traces"
cp -r "${script_dir}/config/." "${demo_root}/config/"

# The stack runs three applications whose metric sinks all publish on one
# iceoryx2 service, and iceoryx2 allows only two publishers per publish-subscribe
# service by default. iceoryx2 reads a project-local `config/iceoryx2.toml`
# relative to the working directory, so the stack starts from an isolated
# directory carrying that file; the host user's configuration is never touched.
# Every daemon, the Execution Manager, and the applications it launches inherit
# this working directory.
stack_dir="${demo_root}/stack"
mkdir -p "${stack_dir}/config"
cat > "${stack_dir}/config/iceoryx2.toml" <<'TOML'
[defaults.publish-subscribe]
max-publishers = 16
max-subscribers = 32
max-nodes = 64
TOML
cd "${stack_dir}"

global_settings="${script_dir}/settings"
manifest="${demo_root}/suite.json"
sm_manifest="${demo_root}/sm.json"
phm_manifest="${demo_root}/phm.json"

# --- telemetry daemons -----------------------------------------------------
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
  "port": 9101,
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

# --- ExecutionManager suite manifest ---------------------------------------
cat > "${manifest}" <<EOF
{
  "version": 1,
  "suite_id": "simulation-suite",
  "run_id": "run-1",
  "run_dir": "${demo_root}/run",
  "global_settings_path": "${global_settings}",
  "artifacts_path": "${demo_root}/artifacts",
  "log_level": "debug",
  "execution_manager_control_thread": { "cpus": [2], "name": "em-ctrl" },
  "execution_manager_logger_thread": { "cpus": [3], "name": "em-log" },
  "function_groups": [
    { "name": "RootFG", "states": ["Off", "Startup", "Verify", "Run", "Shutdown", "Restart"], "initial_state": "Startup" },
    { "name": "Simulation", "states": ["Off", "Running", "SafeState"], "initial_state": "Running" }
  ],
  "apps": [
    {
      "app": "vector_receiver",
      "instance": "1",
      "command": "${receiver_bin}",
      "function_group": "Simulation",
      "type": "daemon",
      "startup_configs": [
        {
          "state": "Running",
          "args": ["--global-settings", "${demo_root}/run/vector_receiver/1/settings"],
          "env": {"NEXIGEN_SIM_RUN_FOREVER": "1", "NEXIGEN_SIM_PERIOD_MS": "${period_ms}", "NEXIGEN_SIM_SPIN": "${sim_spin}", "NEXIGEN_SIM_RT_PRIORITY": "${rt_priority}"},
          "restart_attempts": 100,
          "restart_delay_s": 1
        }
      ],
      "cpus": [8, 9],
      "allow_rt_priority": true,
      "local_settings_path": "config/receiver",
      "impact_on_app_suite_operability": "not_impact"
    },
    {
      "app": "flaky_receiver",
      "instance": "1",
      "command": "${flaky_bin}",
      "function_group": "Simulation",
      "type": "daemon",
      "startup_configs": [
        {
          "state": "Running",
          "args": ["--global-settings", "${demo_root}/run/flaky_receiver/1/settings"],
          "env": {
            "NEXIGEN_SIM_RUN_FOREVER": "1",
            "NEXIGEN_SIM_EXIT_AFTER_S": "${exit_after_s}",
            "NEXIGEN_SIM_PERIOD_MS": "${period_ms}",
            "NEXIGEN_SIM_SPIN": "${sim_spin}",
            "NEXIGEN_SIM_RT_PRIORITY": "${rt_priority}"
          },
          "restart_attempts": 100,
          "restart_delay_s": 1
        }
      ],
      "cpus": [10, 11],
      "allow_rt_priority": true,
      "local_settings_path": "config/flaky_receiver",
      "impact_on_app_suite_operability": "degrade"
    },
    {
      "app": "vector_generator",
      "instance": "1",
      "command": "${generator_bin}",
      "function_group": "Simulation",
      "type": "daemon",
      "startup_configs": [
        {
          "state": "Running",
          "args": ["--global-settings", "${demo_root}/run/vector_generator/1/settings"],
          "env": {"NEXIGEN_SIM_RUN_FOREVER": "1", "NEXIGEN_SIM_PERIOD_MS": "${period_ms}"},
          "dependencies": [{ "process": "vector_receiver", "state": "running" }],
          "restart_attempts": 100,
          "restart_delay_s": 1
        }
      ],
      "cpus": [6, 7],
      "local_settings_path": "config/generator",
      "impact_on_app_suite_operability": "not_impact"
    }
  ]
}
EOF

# --- StateManager manifest -------------------------------------------------
# `Simulation` is declared with `Running` as its first state so the SM publishes
# it immediately: the SM's application-group validation requires an Off-first
# state, which would leave the demonstration idle without an external trigger.
cat > "${sm_manifest}" <<EOF
{
  "function_groups": [
    {
      "name": "Simulation",
      "states": ["Running", "Off", "SafeState"],
      "allowed_transitions": [
        { "from": "Running", "to": "Off" },
        { "from": "Off", "to": "Running" },
        { "from": "Running", "to": "SafeState" },
        { "from": "SafeState", "to": "Running" }
      ],
      "transition_max_time_ms": 1000,
      "preparation_timeout_ms": 500,
      "application_group": false
    }
  ],
  "requester_priorities": [
    { "requester_id": "phm", "priority": 10 }
  ],
  "metrics_flush_interval_ms": 1000,
  "startup_timeout_ms": 2000,
  "bg_thread": { "cpus": [3], "name": "sm-bg" },
  "audit_thread": { "cpus": [3], "name": "sm-audit" }
}
EOF

# --- PlatformHealthManagement manifest --------------------------------------
cat > "${phm_manifest}" <<EOF
{
  "processes": [
    {
      "name": "vector_generator",
      "service_name": "Nexigen/PHM/Simulation/vector_generator",
      "alive_reference_cycle_ms": 2000,
      "alive_min_margin_ms": 0,
      "alive_max_margin_ms": 0,
      "failed_supervision_cycles_tolerance": 1,
      "checkpoints": [1],
      "transitions": [{ "from_id": 1, "to_id": 1 }],
      "recovery_rules": []
    },
    {
      "name": "vector_receiver",
      "service_name": "Nexigen/PHM/Simulation/vector_receiver",
      "alive_reference_cycle_ms": 2000,
      "alive_min_margin_ms": 0,
      "alive_max_margin_ms": 0,
      "failed_supervision_cycles_tolerance": 1,
      "checkpoints": [1],
      "transitions": [{ "from_id": 1, "to_id": 1 }],
      "recovery_rules": []
    },
    {
      "name": "flaky_receiver",
      "service_name": "Nexigen/PHM/Simulation/flaky_receiver",
      "alive_reference_cycle_ms": 2000,
      "alive_min_margin_ms": 0,
      "alive_max_margin_ms": 0,
      "failed_supervision_cycles_tolerance": 1,
      "checkpoints": [1],
      "transitions": [{ "from_id": 1, "to_id": 1 }],
      "recovery_rules": []
    }
  ],
  "recovery_timeout_ms": 1000,
  "watchdog": { "enabled": false },
  "fault_storage_thread": { "cpus": [3], "name": "phm-faults" }
}
EOF

wait_for_line() {
    local file="$1"
    local needle="$2"
    for _ in $(seq 1 100); do
        if grep -q "${needle}" "${file}" 2>/dev/null; then
            return 0
        fi
        sleep 0.1
    done
    return 1
}

# --- start the stack in dependency order -----------------------------------
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

echo "run_simulation_suite: suite manifest ${manifest}"
echo "run_simulation_suite: starting suite 'simulation-suite'"
"${exec_bin}" start --config "${manifest}"
sleep 1

echo "run_simulation_suite: starting StateManager"
"${sm_bin}" --config "${sm_manifest}" > "${demo_root}/daemons/sm.out" 2>&1 &
echo $! > "${demo_root}/daemons/sm.pid"
if ! wait_for_line "${demo_root}/daemons/sm.out" "nexigen-sm: ready"; then
    echo "run_simulation_suite: StateManager did not report ready" >&2
    cat "${demo_root}/daemons/sm.out" >&2
    exit 1
fi

echo "run_simulation_suite: starting PlatformHealthManagement"
"${phmd_bin}" --manifest "${phm_manifest}" --fault-log "${demo_root}/faults.log" \
    > "${demo_root}/daemons/phmd.out" 2>&1 &
echo $! > "${demo_root}/daemons/phmd.pid"
if ! wait_for_line "${demo_root}/daemons/phmd.out" "supervising 3 process(es)"; then
    echo "run_simulation_suite: PlatformHealthManagement did not start supervision" >&2
    cat "${demo_root}/daemons/phmd.out" >&2
    exit 1
fi

echo
echo "run_simulation_suite: stack is up; the flaky receiver crashes every ${exit_after_s}s"
echo "run_simulation_suite: observe with:"
echo "  ${exec_bin} status --config ${manifest}"
echo "  tail -f ${demo_root}/daemons/logd.out"
echo "  curl -s http://127.0.0.1:9101/metrics"
echo "run_simulation_suite: logs ${demo_root}/logs, metrics ${demo_root}/metrics, traces ${demo_root}/traces"
echo "run_simulation_suite: clean with: ${script_dir}/clean_simulation_suite.sh"
