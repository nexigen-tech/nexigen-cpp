# Spec Delta

## ADDED Requirements

### Requirement: Size-based file rotation

`FileLogger` SHALL support an optional maximum active-file size. When `max_file_size_bytes` is non-zero and the active file has reached that size, the background writer alone SHALL open the next numbered segment in the same period directory, write subsequent records there, and close the previous segment without losing or splitting an accepted record.

#### Scenario: Limit reached starts the next segment

- **WHEN** the active segment reaches `max_file_size_bytes` while records keep arriving
- **THEN** subsequent records are written to the next segment in the same directory, and every earlier segment ends with complete JSON lines

#### Scenario: Segment naming preserves the extension

- **WHEN** the configured file name is `app.jsonl` and a second segment is needed
- **THEN** the new file is named `app.1.jsonl`, the following one `app.2.jsonl`, and the first segment keeps the configured `app.jsonl` name

#### Scenario: Zero disables size-based rotation

- **WHEN** `max_file_size_bytes` is `0`
- **THEN** the logger writes a single file per period exactly as before and never creates numbered segments

#### Scenario: Pre-existing content counts toward the limit

- **WHEN** the logger opens a file that already contains data
- **THEN** the existing file size is read at open and counts toward the limit before the first new record is judged

#### Scenario: Producers observe no size-rotation hitch

- **WHEN** producers run a latency benchmark across a size-based segment switch
- **THEN** enqueue latencies show no segment-switch-attributable spike, and the switch performs no heap allocation

#### Scenario: Flush covers the active segment

- **WHEN** `flush()` returns OK after one or more size-based switches
- **THEN** the active segment has been drained and synced; previously closed segments remain complete and durable

## MODIFIED Requirements

### Requirement: File logger vocabulary diagnostic strings

`Throttle_config`, `Rotation_config`, `FileLogger_config`, and `Dropped_stats` SHALL each provide a cold-path `std::string to_string() const` (plus matching free `to_string(const T&)`) rendering every field (enabled flag, budgets and windows, schedule and custom period, app/base_dir/file_name/level/max_file_size_bytes/core_mask, per-cause drop totals) for configuration dumps and test evidence.

#### Scenario: Config dump is complete

- **WHEN** `to_string()` is called on a populated `FileLogger_config`
- **THEN** the returned string contains the app name, base directory, file name, level name, maximum file size, throttle window, and rotation schedule name

#### Scenario: Drop snapshot renders

- **WHEN** `to_string()` is called on a `Dropped_stats` with nonzero counters
- **THEN** the string names each cause and count
