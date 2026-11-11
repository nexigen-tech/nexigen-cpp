# Design

## Context

See `proposal.md` — Why. `.gitignore` already organises its rules by category with anchor notes (build trees, sanitizer dumps, example artifacts, graphify cache). `.opencode/` is partially tracked: `commands/` and the `openspec-*` skills are committed, so adding a plugin and deciding on a second skill needs to fit that pattern.

## Goals / Non-Goals

**Goals:**

- `git status` clean in a normal working tree.
- Keep the workflow inputs (graphify plugin, source requirements) in history.
- Never delete a user's local file just to silence `git status`.

**Non-Goals:**

- Curating or rewriting `initial_spec/` content.
- Adding tooling to the build, tests or CI.
- Removing the ignored files from disk.

## Decisions

### D1 — Commit the graphify plugin, ignore the session analyzer

The plugin is part of the repository's mandated workflow (`AGENTS.md` requires the graph before codebase questions) and is useless to a fresh clone if untracked. The session analyzer is a generic log tool with no nexigen-specific inputs; keeping it ignored avoids committing a personal skill while the environment still uses it.

### D2 — Commit `initial_spec/` as provenance

These documents predate OpenSpec and explain where several current requirements came from. Git is the only place they are versioned today, so losing the working copy loses them. A secret scan (`api_key|secret|password|token`) returned nothing.

### D3 — Ignore the leftovers with anchored, root-scoped patterns

`/lens.py`, `/opencode_report.html`, `/pub.log` are anchored at the repository root so a future file of the same name inside a module is not silently ignored. The skill directory is matched by path (`.opencode/skills/session_analyzer/`) because it is unambiguous. The files remain on disk — ignoring is a statement about history, not about deleting work.

## Risks / Trade-offs

- **`initial_spec/` later diverges from the OpenSpec specs** → it is clearly an input archive, not a live spec tree; the OpenSpec root remains the single source of truth for behavior.
- **A contributor needs the session analyzer** → it can be tracked deliberately later with its own change; nothing here blocks that.
- **Anchored patterns miss a nested copy** → intentional: nested artifacts should be committed or ignored on their own merits.
