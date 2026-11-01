#!/usr/bin/env bash
# Build (when needed) and start the multi-application suite demonstration via
# nexigen-run. The manifest is generated under the demo root so no
# machine-specific absolute paths are committed (design D6 of add-suite-demo).
#
# Overridable via environment:
#   BUILD_DIR                         build tree (default <repo>/build)
#   NEXIGEN_DEMO_ROOT                 demo root (default /tmp/opencode/nexigen-suite-demo)
#   NEXIGEN_DEMO_EXIT_AFTER_S         degrade crash interval (default 60)
#   NEXIGEN_DEMO_ABORT_AFTER_S        failure crash interval (default 300)
#   NEXIGEN_DEMO_RESTART_ATTEMPTS     degrade per-app retries (default 3)
#   NEXIGEN_DEMO_RESTART_DELAY_S      degrade retry delay (default 2)
#   NEXIGEN_DEMO_SUITE_RESTART_ATTEMPTS  whole-suite budget (default 1)
#   NEXIGEN_DEMO_SUITE_RESTART_DELAY_S   whole-suite restart delay (default 5)
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
build_dir="${BUILD_DIR:-${repo_root}/build}"
demo_root="${NEXIGEN_DEMO_ROOT:-/tmp/opencode/nexigen-suite-demo}"

suite_id="suite-demo"

degrade_exit_s="${NEXIGEN_DEMO_EXIT_AFTER_S:-60}"
failure_abort_s="${NEXIGEN_DEMO_ABORT_AFTER_S:-300}"
restart_attempts="${NEXIGEN_DEMO_RESTART_ATTEMPTS:-3}"
restart_delay_s="${NEXIGEN_DEMO_RESTART_DELAY_S:-2}"
suite_restart_attempts="${NEXIGEN_DEMO_SUITE_RESTART_ATTEMPTS:-1}"
suite_restart_delay_s="${NEXIGEN_DEMO_SUITE_RESTART_DELAY_S:-5}"

find_app() {
    local candidate
    for candidate in \
        "${build_dir}/examples/suite/ex_suite_app" \
        "${build_dir}/debug/examples/suite/ex_suite_app" \
        "${build_dir}/release/examples/suite/ex_suite_app"; do
        if [[ -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done
    return 1
}

find_runner() {
    local candidate
    for candidate in \
        "${build_dir}/tools/nexigen-run" \
        "${build_dir}/debug/tools/nexigen-run" \
        "${build_dir}/release/tools/nexigen-run"; do
        if [[ -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done
    return 1
}

app_binary="$(find_app || true)"
runner_binary="$(find_runner || true)"

if [[ -z "${app_binary}" || -z "${runner_binary}" ]]; then
    echo "run_suite_demo: building ex_suite_app and nexigen-run in ${build_dir}" >&2
    if ! cmake --build "${build_dir}" --target ex_suite_app nexigen-run; then
        echo "run_suite_demo: build failed" >&2
        exit 1
    fi
    app_binary="$(find_app || true)"
    runner_binary="$(find_runner || true)"
fi

if [[ -z "${app_binary}" ]]; then
    echo "run_suite_demo: ex_suite_app binary not found under ${build_dir}" >&2
    exit 1
fi
if [[ -z "${runner_binary}" ]]; then
    echo "run_suite_demo: nexigen-run binary not found under ${build_dir}" >&2
    exit 1
fi

# Prepare the demo root: control files, artifacts, and the per-app local
# settings (local_settings_path resolves relative to the manifest, which lives
# in the demo root).
rm -rf -- "${demo_root}"
mkdir -p "${demo_root}/run" "${demo_root}/artifacts" "${demo_root}/config"
cp -r "${script_dir}/config/." "${demo_root}/config/"

global_settings="${repo_root}/examples/suite/settings"
run_dir="${demo_root}/run"
artifacts_path="${demo_root}/artifacts"
manifest="${demo_root}/suite.json"

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
      "command": "${app_binary}",
      "args": ["--global-settings", "${run_dir}/worker/1/settings"],
      "env": {"NEXIGEN_DEMO_ROLE": "worker", "NEXIGEN_DEMO_RUN_FOREVER": "1"},
      "impact_on_app_suite_operability": "not_impact"
    },
    {
      "app": "degrade",
      "instance": "1",
      "command": "${app_binary}",
      "args": ["--global-settings", "${run_dir}/degrade/1/settings"],
      "env": {"NEXIGEN_DEMO_ROLE": "degrade", "NEXIGEN_DEMO_EXIT_AFTER_S": "${degrade_exit_s}"},
      "restart_attempts": ${restart_attempts},
      "restart_delay_s": ${restart_delay_s},
      "local_settings_path": "config/degrade",
      "impact_on_app_suite_operability": "degrade"
    },
    {
      "app": "failure",
      "instance": "1",
      "command": "${app_binary}",
      "args": ["--global-settings", "${run_dir}/failure/1/settings"],
      "env": {"NEXIGEN_DEMO_ROLE": "failure", "NEXIGEN_DEMO_ABORT_AFTER_S": "${failure_abort_s}"},
      "impact_on_app_suite_operability": "failure"
    }
  ]
}
EOF

echo "run_suite_demo: manifest=${manifest}"
echo "run_suite_demo: starting suite '${suite_id}' (degrade_exit=${degrade_exit_s}s, failure_abort=${failure_abort_s}s)"
"${runner_binary}" start --config "${manifest}"

echo
echo "run_suite_demo: observe with:"
echo "  ${runner_binary} status --config ${manifest}"
echo "  ${runner_binary} status --suite-dir ${run_dir}/.suite/${suite_id} --json"
echo "run_suite_demo: stop with:"
echo "  ${runner_binary} stop --config ${manifest}"
echo "run_suite_demo: clean with:"
echo "  ${repo_root}/examples/suite/clean_suite_demo.sh"
