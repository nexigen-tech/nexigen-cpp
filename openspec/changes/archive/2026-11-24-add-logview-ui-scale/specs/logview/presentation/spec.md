# Spec Delta

## ADDED Requirements

### Requirement: UI scales for high-DPI displays

The viewer SHALL support a process-local UI scale for its font, ImGui style metrics, and initial window size. The scale SHALL be provided explicitly with `--ui-scale <factor>` in the range `0.5`–`4.0`; when absent, the viewer SHALL derive it from the primary monitor content scale and clamp it to `1.0`–`4.0`. The scale SHALL NOT change captured data, the history budget, the transport service, or the export/import format.

#### Scenario: Automatic scale on a high-DPI display

- **WHEN** the viewer starts without `--ui-scale` on a display whose reported content scale is `2.0`
- **THEN** the font, control metrics, and initial window use a `2.0` scale

#### Scenario: Explicit 4K scale

- **WHEN** the operator starts the viewer with `--ui-scale 2`
- **THEN** the embedded font is loaded at twice its base size, ImGui control sizes are doubled, and the initial window is created at twice its base dimensions

#### Scenario: Invalid scale is rejected

- **WHEN** the operator supplies a scale below `0.5`, above `4.0`, or a non-numeric value
- **THEN** the viewer exits with the invalid-argument path and prints usage rather than starting with a guessed scale

#### Scenario: Scale is visible in diagnostics

- **WHEN** the parsed command-line options are rendered with `to_string()`
- **THEN** the numeric UI scale is present in the diagnostic text

#### Scenario: Headless mode is unaffected

- **WHEN** the viewer runs with `--headless` and an explicit `--ui-scale`
- **THEN** the headless model/dump behaviour is unchanged; the scale only applies to windowed rendering
