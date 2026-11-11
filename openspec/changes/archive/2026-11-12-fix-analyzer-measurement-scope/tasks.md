# Tasks

## 1. Scope the line counter

- [x] 1.1 Add the `cloc --not-match-d` exclusion for `build*`, `html/`, `graphify-out/` and `.git/` in `analyzer.py`, and verify with `--by-file` that the only remaining C++ directories are `src/`, `include/`, `tests/`, `tools/` and `examples/`.
- [x] 1.2 Run the analyzer and verify `project.lines.code` is within a few percent of a manual `cloc` over `src include tests tools cmake scripts` minus `examples`, and that the file counts match (about 153 `.cpp` and 276 headers for the project, 20 `.cpp` for examples).

## 2. Scope the complexity analysis

- [x] 2.1 Restrict lizard to C++ (`lans=["cpp"]`, with a `TypeError` fallback plus extension post-filter) and drop `alignas` pseudo-functions from the high-complexity list. Verify the project's high-complexity list no longer contains Python helpers or `alignas` entries and the `examples` list still reports the long `main`s.
- [x] 2.2 Cross-check against the gate: the project-level high-complexity list must be empty while `python3 cmake/check_complexity.py` passes, and a synthetic CCN-16 C++ function inside `src/` must appear in both the analyzer output and the gate failure.

## 3. Verification

- [x] 3.1 Run `python3 .opencode/skills/cpp-project-analizer/analyzer.py .` and verify the JSON still parses and exposes exactly the `project`, `examples`, `total` keys with the same metric names.
- [x] 3.2 Verify the analyzer ignores a synthetic `build-probe/sub/dep.cpp` file (created and removed within the task) so a future build tree cannot inflate the report.
- [x] 3.3 Run `check_naming.py`, `check_result_value.py`, `check_complexity.py` and `ctest -R complexity`; verify all green.
- [x] 3.4 Run `openspec validate fix-analyzer-measurement-scope --strict`.

## Workflow follow-up

- Commit the fix, archive with `/opsx-archive`, and push.
