# Spec Delta

## ADDED Requirements

### Requirement: Built-in frame descriptors match the wire frames

Built-in descriptors for `nexigen.log.LogFrame`, `nexigen.metrics.MetricFrame`, and
`nexigen.tracing.SpanFrame` SHALL enumerate every field of the corresponding wire frame with its
offset, element size, count, and stride, and SHALL be pinned to the C++ layout by compile-time size
and offset checks. A descriptor that omits a frame field SHALL fail the build.

#### Scenario: Metrics descriptor is complete

- **WHEN** the built-in schema table is inspected
- **THEN** `nexigen.metrics.MetricFrame` declares the application code, process id, timestamp, and
  every field of the embedded metric record

#### Scenario: Log and span descriptors match their frames

- **WHEN** the built-in descriptors are compared with `LogFrame` and `SpanFrame`
- **THEN** each declared offset and width equals the corresponding `offsetof`/`sizeof` in the frame

#### Scenario: Layout drift fails the build

- **WHEN** a frame field is moved, resized, or added without updating its descriptor
- **THEN** the compile-time check rejects the build

#### Scenario: Exported metadata lists every field

- **WHEN** `nexigen-export info` reports a built-in frame dataset
- **THEN** its field list matches the descriptor, including the metric record's fields

#### Scenario: Descriptor remains self-contained

- **WHEN** a session with built-in frame datasets is copied to another machine
- **THEN** decoding needs only the embedded descriptor bytes and no external schema file
