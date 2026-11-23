# Tasks

## 1. CLI contract

- [x] 1.1 Add `--ui-scale <factor>` to `CliOptions`, parse a strict full-token float, accept `0.5`–`4.0`, and include the value in `CliOptions::to_string()`.
- [x] 1.2 Add the flag to `--help` and cover `--help` and invalid `--ui-scale` values with the existing CLI tests.

## 2. Window scaling

- [x] 2.1 Auto-detect the primary monitor content scale when no flag is supplied; clamp automatic detection to `1.0`–`4.0`.
- [x] 2.2 Load the embedded JetBrains Mono atlas at `16 * scale`, call `ImGuiStyle::ScaleAllSizes(scale)`, and create the initial window at scaled dimensions.
- [x] 2.3 Document the public CLI/UI fields with `/// @brief`/`@param`/`@return` comments in the touched headers.

## 3. Verification

- [x] 3.1 Add an Xvfb smoke test for `nexigen-logview --ui-smoke --ui-scale 2` alongside the existing GUI tests.
- [x] 3.2 Run Debug, Release, ASan, and TSan model/transport/tool suites; run the GUI Xvfb suite including scaled, scripted-input, and layout tests.
- [x] 3.3 Run clang-format, naming, result-value, JSON-vocabulary, and complexity gates for the touched tool tree.

## 4. Documentation and release

- [x] 4.1 Document the 4K workflow (`--ui-scale 2`, `1.5`–`1.75` alternatives, auto-detection) in `docs/articles/log-viewer.md`.
- [x] 4.2 Run `graphify update .` after the code changes and confirm the graph rebuild completes without errors.

## Workflow follow-up

- Archive this change after review and a final `/opsx-verify`.
