#!/usr/bin/env bash
# Xvfb click-level UI test: scripted ImGui input for Copy and Open.
set -euo pipefail
binary="$1"
fixture="$2"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
export XDG_CONFIG_HOME="${work}/config"
mkdir -p "${XDG_CONFIG_HOME}"

xvfb-run -a "${binary}" --offline "${fixture}" --ui-input-test | tee "${work}/first.log"
grep -q 'UI_INPUT truncated=.* conflict=0 menu_message=1 menu_record=1' "${work}/first.log"

xvfb-run -a "${binary}" --offline "${fixture}" --ui-input-test | tee "${work}/second.log"
grep -q 'UI_INPUT truncated=.* conflict=0 menu_message=1 menu_record=1 layout=1' "${work}/second.log"
