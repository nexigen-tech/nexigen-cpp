#!/usr/bin/env bash
# Launch the nexigen-exec suite demo from a self-contained deployment layout:
#
#   <root>/bin/nexigen-exec            # all executables in one directory
#   <root>/bin/ex_suite_app
#   <root>/global_settings/           # logging.json metrics.json tracing.json chrono.json
#   <root>/local_settings/<app>/      # per-app local overrides (e.g. degrade/metrics.json)
#   <root>/output/                    # generated logs/metrics/traces + control files
#   <root>/suite.json                 # generated manifest (local_settings resolves here)
#
# The script locates the two executables, generates the manifest, clears the
# output directory, and starts the suite. Global/local settings are read in
# place (missing files fall back to built-in defaults, per the execution_manager).
#
# Overridable via environment (defaults shown):
#   NEXIGEN_DEMO_ROOT                  deployment root (default: current directory)
#   NEXIGEN_DEMO_BIN                   executables directory (default: <root>/bin)
#   NEXIGEN_DEMO_GLOBAL_SETTINGS       global settings (default: <root>/global_settings)
#   NEXIGEN_DEMO_LOCAL_SETTINGS        local settings (default: <root>/local_settings)
#   NEXIGEN_DEMO_OUTPUT                output root (default: <root>/output)
#   NEXIGEN_DEMO_EXIT_AFTER_S          degrade crash interval (default 60)
#   NEXIGEN_DEMO_ABORT_AFTER_S         failure crash interval (default 300)
#   NEXIGEN_DEMO_RESTART_ATTEMPTS      degrade per-app retries (default 3)
#   NEXIGEN_DEMO_RESTART_DELAY_S       degrade retry delay (default 2)
#   NEXIGEN_DEMO_SUITE_RESTART_ATTEMPTS  whole-suite budget (default 1)
#   NEXIGEN_DEMO_SUITE_RESTART_DELAY_S   whole-suite restart delay (default 5)
set -euo pipefail

root_dir="$(cd "${NEXIGEN_DEMO_ROOT:-.}" && pwd)"
bin_dir="${NEXIGEN_DEMO_BIN:-${root_dir}/bin}"
global_settings="${NEXIGEN_DEMO_GLOBAL_SETTINGS:-${root_dir}/global_settings}"
local_settings="${NEXIGEN_DEMO_LOCAL_SETTINGS:-${root_dir}/local_settings}"
output_dir="${NEXIGEN_DEMO_OUTPUT:-${root_dir}/output}"

suite_id="suite-demo"

degrade_exit_s="${NEXIGEN_DEMO_EXIT_AFTER_S:-60}"
failure_abort_s="${NEXIGEN_DEMO_ABORT_AFTER_S:-300}"
restart_attempts="${NEXIGEN_DEMO_RESTART_ATTEMPTS:-3}"
restart_delay_s="${NEXIGEN_DEMO_RESTART_DELAY_S:-2}"
suite_restart_attempts="${NEXIGEN_DEMO_SUITE_RESTART_ATTEMPTS:-1}"
suite_restart_delay_s="${NEXIGEN_DEMO_SUITE_RESTART_DELAY_S:-5}"

# --- locate the two executables in the single bin directory ---
execution_manager_bin="${bin_dir}/nexigen-exec"
app_bin="${bin_dir}/ex_suite_app"
for exe in "${execution_manager_bin}" "${app_bin}"; do
    if [[ ! -x "${exe}" ]]; then
        echo "run_suite_demo_deployed: executable not found: ${exe}" >&2
        echo "run_suite_demo_deployed: place nexigen-exec and ex_suite_app in ${bin_dir}" >&2
        exit 1
    fi
done

run_dir="${output_dir}/run"
artifacts_path="${output_dir}/artifacts"
manifest="${root_dir}/suite.json"

# --- fresh output tree ---
rm -rf -- "${output_dir}"
mkdir -p "${run_dir}" "${artifacts_path}"

# --- generate the manifest (local_settings_path is relative to suite.json) ---
cat > "${manifest}" <<EOF
{
  "version": 1,
  "suite_id": "${suite_id}",
  "run_id": "run-demo",
  "run_dir": "${run_dir}",
  "global_settings_path": "${global_settings}",
  "artifacts_path": "${artifacts_path}",
  "log_level": "info",
  "suite_restart_attempts": ${suite_restart_attempts},
  "suite_restart_delay_s": ${suite_restart_delay_s},
  "apps": [
    {
      "app": "worker",
      "instance": "1",
      "command": "${app_bin}",
      "args": ["--global-settings", "${run_dir}/worker/1/settings"],
      "env": {"NEXIGEN_DEMO_ROLE": "worker", "NEXIGEN_DEMO_RUN_FOREVER": "1"},
      "impact_on_app_suite_operability": "not_impact"
    },
    {
      "app": "degrade",
      "instance": "1",
      "command": "${app_bin}",
      "args": ["--global-settings", "${run_dir}/degrade/1/settings"],
      "env": {"NEXIGEN_DEMO_ROLE": "degrade", "NEXIGEN_DEMO_EXIT_AFTER_S": "${degrade_exit_s}"},
      "restart_attempts": ${restart_attempts},
      "restart_delay_s": ${restart_delay_s},
      "local_settings_path": "local_settings/degrade",
      "impact_on_app_suite_operability": "degrade"
    },
    {
      "app": "failure",
      "instance": "1",
      "command": "${app_bin}",
      "args": ["--global-settings", "${run_dir}/failure/1/settings"],
      "env": {"NEXIGEN_DEMO_ROLE": "failure", "NEXIGEN_DEMO_ABORT_AFTER_S": "${failure_abort_s}"},
      "impact_on_app_suite_operability": "failure"
    }
  ]
}
EOF

echo "run_suite_demo_deployed: manifest=${manifest}"
echo "run_suite_demo_deployed: bin=${bin_dir} global=${global_settings} local=${local_settings}"
echo "run_suite_demo_deployed: starting suite '${suite_id}' (degrade_exit=${degrade_exit_s}s, failure_abort=${failure_abort_s}s)"
"${execution_manager_bin}" start --config "${manifest}"

echo
echo "run_suite_demo_deployed: observe with:"
echo "  ${execution_manager_bin} status --config ${manifest}"
echo "  ${execution_manager_bin} status --suite-dir ${run_dir}/.suite/${suite_id} --json"
echo "run_suite_demo_deployed: stop with:"
echo "  ${execution_manager_bin} stop --config ${manifest}"
echo "run_suite_demo_deployed: clean with:"
echo "  rm -rf ${output_dir} ${manifest}"
