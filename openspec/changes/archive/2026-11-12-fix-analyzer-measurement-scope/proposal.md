# Proposal

## Why

`.opencode/skills/cpp-project-analizer/analyzer.py` is the tool behind every C++ metrics report in this project, and two defects make its output misleading:

1. **`cloc` scans the whole repository root**, including the eleven `build*` directories that hold copied third-party sources. The last report showed 215 705 lines of "project code" instead of 56 748 — a 3.8× overstatement — and 717 `.cpp` files instead of 153. Every reader has to re-measure by hand.
2. **`lizard` analyses every language it recognises**, so the "functions above CCN 15" list mixes C++ with the Python helper scripts (`cmake/check_naming.py`, `check_result_value.py`, the analyzer itself) and with `alignas` pseudo-functions produced by parsing `class alignas(64) T { ... }`. The report shows 6 such entries for a project whose real C++ count is zero.

The complexity gate already solved the second problem (`-l cpp` plus an identifier-scoped `alignas` filter); the analyzer should agree with it.

## What Changes

- Pass `cloc` a directory exclusion for `build*`, `html/`, `graphify-out/` and `.git/`, so only real sources are counted.
- Restrict `lizard` to C++ (`lans=["cpp"]`) and drop `alignas` pseudo-functions from the high-complexity list.
- Keep the JSON contract identical: the same `project` / `examples` / `total` keys, the same metric names, the same `> 15` threshold, so the skill's instructions and any consumer keep working.

## Capabilities

No capability deltas: tooling only, no system behavior change (`.openspec.yaml` sets `skip_specs: true`).

## Impact

- **Code:** `.opencode/skills/cpp-project-analizer/analyzer.py` only.
- **Output:** numbers become correct rather than inflated; the structure does not change. Expected: project code ≈ 57k lines, 153 `.cpp` + 276 headers, zero real C++ functions above CCN 15, and the seven long example `main`s still reported under `examples`.
- **Performance / latency / memory:** none on the product; the analyzer runs faster because it no longer parses the build tree.
- **Risk:** an over-broad exclusion regex could hide a real directory. The pattern only matches basenames starting with `build`, plus `html`, `graphify-out` and `.git`, and the run is compared against a manual `cloc` invocation over the source set.
