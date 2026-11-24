#!/usr/bin/env bash
# Xvfb startup splash test: decode, ingestion during splash, non-interactive input,
# workspace hand-off, high-DPI scaling, and decode-failure fallback.
set -euo pipefail
binary="$1"
fixture="$2"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
export XDG_CONFIG_HOME="${work}/config"
mkdir -p "${XDG_CONFIG_HOME}"

NEXIGEN_LOGVIEW_SPLASH_TEST_INGEST=1 NEXIGEN_LOGVIEW_SPLASH_TEST_INPUT=1 \
    xvfb-run -a "${binary}" --offline "${fixture}" --ui-splash-test | tee "${work}/splash.log"
grep -q 'UI_SPLASH width=1360 height=768 texture=1 frames=[1-9]' "${work}/splash.log"
grep -q 'aspect_fit=1' "${work}/splash.log"
grep -q 'records=[1-9]' "${work}/splash.log"
grep -q 'ingested=[1-9][0-9]*' "${work}/splash.log"
grep -q 'input_ignored=1' "${work}/splash.log"
grep -q 'elapsed_ms=1[0-9][0-9][0-9]' "${work}/splash.log"
grep -q 'UI_SPLASH_WORKSPACE frames=6 records=[1-9][0-9]* views=[1-9][0-9]* stable=1' "${work}/splash.log"

xvfb-run -a "${binary}" --offline "${fixture}" --ui-splash-test --ui-scale 2 \
    | tee "${work}/hidpi.log"
grep -q 'UI_SPLASH width=1360 height=768 texture=1 frames=[1-9]' "${work}/hidpi.log"
grep -q 'aspect_fit=1' "${work}/hidpi.log"
grep -q 'UI_SPLASH_WORKSPACE frames=6 records=[1-9] views=[1-9][0-9]* stable=1' "${work}/hidpi.log"

NEXIGEN_LOGVIEW_SPLASH_TEST_FAIL=1 xvfb-run -a "${binary}" --offline "${fixture}" --ui-splash-test | tee "${work}/fallback.log"
grep -q 'UI_SPLASH fallback=1' "${work}/fallback.log"
