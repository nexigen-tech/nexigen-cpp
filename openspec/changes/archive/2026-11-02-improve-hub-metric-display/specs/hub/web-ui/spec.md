# Spec Delta

## MODIFIED Requirements

### Requirement: Stream viewer supports replay and live modes

The UI SHALL list every discovered stream, show replay records on selection, and then follow live records. It SHALL offer pause/resume, clear, follow-tail, and raw JSON/text display, and SHALL preserve the selected stream while switching views.

#### Scenario: Stream list is complete

- **WHEN** an instance has app, metrics, traces, chrono, stdout, stderr, and runner streams
- **THEN** each is selectable in the viewer

#### Scenario: Replay then live

- **WHEN** a stream with history is selected
- **THEN** replay records render before live records

#### Scenario: Pause stops auto-scroll

- **WHEN** the user pauses the viewer
- **THEN** incoming records do not force scrolling and remain available on resume

#### Scenario: Text and JSONL render appropriately

- **WHEN** stdout/stderr, or a JSONL stream that is not metrics (app, traces, chrono), is selected
- **THEN** the viewer renders the line as raw text and preserves JSON formatting for inspection

## ADDED Requirements

### Requirement: Metrics render structured by kind

When a metrics stream is selected, the UI SHALL parse each JSONL batch and render each harvested metric in a compact, kind-specific form rather than the raw JSON object: counters and gauges as a name and value, descriptive statistics as count/min/max/mean/std, histograms as summary percentiles plus a bucket distribution, and state gauges as a name and value.

#### Scenario: Counter and gauge render as name and value

- **WHEN** a metrics batch contains a counter and a gauge
- **THEN** the viewer shows each as a compact `name = value` line rather than the raw JSON object

#### Scenario: Descriptive statistics render their summary

- **WHEN** a metrics batch contains a descriptive-statistics record
- **THEN** the viewer shows its count, min, max, mean, and stddev in a compact form

#### Scenario: Histogram renders its summary

- **WHEN** a metrics batch contains a histogram
- **THEN** the viewer shows its count, min, max, and percentiles in a compact form

#### Scenario: State renders as a name and value

- **WHEN** a metrics batch contains a state gauge
- **THEN** the viewer shows the state name and its value

#### Scenario: Malformed batch falls back to raw text

- **WHEN** a metrics line cannot be parsed as a batch
- **THEN** the viewer shows the raw line so it remains inspectable
