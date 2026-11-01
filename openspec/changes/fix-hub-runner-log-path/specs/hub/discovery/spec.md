# Spec Delta

## ADDED Requirements

### Requirement: Suite runner log path follows the runner layout

The hub SHALL resolve the suite runner log stream to `<artifacts_path>/<suite_id>/runner.jsonl`, the location `nexigen-run` writes it to, so the runner stream opens and is followed rather than failing with a missing file.

#### Scenario: Runner log resolves under the suite artifact directory

- **WHEN** a suite records `artifacts_path` and `suite_id`, and `nexigen-run` has written `<artifacts_path>/<suite_id>/runner.jsonl`
- **THEN** the hub reports the runner stream with that exact path and follows it

#### Scenario: Runner log stream is discoverable and openable

- **WHEN** the hub discovers a suite whose runner log exists under the suite artifact directory
- **THEN** the stream kind `runner` is present with a non-empty path ending in `<suite_id>/runner.jsonl`, and the initial stream sync succeeds without a missing-file failure

#### Scenario: Missing suite id keeps the runner log unset

- **WHEN** a suite has no recorded `suite_id` or no recorded `artifacts_path`
- **THEN** no runner log stream is reported for that suite
