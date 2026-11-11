# Proposal

## Why

Six paths have been sitting untracked through the whole refactor series, so `git status` never shows a clean tree and every commit has to spell out what it deliberately skipped. They split into two groups:

**Repository inputs that belong in history**

- `.opencode/plugins/graphify.js` — the plugin that injects the graphify reminder before the first shell command. `AGENTS.md` makes the knowledge graph part of the workflow, and the plugin is what keeps it loaded — yet only the other `.opencode/` content (commands, `openspec-*` skills) is tracked.
- `initial_spec/` — 19 source requirement documents (execution manager, log daemon, communication, PHM, …) written before OpenSpec existed. They are the provenance of the current specs and cannot be regenerated; a scan for secrets/keys found nothing.

**Local, machine-specific leftovers**

- `lens.py` — a standalone Git-project analyzer (`click`, GitPython), unrelated to nexigen.
- `opencode_report.html` — a generated HTML report.
- `pub.log` — a leftover log with a `timeout: failed to execute process` line.
- `.opencode/skills/session_analyzer/` — a generic session-log analyzer skill, not tied to this project.

## What Changes

- Track `.opencode/plugins/graphify.js` and `initial_spec/` (19 files).
- Add the four leftovers to `.gitignore`: `/lens.py`, `/opencode_report.html`, `/pub.log`, `.opencode/skills/session_analyzer/`. The files stay on disk; they simply stop appearing in `git status`.
- No source, build or behavior change.

## Capabilities

No capability deltas: repository hygiene only (`.openspec.yaml` sets `skip_specs: true`).

## Impact

- **Repository:** `git status` becomes clean; the two committed inputs are reproducible and reviewable.
- **Code / behavior:** none. No file under `include/`, `src/`, `tests/`, `tools/`, `examples/` or `cmake/` changes.
- **Performance / latency / memory:** none.
- **Risk:** `initial_spec/` is documentation and adds ~260 KB; the scan confirmed no credentials.
