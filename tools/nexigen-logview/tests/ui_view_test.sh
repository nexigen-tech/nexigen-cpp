#!/usr/bin/env bash
# Xvfb multi-view test: docking, clone modes, limits, badges, and inspector focus.
set -euo pipefail
binary="$1"
fixture="$2"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
export XDG_CONFIG_HOME="${work}/config"
mkdir -p "${XDG_CONFIG_HOME}"

xvfb-run -a "${binary}" --offline "${fixture}" --ui-view-test | tee "${work}/first.log"
grep -q 'UI_VIEWS restored=1 .*source=1 live_clone=1 paused_clone=1 live_tail=1 nav_default=1 nav_pause=1 levels_isolated=1 focus=1 limit_rejected=1' "${work}/first.log"
grep -q 'UI_VIEWS_CHECKS pause=1 counters=1 eco=1 docked=[1-9].* floating=1 inspector=1 inspector_meta=1 nav=1 nav_hit_areas=3 auto_active=1' "${work}/first.log"
grep -q 'toggle_off=1 toggle_on=1 wheel_off=1' "${work}/first.log"

xvfb-run -a "${binary}" --offline "${fixture}" --ui-view-test | tee "${work}/second.log"
grep -q 'UI_VIEWS restored=8 .*source=1 live_clone=1 paused_clone=1 live_tail=1 nav_default=1 nav_pause=1 levels_isolated=1 focus=1 limit_rejected=1' "${work}/second.log"
grep -q 'UI_VIEWS_CHECKS pause=1 counters=1 eco=1 docked=.* floating=1 inspector=1 inspector_meta=1 nav=1 nav_hit_areas=3 auto_active=1' "${work}/second.log"
grep -q 'toggle_off=1 toggle_on=1 wheel_off=1' "${work}/second.log"
