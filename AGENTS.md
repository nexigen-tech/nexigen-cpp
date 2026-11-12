## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

When the user types `/graphify`, use the installed graphify skill or instructions before doing anything else.

Rules:
- When graphify-out/graph.json exists, run `graphify query "<question>"` before your first grep, search or source-file read for a codebase question, and put this rule in every subagent prompt that explores code. Read raw files only for lines the graph or an earlier read already located. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- Dirty graphify-out/ files are expected after hooks or incremental updates; dirty graph files are not a reason to skip graphify. Only skip graphify if the task is about stale or incorrect graph output, or the user explicitly says not to use it.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).
- A committed graphify-out/graph.json is rewritten whole on every build, so a large diff to it in a pull request is expected. Do not flag it as churn or suggest skipping the graph rebuild or its hooks.

## build

- Always build with `-j 64` on this machine: use `scripts/build.sh <build-dir...>` (defaults to `-j 64`) or `cmake --build <build-dir> -j 64`. Do not lower parallelism (`-j 2`, `-j 8`) for local builds.
- This is measured, not guessed: 32 physical cores / 64 threads, 122 GiB RAM. Cold full rebuild at `-j 64`: ~21 s (ASan Debug and Release), ~15 s (LTO); `-j 8` is ~4x slower, while `-j 96/128` give no further gain.
- The rule applies to local builds only; leave CI parallelism in `.github/workflows/ci.yml` (`CMAKE_BUILD_PARALLEL_LEVEL`) unchanged.
- Multiple trees can be built at once: `scripts/build.sh build-asan-com build-release-com build-verify-lto` or `--all`; per-build logs go to `<build-dir>/build-script.log`.
