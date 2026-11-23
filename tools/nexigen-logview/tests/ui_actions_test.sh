#!/usr/bin/env bash
# Xvfb UI smoke: layout persistence plus copy/truncation/open actions.
set -euo pipefail
binary="$1"
fixture="$2"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
export XDG_CONFIG_HOME="${work}/config"
mkdir -p "${XDG_CONFIG_HOME}"

xvfb-run -a "${binary}" --offline "${fixture}" --ui-smoke
ini="${XDG_CONFIG_HOME}/nexigen/logview-layout.ini"
test -s "${ini}"
xvfb-run -a "${binary}" --offline "${fixture}" --ui-smoke
xvfb-run -a "${binary}" --offline "${fixture}" --ui-actions-test | tee "${work}/actions.log"
grep -q 'truncated=1 copy=.* confirm=1 open=1 imported=1' "${work}/actions.log"
