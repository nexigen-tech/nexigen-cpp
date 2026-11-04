# Spec Delta

## MODIFIED Requirements

### Requirement: Background writer CPU core affinity

`FileLoggerConfig` SHALL carry a `ThreadSpec` for its background writer instead of a uint64 core mask. The writer SHALL apply that spec to itself before opening/starting the sink and before its drain loop, supporting CPUs above 63, naming, and Other/FIFO/RR scheduling. An empty CPU set SHALL preserve inherited affinity. Best-effort failures SHALL remain visible in the startup report without stopping the writer; a `Required` failure SHALL make `initialize()` fail before the drain loop starts and SHALL roll back the partially initialized worker.

#### Scenario: Pinned writer reports the configured mask

- **WHEN** a logger is configured with valid CPUs, a name, and scheduler attributes
- **THEN** the writer applies them before its first file write and its startup report records the result

#### Scenario: CPU above 63 is supported

- **WHEN** the writer spec contains a permitted CPU above 63
- **THEN** that CPU is applied without conversion to a uint64 mask

#### Scenario: Best-effort failure remains non-fatal and visible

- **WHEN** a requested affinity or RT scheduler operation is rejected under `BestEffort`
- **THEN** `writer_start_code()`/the thread report exposes the failure and the writer continues draining with the successfully applied attributes

#### Scenario: Required failure rolls back initialization

- **WHEN** any requested attribute fails under `Required`
- **THEN** `initialize()` returns `SystemError`, the worker is stopped and joined before its drain loop, the sink is not started, and destruction or reinitialization is safe

#### Scenario: Unpinned configuration is valid

- **WHEN** no CPU set is configured
- **THEN** the logger starts with inherited affinity and no pinning call is required to succeed

### Requirement: File logger vocabulary diagnostic strings

`Throttle_config`, `Rotation_config`, `FileLogger_config`, and `Dropped_stats` SHALL each provide a cold-path `std::string to_string() const` (plus matching free `to_string(const T&)`) rendering every field (enabled flag, budgets and windows, schedule and custom period, app/base_dir/file_name/level/max_file_size_bytes/worker_thread/worker_poll_interval_us, per-cause drop totals) for configuration dumps and test evidence. `FileLoggerConfig` SHALL also provide non-throwing JSON round-trip and formatter coverage for its thread spec.

#### Scenario: Config dump is complete

- **WHEN** `to_string()` is called on a populated `FileLogger_config`
- **THEN** the returned string contains the app name, base directory, file name, level name, maximum file size, throttle window, rotation schedule name, worker thread spec, and worker poll interval

#### Scenario: Config thread spec round-trips

- **WHEN** a config whose worker uses a name, a CPU above 63, and RR priority is serialized and parsed under `JSON_NOEXCEPTION=1`
- **THEN** every worker attribute is recovered and `std::format("{}", config)` equals `to_string()`

#### Scenario: Drop snapshot renders

- **WHEN** `to_string()` is called on a `Dropped_stats` with nonzero counters
- **THEN** the string names each cause and count
