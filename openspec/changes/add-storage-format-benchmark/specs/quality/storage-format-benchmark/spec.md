# Spec Delta

## Purpose

Defines the reproducible benchmark that compares candidate recorder storage formats (MCAP and the custom `.nxrec` / `.nxidx` / `.nxvid` design) and measures the publish-path cost of the sample header, so the recorder format decision is evidence-based rather than speculative.

## ADDED Requirements

### Requirement: Benchmark is isolated from production artifacts

The benchmark SHALL build only when its opt-in build switch is enabled and MUST NOT change production headers, libraries, tools, default build outputs, or installed artifacts.

#### Scenario: Default build excludes the benchmark

- **WHEN** the project is configured with default options
- **THEN** no benchmark target is built and the production target set is unchanged

#### Scenario: Opt-in build produces only benchmark targets

- **WHEN** the benchmark switch is enabled
- **THEN** the benchmark executables are built beside the production targets without modifying their sources or link interfaces

### Requirement: Workloads are deterministic and identical across formats

Every candidate SHALL consume the same generated payload stream, rates, message sizes, channel mix, and video bitrates. The benchmark SHALL compute a payload checksum and verify that all candidates received identical bytes.

#### Scenario: Identical payload checksum

- **WHEN** the same workload runs against MCAP and the custom prototype
- **THEN** both report the same payload checksum and the same message count

#### Scenario: Telemetry and video workloads cover the operating envelope

- **WHEN** the benchmark runs the telemetry workloads
- **THEN** it covers at least 10k, 100k, and 1M messages per second at 256 B, 384 B, and 4464 B payload sizes
- **AND** when it runs the video workloads, it covers at least 50, 100, and 200 MB/s

### Requirement: Candidate storage modes cover the decision space

The benchmark SHALL measure MCAP in chunked mode, in no-chunking mode, and with message indexing disabled at multiple chunk sizes, and SHALL measure the custom prototype with a sparse index and batched writes. Video SHALL be measured both as MCAP chunked messages and as a 4 KiB-aligned `.nxvid` blob.

#### Scenario: MCAP modes are measured

- **WHEN** the MCAP candidate runs
- **THEN** chunked, no-chunking, and no-message-index modes are each reported with the chunk sizes under test

#### Scenario: Video candidates are measured

- **WHEN** the video workload runs
- **THEN** MCAP chunked messages and the `.nxvid` blob are both measured for the same encoded frame stream

### Requirement: Publish-path header cost is measured

The benchmark SHALL measure the single-call publication path against the path that also writes a user header, on the same payload, and SHALL report median and p99 latency plus any memory delta. It SHALL report whether the producer hot path changes.

#### Scenario: Header publication cost is reported

- **WHEN** the publish-path benchmark runs
- **THEN** it reports median and p99 for the single-call path and for the loan/write/header/send path
- **AND** it reports the additional cost per publication

#### Scenario: Producer hot path is reported separately

- **WHEN** the publish-path benchmark runs
- **THEN** the producer-side enqueue path is measured and reported independently of the consumer-side publication path

### Requirement: Metrics and integrity are reported

The benchmark SHALL report throughput, CPU time, p99 write latency, peak RSS, file size, index size, recovery time, and the active CRC mode. Telemetry SHALL be evaluated with per-record CRC-32C. Video SHALL NOT require per-record CRC-32C; chunk-level or optional CRC is acceptable. The benchmark SHALL fail when a candidate silently drops or corrupts a message.

#### Scenario: Every metric is present

- **WHEN** a benchmark run completes
- **THEN** the report contains throughput, CPU time, p99 write latency, peak RSS, file size, index size, recovery time, and CRC mode for that candidate

#### Scenario: Video does not require per-record CRC-32C

- **WHEN** the video candidate is evaluated
- **THEN** the report accepts chunk-level or optional CRC and does not fail the candidate solely because per-record CRC-32C is absent

#### Scenario: Silent loss is a failure

- **WHEN** a candidate reports fewer messages than the workload produced or a different payload checksum
- **THEN** the benchmark fails and identifies the candidate

### Requirement: Results are reproducible and drive a recommendation

The benchmark SHALL run multiple iterations, report medians, record hardware, operating system, filesystem, compiler, and candidate library revisions, and produce a comparison table with an explicit recommendation. It SHALL NOT change the production storage format.

#### Scenario: Reproducible run metadata is recorded

- **WHEN** a benchmark report is produced
- **THEN** it names the hardware, OS, filesystem, compiler, candidate library revisions, commands, and iteration count used

#### Scenario: Report ends in a recommendation

- **WHEN** all candidates have been measured
- **THEN** the report contains a comparison table and a recommendation for telemetry storage, video storage, and the publish header decision

#### Scenario: No production format changes

- **WHEN** the benchmark change is complete
- **THEN** production storage targets and public headers are unchanged and the format decision remains a follow-up change
