#!/usr/bin/env bash
# Xvfb keyboard-focus test: F2 acts on a focused Log view and is ignored without one.
set -euo pipefail
binary="$1"
fixture="$2"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
export XDG_CONFIG_HOME="${work}/config"
mkdir -p "${XDG_CONFIG_HOME}"

xvfb-run -a "${binary}" --offline "${fixture}" --ui-focus-test | tee "${work}/focus.log"
grep -q 'UI_FOCUS focused=1 unfocused=1' "${work}/focus.log"
