# Design

## Context

See `proposal.md` — Why. Facts that constrain the fix, verified against the installed tools:

- `cloc 2.02` supports `--not-match-d=REGEX`, matched against the directory path. `--not-match-d='(^|/)(build[^/]*|html|graphify-out|\.git)(/|$)'` reduces the C++/header file count from 743+589 to 179+276 over the whole tree, and a `--by-file` run confirms the remaining files live only in `src/`, `include/`, `tests/`, `tools/` and `examples/`.
- `lizard.analyze(paths, ..., lans=None, use_gitignore=True)`: `lans=["cpp"]` restricts the language; `use_gitignore=True` is already the default, which is why the build tree never reached the complexity numbers.
- The analyzer's JSON shape is a contract: the skill instructions reference `project`, `examples`, `total`, and the per-key `files`/`lines`/`complexity`/`tech_debt` objects.

## Goals / Non-Goals

**Goals:**

- Line, file and function counts reflect the real sources.
- The high-complexity list matches what the project's own gate would report (C++ only, `alignas` filtered).
- Unchanged JSON schema and unchanged CLI (`analyzer.py <path>`).

**Non-Goals:**

- Redesigning the report, adding new metrics or changing the `> 15` threshold.
- Changing the skill instructions or the output sections the agent must produce.
- Deduplicating the `alignas` filter with `cmake/check_complexity.py`: the analyzer is a standalone script vendored under `.opencode/`, and importing from `cmake/` would couple it to a repository path.

## Decisions

### D1 — Exclusion via one `--not-match-d` regex, not a per-directory list

The repository holds eleven `build*` directories today and more will appear (`build-verify-lto`, `build-coverage`). A regex on the basename keeps the analyzer correct as new build trees appear, while `--exclude-dir` would need maintenance. The pattern is anchored on path separators so a directory merely *containing* "build" is not excluded.

### D2 — `lans=["cpp"]` with a compatibility fallback

The current lizard accepts `lans`; older installations may not. Wrap the call in `try/except TypeError` and fall back to the unfiltered analysis with a post-filter on the file extension, so the script keeps working on an older lizard instead of failing.

### D3 — Filter `alignas` by identifier, mirroring the gate

A function whose last namespace component is `alignas` or starts with `alignas(` is a parser artefact of `class alignas(64) T`. The gate already filters exactly this way; keeping the analyzer consistent means the report's "functions above 15" and `ctest -R complexity` always agree.

### D4 — Verify by cross-check, not by trusting the change

After the fix, compare `project` metrics against a manual `cloc` over `src include tests tools cmake scripts` minus `examples`, and compare the high-complexity list against `python3 cmake/check_complexity.py`. They must agree; a discrepancy means the regex or the language filter is wrong.

## Risks / Trade-offs

- **Regex accidentally excludes a real directory** → checked with a `--by-file` listing; only the five source trees remain.
- **`lans` spelling differs across lizard versions** → the fallback path keeps behavior; if neither works, the script still produces output (the extension filter runs after analysis only when `lans` is unsupported).
- **Numbers change and someone compares against an old report** → the proposal records the expected magnitudes, and the skill report now includes both the corrected numbers and a note that the previous inflation was a tooling artefact.
