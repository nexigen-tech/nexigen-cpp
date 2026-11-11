# Tasks

## 1. Track the workflow inputs

- [x] 1.1 Stage `.opencode/plugins/graphify.js` and verify it is the only plugin file and that `git status` no longer lists `.opencode/plugins/` as untracked.
- [x] 1.2 Stage `initial_spec/` (19 files) and verify the staged set contains only Markdown/YAML/text requirement documents and no binary or credential material.

## 2. Ignore the local leftovers

- [x] 2.1 Add `/lens.py`, `/opencode_report.html`, `/pub.log` and `.opencode/skills/session_analyzer/` to `.gitignore` under a clearly labelled section, and verify all four files still exist on disk after the change.
- [x] 2.2 Verify `git status --short` reports no untracked paths with the committed and ignored sets in place.

## 3. Verification

- [x] 3.1 Verify `git ls-files initial_spec | wc -l` equals the file count on disk (19) and that `git check-ignore -v` reports each of the four ignored paths.
- [x] 3.2 Run `openspec validate track-project-inputs-and-tooling --strict`.
- [x] 3.3 Verify the working tree contains no source changes: `git diff --stat` on `include/ src/ tests/ tools/ examples/ cmake/` is empty.

## Workflow follow-up

- Commit, archive with `/opsx-archive`, and push.
