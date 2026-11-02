# hub/web-ui Specification

## Purpose

Defines the embedded browser UI for inspecting the discovered run tree, suite and instance state, and replayed or live stream records through the hub's HTTP and SSE endpoints.

## Requirements

### Requirement: UI is a single embedded application

The UI SHALL be plain HTML/CSS/JavaScript embedded in the `nexigen-hub` binary. It SHALL load without a build step, external CDN, external font, or any network request other than the hub's own origin.

#### Scenario: UI loads from the binary

- **WHEN** a browser opens `/`
- **THEN** the complete UI loads from the hub origin with no external asset requests

#### Scenario: UI works without internet

- **WHEN** the machine has no outbound network access
- **THEN** the UI still renders and updates

### Requirement: Run tree reflects discovery

The UI SHALL display run → suite → app → instance → attempt hierarchy with suite and application states, attempt numbers, and artifact locations. It SHALL update automatically when discovery adds or removes entries.

#### Scenario: Existing tree is shown

- **WHEN** the UI connects to a hub with discovered suites
- **THEN** every run, suite, application, instance, and attempt is visible

#### Scenario: New suite appears live

- **WHEN** a suite is created while the UI is open
- **THEN** it appears without a page reload

#### Scenario: Removed entry disappears

- **WHEN** an instance is removed while the UI is open
- **THEN** it disappears without a page reload

### Requirement: State panels show runner-derived details

The UI SHALL show aggregate suite state, per-application state, restart usage, current attempt, artifact directory, and stop/failure reason when available. It SHALL present `unknown` explicitly rather than inventing a state.

#### Scenario: Running suite details

- **WHEN** a running suite is selected
- **THEN** its aggregate and per-application states, attempts, and artifact paths are shown

#### Scenario: Degraded and failed suites are distinguishable

- **WHEN** a suite is recorded as `degraded` or `failed`
- **THEN** the UI displays that state and the available reason

#### Scenario: Unknown state is explicit

- **WHEN** no state record exists
- **THEN** the UI displays `unknown`

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

### Requirement: Connection and error states are visible

The UI SHALL show SSE connection status and reconnect after a dropped connection using the last received sequence where possible. It SHALL display empty-run, unknown-stream, and server-error states without breaking the page.

#### Scenario: Disconnect is shown

- **WHEN** the SSE connection drops
- **THEN** the UI displays a disconnected indicator and retries

#### Scenario: Reconnect resumes near the cursor

- **WHEN** the connection returns after a short drop
- **THEN** the UI requests records after the last sequence and avoids duplicating them

#### Scenario: Empty hub is handled

- **WHEN** the hub has no runs
- **THEN** the UI shows an explicit empty state

### Requirement: UI is read-only

The UI SHALL NOT present start, stop, restart, signal, delete, or edit controls for applications, suites, streams, or runner metadata.

#### Scenario: No control buttons exist

- **WHEN** an operator opens the UI
- **THEN** no action can start, stop, or modify a supervised process

#### Scenario: Viewing does not change state

- **WHEN** the operator navigates the tree and streams
- **THEN** no runner or artifact file is modified

### Requirement: UI work stays off application hot paths

The UI, SSE handling, and rendering SHALL execute in the browser and the hub process only. They MUST NOT add work to application ticks, producers, snapshots, or sinks.

#### Scenario: Application ticks are unaffected

- **WHEN** a supervised application runs its tick loop
- **THEN** no UI, SSE, or rendering work occurs in that loop

### Requirement: Embedded script parses and bootstraps

The UI's embedded script SHALL be valid JavaScript that executes on load: it SHALL register the SSE tree subscription and the initial tree fetch, and it SHALL transition the connection badge out of its initial `connecting` state once the hub serves the tree.

#### Scenario: Script parses without error

- **WHEN** the embedded script is loaded by a browser
- **THEN** it parses without a syntax error and its top-level statements execute

#### Scenario: Connection badge leaves the initial state

- **WHEN** a browser opens `/` and the hub serves the tree
- **THEN** the connection badge transitions from `connecting` to `connected` (or an explicit error state) without a page reload

#### Scenario: Initial tree is fetched and rendered

- **WHEN** the script bootstraps against a hub with discovered suites
- **THEN** the run tree is fetched and rendered without requiring the user to select a stream first

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
