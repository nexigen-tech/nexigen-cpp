# Graph Report - nexigen-cpp  (2026-11-12)

## Corpus Check
- 1312 files · ~824,964 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 35 file(s) not represented in the graph (top: (none) 26, .cmake 8, .in 1)

## Summary
- 18836 nodes · 32648 edges · 1049 communities (936 shown, 113 thin omitted)
- Extraction: 87% EXTRACTED · 13% INFERRED · 0% AMBIGUOUS · INFERRED: 4167 edges (avg confidence: 0.93)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `13c62225`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- string
- format
- optional
- SpanStatus
- daemon_components_test.cpp
- SuiteManifest
- format_param_t
- ResetPolicy
- tracer_provider.hpp
- Snapshot
- ResourceMetadata
- MetricProviderConfig
- HttpServer::Impl
- MetricIdentity
- HistogramSnapshot
- MetricRecord
- NullTraceSink
- LoggerOptions
- ExponentialHistogram
- Requirements
- HubInstance
- Decisions
- lens.py
- LogRecord
- ExplicitBucketHistogram
- StartOptions
- StreamManager
- .meter
- SettingsPaths
- ErrorCode
- ApplicationStateReporter
- .metrics
- TracerProviderConfig
- settings_io.hpp
- SettingsSource
- HubSuite
- main
- SpanAttribute
- suite_supervisor.cpp
- SpanFrame
- EventNotifier
- MetricKind
- app_suite_test.cpp
- MetricProvider
- json_field.hpp
- FileLogger
- SpanStatus
- ProviderConfig
- atomic
- Settings
- core_test.cpp
- EventListener
- TracerProvider
- string_view
- .chrono
- StopwatchState
- ExecutionManager
- Subscriber
- SmMetrics
- SpanId
- id_generator.hpp
- FaultStorage
- Requirements
- JsonFileSinkConfig
- enum_name
- ex_composite_logger.cpp
- StateStorage
- TracerProviderConfig
- SettingsDiagnostic
- Requirement: `apply_thread_spec_self` delegates to single-attribute applicators
- CompositeLogger
- RecoveryClient
- SuiteState
- Iceoryx2MetricSink
- execution_manager_test.cpp
- ChromeTraceSink
- Requirements
- tracer_concept_test.cpp
- self_affinity
- SupervisedProcess
- Requirements
- ThreadApplyReport
- Field
- Requirements
- FileLoggerConfig
- Iceoryx2Logger
- Discovery
- check_capacity
- main
- Provider
- Settings
- FileSink
- Requirements
- Requirements
- SpamProtectionConfig
- Counter
- MetricProviderConfig
- Settings
- JsonFileTraceSink
- from_json
- Requirements
- StampSource
- Appender
- ADDED Requirements
- ADDED Requirements
- Requirements
- Requirements
- Requirements
- Requirement: CPU-pinned background consumer
- Meter
- ADDED Requirements
- Server
- Requirement: JSON-lines file sink
- AsyncWorker
- instance_lifecycle.cpp
- ScopedSpan
- ADDED Requirements
- ADDED Requirements
- ADDED Requirements
- HistoryWriter
- .restart
- Producer
- FunctionGroup
- StateManager
- ADDED Requirements
- ADDED Requirements
- snapshot_format.cpp
- tracing/sinks_test.cpp
- Requirements
- Requirements
- make_infrastructure
- .nanoseconds
- EventId
- WorkerTuning
- ConsoleLoggerConfig
- HealthState
- SamplingDecision
- ADDED Requirements
- Requirements
- http_server_test.cpp
- SpamProtection
- Iceoryx2LoggerConfig
- LogFrame
- resolve_stop_state
- ADDED Requirements
- ADDED Requirements
- Requirements
- Requirements
- supervise_child
- TransitionResult
- ADDED Requirements
- Requirements
- tracing_settings.cpp
- sample_utc_offset
- ChronoWorker
- LapSnapshot
- StreamRecord
- drain_frames
- OverflowPolicy
- ConsoleTraceSink
- ADDED Requirements
- ADDED Requirements
- ADDED Requirements
- ADDED Requirements
- Requirements
- Requirements
- Requirements
- map_error
- ConsoleMetricSink
- ADDED Requirements
- ADDED Requirements
- ADDED Requirements
- Requirements
- Requirements
- Requirements
- diagnostics_test.cpp
- main
- SpscRingBuffer
- Requirements
- ConsoleSink
- SampleMut
- WaitSet
- Gauge
- MachineState
- ADDED Requirements
- 3. Per-file plan
- nexigen-exec/main.cpp
- Requirement: Environment overrides are documented and typed
- ClockId
- proc_start_ticks
- rotation.cpp
- Requirements
- Decisions
- Hub_options
- Requirements
- command_line.cpp
- ShardPool
- TraceDaemonSession
- Iceoryx2Sink
- metrics/latency_test.cpp
- EventListener
- Tracer
- ADDED Requirements
- ADDED Requirements
- Requirements
- logger_options.cpp
- Requirements
- Requirements
- RecordingSink
- ThreadStateSnapshot
- JsonFileSink
- PendingResponse
- ServiceManifest
- Subscriber
- .create_without_signal_handling
- SupervisionStatus
- ADDED Requirements
- MODIFIED Requirements
- ADDED Requirements
- ADDED Requirements
- ADDED Requirements
- Requirement: Affinity failures are non-fatal warnings
- Requirements
- Requirements
- Requirements
- test_scoped_span
- RecordingSink
- nexigen-hub
- StreamKind
- ADDED Requirements
- .inc
- chrono/settings.cpp
- Client
- Publisher
- InfrastructureStopReason
- StateGauge
- StartCell
- Requirements
- ADDED Requirements
- Requirement: Background snapshot delivery and collection period
- ADDED Requirements
- ADDED Requirements
- Requirements
- Requirement: Runnable composite logger example
- LogDaemonConfig
- ADDED Requirements
- command_line_test.cpp
- file_logger_test.cpp
- RecordingSink
- TransitionAudit
- RunIdentity
- ADDED Requirements
- ADDED Requirements
- ADDED Requirements
- Meter
- ADDED Requirements
- ADDED Requirements
- Requirements
- Requirements
- Requirements
- nexigen-cpp — project analysis
- AppState
- ex_app_cli_settings.cpp
- CallbackProgression
- Error
- FaultPoint
- FixedWriter
- Requirements
- CpuSet
- ChildProcess
- RecoveryAction
- Supervisor
- ADDED Requirements
- ADDED Requirements
- Tasks
- Requirement: Malformed requests are rejected consistently
- Requirements
- Requirements
- Requirements
- Requirements
- arena_used
- StatisticsSnapshot
- MetricHttpServer
- ProviderKind
- Requirements
- TraceDumpHeader
- .is_full
- FunctionGroupRegistry
- StateClient
- cmd_status
- NullMetricSink
- state_manager.cpp
- StatePublication
- TransitionPreparation
- RotationPolicy
- opsx-onboard.md
- openspec-onboard/SKILL.md
- MODIFIED Requirements
- Requirements
- Requirements
- logging/latency_test.cpp
- ADDED Requirements
- Session
- ADDED Requirements
- TelemetryPacket
- RecoveryNotification
- enum_from_json
- unique_path
- ConsoleSink
- ShardPoolState
- DescriptiveStatistics
- JsonFileMetricSink
- .on_batch
- Decisions
- ADDED Requirements
- ADDED Requirements
- ADDED Requirements
- ADDED Requirements
- Design
- Requirement: Suite manifest is validated before launch
- Requirements
- Requirement: Readable metric concept
- Requirements
- file_logger_config.cpp
- LogLevel
- JsonFileMetricSinkConfig
- execution_manager_logging_test.cpp
- TraceContext
- .now
- ADDED Requirements
- ADDED Requirements
- Requirement: Logger concept core surface
- ADDED Requirements
- 5. Oversized tests — split into focused sub-functions
- Decisions
- Decisions
- ADDED Requirements
- ADDED Requirements
- Requirements
- Requirements
- Requirements
- function_group_registry.cpp
- RotationSchedule
- supervisor.cpp
- iceoryx2_metric_sink_test.cpp
- MODIFIED Requirements
- ADDED Requirements
- ADDED Requirements
- ADDED Requirements
- ADDED Requirements
- Requirements
- Decisions
- Decisions
- Requirements
- Decisions
- Header compilation audit — compiled-module JSON pairs
- ADDED Requirements
- Requirement: App settings aggregate every subsystem
- Requirements
- Requirements
- SpacingSink
- .emit
- ADDED Requirements
- ADDED Requirements
- Tasks
- Requirement: Example runner and cleanup scripts
- ADDED Requirements
- Requirement: Environment overrides are documented and typed
- Requirement: Long and short option forms are supported
- Requirement: Identity validation rejects unsafe values
- SpanContext
- metric_provider_test.cpp
- Requirements
- Requirements
- Requirements
- Stopwatch
- logging_test_support.hpp
- .emplace_logger
- 2. Production deserializers (CCN ≤ 15, cold-path in `src/`)
- CommandLineOptions
- TraceDaemonConfig
- Decisions
- WaitSetGuard
- Decisions
- CompositeMetricSink
- ThrottleConfig
- ADDED Requirements
- ADDED Requirements
- Requirement: Runnable command-line settings example
- ADDED Requirements
- ADDED Requirements
- Requirement: Metrics render structured by kind
- Requirement: Runnable minimal infrastructure example
- Requirement: Drop summaries are synthesized by the worker
- nexigen-phmd/main.cpp
- Design
- Requirement: CPU-pinned background consumer
- Requirement: Error code catalogue
- Requirement: Fixed-width ISO-8601 nanosecond formatting
- Requirements
- Iceoryx2TraceSink
- Fixture
- StopArgs
- name
- CivilTime
- state_client.cpp
- TransitionRecord
- Decisions
- Requirement: Asynchronous wait-free console output
- ThreadApplyMode
- ADDED Requirements
- Requirement: Base clock-domain identifiers
- Requirement: Size-based file rotation
- Requirement: App settings are serializable
- Requirement: Factory hierarchy and passkey construction
- ADDED Requirements
- Requirement: Child environment and CPU affinity are configurable
- Requirement: CPU core affinity
- Requirement: CPU-pinned background consumer
- Requirement: Base clock-domain identifiers
- Requirements
- Requirement: LoggerOptions defines the engine configuration
- Requirement: Dynamic quota updates are race-free
- Requirement: Nested and cross-thread spans example
- Sink
- run_suite
- metrics/sinks_test.cpp
- make_record
- ShardState
- Tasks
- ADDED Requirements
- ADDED Requirements
- Decisions
- Requirement: Runnable command-line settings example
- Decisions
- Decisions
- ADDED Requirements
- Requirement: CPU core affinity
- Requirement: Background writer CPU core affinity
- Requirements
- ADDED Requirements
- ADDED Requirements
- Requirements
- Requirements
- Requirement: Pinning the calling thread by core mask
- Decisions
- Requirements
- What Changes
- ADDED Requirements
- HttpServer
- FakeProvider
- http_parse.hpp
- Tasks
- .pop
- openspec-explore/SKILL.md
- ADDED Requirements
- Requirement: Conforming no-op logger
- Requirement: Fixed-width ISO-8601 nanosecond formatting
- Design
- ADDED Requirements
- Requirement: Coverage includes forked supervisors
- Requirement: Generated settings contain absolute sink paths
- Decisions
- Requirement: Suite-metadata read targets are run-root contained
- Requirement: Global logging settings supply operational fields
- JsonFileTraceSinkConfig
- Requirements
- ADDED Requirements
- ADDED Requirements
- .loan_uninit
- Requirements
- Requirement: Runtime level control
- Requirement: Conforming no-op logger
- .get_meter
- Requirement: Coverage includes forked supervisors
- Requirement: State machines execution
- Color
- nexigen-run application suites
- Sample
- Design
- ADDED Requirements
- TraceHeader
- example_of_system_manifest.yaml
- prometheus_http
- phm_health_alerts
- scraping
- opsx-explore.md
- LatencyStats
- ADDED Requirements
- Requirement: Runnable command-line settings example
- Design
- Design
- Requirement: Portable Release mode
- ADDED Requirements
- Requirement: Documented example invocation
- Requirement: Settings diagnostics are serializable
- Requirement: Console writer accepts a thread specification
- Requirement: Child environment and CPU affinity are configurable
- Requirement: Affinity failures are non-fatal warnings
- Design
- Requirements
- ADDED Requirements
- ADDED Requirements
- Requirements
- Requirement: Portable Release mode
- Requirements
- sink_pack_negative.cpp
- What Changes
- message_non_pod_negative.cpp
- REMOVED Requirements
- ci.yml
- Fanout
- MetricDaemonConfig
- TriggerRequest
- DroppedStats
- environment_variables
- Requirement: Error code catalogue
- Requirement: Conforming no-op logger
- Tasks
- Requirement: Sharded drop accounting without contention
- ADDED Requirements
- ADDED Requirements
- Proposal
- MODIFIED Requirements
- Requirement: Error code catalogue
- Requirement: Runnable minimal infrastructure example
- ADDED Requirements
- Tasks
- Design
- Requirement: Live updates use Server-Sent Events
- StatsBank
- Requirement: JSON-lines file sink
- Design
- Requirement: HTTP listener is loopback by default
- ADDED Requirements
- Requirement: Merged settings apply to provider configuration
- ADDED Requirements
- ADDED Requirements
- ADDED Requirements
- ADDED Requirements
- ADDED Requirements
- Requirements
- Decisions
- Requirements
- Requirements
- Requirements
- ProbeSink
- SpanRing
- PodPayload
- Decisions
- Iceoryx2TraceSinkConfig
- Iceoryx2MetricSinkConfig
- StateAck
- FaultKind
- prometheus_render_test.cpp
- DroppedStats
- JsonFileTraceSinkConfig
- span_frame_format.cpp
- ADDED Requirements
- provided_services
- phm_supervision
- processes
- storage
- crash_dump
- descriptive_statistics
- .emplace_logger
- Высокоуровневые требования: Архитектура управления состояниями платформы Nexigen
- Requirements
- Requirement: Runnable composite logger example
- Requirement: Runnable file logger example
- ADDED Requirements
- MODIFIED Requirements
- ADDED Requirements
- MODIFIED Requirements
- MODIFIED Requirements
- MODIFIED Requirements
- MODIFIED Requirements
- MODIFIED Requirements
- Tasks
- ADDED Requirements
- Requirement: Dynamic quota updates are race-free
- Requirement: Child environment and CPU affinity are configurable
- Requirement: Trace spans carry the application name
- Requirement: Initialization follows a fixed order
- Requirement: Canonical and legacy worker settings merge deterministically
- Requirement: LoggerOptions defines the engine configuration
- CompositeTraceSink
- check_result_value.py
- Tasks
- Decisions
- ADDED Requirements
- MetricFrame
- make_test_infra
- exec/application-state Specification
- Requirement: JSON-lines file sink
- MODIFIED Requirements
- NullSink
- Tasks
- Decisions
- span_context_test.cpp
- include
- TriggerResponse
- example_of_log_daemon_config.yaml
- network
- Спецификация требований к демону логирования (LogDaemon)
- example_of_metrics_daemon_config.yaml
- os_telemetry
- Спецификация требований к демону сбора метрик и мониторинга ресурсов (MetricsDaemon)
- Спецификация требований к демону трассировки (TraceDaemon)
- run_suite
- Requirement: Runnable console logger example
- HubSuite
- Proposal
- Proposal
- Requirement: SpscShardRing overflow policies
- Proposal
- Proposal
- Proposal
- Proposal
- load_logging_settings
- Settings
- Proposal
- Proposal
- Requirement: Settings diagnostic records a finding
- Proposal
- Proposal
- Tasks
- Proposal
- Proposal
- Requirement: Fixed file names produce the four settings pairs
- Requirement: State write failures are reported
- Proposal
- Tasks
- Proposal
- Requirements
- Requirement: Settings sources merge per application
- Requirement: Suite manifest is validated before launch
- Proposal
- Tasks
- Proposal
- Proposal
- Requirement: Optional worker logger
- Requirement: Thread-local slot registration
- Requirement: Suite manifest has a fixed schema
- Proposal
- ADDED Requirements
- int32_t
- Proposal
- Tasks
- SuiteManifest
- ExponentialSlot
- Proposal
- MeterPasskey
- LinesSink
- nexigen-cpp — анализ кода
- Proposal
- Tasks
- Proposal
- ADDED Requirements
- Proposal
- Proposal
- ADDED Requirements
- Requirement: JsonFileTraceSink JSONL output
- Tasks
- Proposal
- Proposal
- rules
- Requirement: Settings diagnostic records a finding
- Requirement: Affinity failures are non-fatal warnings
- check_naming.py
- Requirement: HTTP listener is loopback by default
- Requirement: Malformed requests are rejected consistently
- Requirements
- StressResult
- coverage_report.py
- SpanRecord
- app_suite.cpp
- Deferred: отложенные работы платформы (не забыть доделать)
- Settings
- RotationConfig
- custom_metric_rules
- disk_flight_recorder
- .shutdown
- Proposal
- Requirement: Suite manifest is validated before launch
- app_settings.cpp
- Requirement: Runnable simple settings example
- Tasks
- Requirement: CTest smoke coverage for every example
- Tasks
- Requirement: Manifest launch settings reach each child
- Requirement: Suite status reports aggregate and application state
- Proposal
- Requirement: Suite runner log path follows the runner layout
- Proposal
- Proposal
- Requirement: Embedded script parses and bootstraps
- Proposal
- metric_frame_test.cpp
- Requirement: Stateless infrastructure policy
- Requirement: Application name is app-level
- Decisions
- Tasks
- Requirement: LoggerOptions defines the engine configuration
- WelfordState
- ExponentialBank
- cpp-project-analizer/analyzer.py
- Requirement: Runtime level control
- TerminatedBy
- Design
- .bump
- Tasks
- spam_protection_config.cpp
- Requirement: Manifest launch settings reach each child
- Requirement: Suite status reports aggregate and application state
- Requirements
- Requirement: Live updates use Server-Sent Events
- Design
- Decisions
- validate.sh
- producer_test.cpp
- Requirement: Metric batches carry the application name
- Requirement: Provider kind setting
- to_string
- BoundedSink
- GaugeStorage<ValueType, ConcurrencyPolicy::SingleThreaded, Reset>
- resources
- resources
- Tasks
- ThreadSpec
- ExponentialMerge
- Requirement: Civil time diagnostic string
- Requirement: Timestamp diagnostic string
- Why
- Requirement: Tracing settings map every provider field
- Requirement: Monotonic nanosecond stamping
- SuiteMetadata
- Proposal
- Decisions
- .prepare
- Tasks
- Tasks
- .collect_now
- Entry
- TelemetryPaths
- StateStorage<ConcurrencyPolicy::MultiThreaded, Reset>
- Tasks
- Requirement: Effective log level applies to each application
- Requirement: Failure impact restarts the whole suite
- Requirement: Restarts follow abnormal termination only
- Requirement: Manifest configures runner thread affinity
- Requirement: Runner level follows manifest, then global settings
- Proposal
- Requirement: Meter publication is serialized with harvesting
- View
- Proposal
- Proposal
- Proposal
- Proposal
- Entry
- Requirement: SafeState priority
- Design
- composite_logger_test.cpp
- Requirement: Restarts follow abnormal termination only
- Requirement: ExecutionManager level follows manifest, then global settings
- Requirement: Global logging settings supply operational fields
- Requirement: Manifest configures execution_manager thread affinity
- Requirement: Read-only JSON endpoints
- Requirement: Request headers are matched case-insensitively
- Tasks
- ADDED Requirements
- Requirement: Manifest and config deserialization uses a shared field vocabulary
- execution_manager_logging.cpp
- Tasks
- Tasks
- OutRefSink
- Proposal
- to_string
- .format
- security
- context_overrides
- Decisions
- Infrastructure
- .user_header
- Proposal
- StateStorage<ConcurrencyPolicy::SingleThreaded, Reset>
- Design
- Requirement: Degrade and not-impact levels bound the damage
- Requirement: Affinity failures are non-fatal warnings
- Requirement: CLI commands record their outcome
- Requirement: Global logging settings supply operational fields
- Requirement: Log location follows the manifest and suite metadata
- Requirement: Suite supervisor records lifecycle and policy events
- Decisions
- Tasks
- Tasks
- Tasks
- Proposal
- MODIFIED Requirements
- SupervisedEntity
- MODIFIED Requirements
- Design
- Design
- Approach
- Requirement: CLI commands record their outcome
- Requirement: Log location follows the manifest and suite metadata
- Requirement: State write failures are reported
- Requirement: Suite supervisor records lifecycle and policy events
- Modified Capabilities
- Requirement: HTTP parsing and errors are bounded
- OtherPayload
- FanoutConfiguredSink
- Tasks
- operator new
- operator new
- operator new
- run_suite_demo.sh
- Design
- Design
- Requirement: Failure impact restarts the whole suite
- Decisions
- Proposal
- lifecycle
- ipc_iceoryx2
- rotation
- StopArgs
- Design
- Design
- Design
- Requirement: Each attempt writes to a new artifact directory
- Requirement: Generated settings point at the attempt artifacts
- Requirement: Suite launch is detached
- Requirement: Suite manifest has a fixed schema
- Requirement: Suite metadata lives in a deterministic suite directory
- Requirement: Suite types follow the serialization contract
- Requirement: Logger lifecycle is fork-safe
- Requirement: Logging is best-effort
- Requirement: Runner diagnostics use the project file logger
- logging/spsc-shard-ring delta spec
- Tasks
- ConsoleLogger
- exponential_index
- Boxed
- Requirement: Long and short option forms are supported
- Requirement: LoggerOptions defines the engine configuration
- run_daemon
- Design
- Requirement: Degrade and not-impact levels bound the damage
- Requirement: ExecutionManager diagnostics use the project file logger
- Requirement: Logger lifecycle is fork-safe
- Requirement: Logging is best-effort
- Requirement: Suite metadata preserves execution_manager affinity
- Requirement: Control-plane refresh failures are reported
- Requirement: Web assets are served by the binary
- Spec Delta
- Tasks
- PodPayload
- Why
- run_ex_app_cli_settings.sh
- Tasks
- Entry
- Навык анализа C++ проектов
- Requirement: At least one settings directory is required
- Requirement: Command-line types are serializable
- Requirement: Global and local directory shapes are validated
- Requirement: Client read logic is layered
- Requirement: `HttpServer::Impl::start` is split into setup phases
- Requirement: The API is observation-only
- cli_test.sh
- hub_cli_test.sh
- smoke_test.sh
- AGENTS.md
- clean_ex_app_cli_settings.sh
- clean_suite_demo.sh
- run_suite_demo_deployed.sh
- 2026-11-08-refactor-app-file-per-entity/tasks.md
- 2026-11-08-refactor-diagnostics-file-per-entity/tasks.md
- 2026-11-08-refactor-hub-file-per-entity/tasks.md
- 2026-11-08-refactor-metrics-file-per-entity/tasks.md
- 2026-11-08-refactor-runner-file-per-entity/tasks.md
- 2026-11-08-refactor-tracing-file-per-entity/tasks.md
- ci-cyclomatic-complexity-gate/tasks.md
- negative_compile.sh
- Requirement: Unknown, missing, repeated, and positional arguments are handled
- string_view
- Proposal
- SuiteLaunchPlan
- Proposal
- Design
- Requirement: Fixed file names produce the four settings pairs
- Requirement: Help short-circuits with success
- Requirement: Log level accepts canonical enumerator names
- proposal: refactor-high-complexity-functions
- jobs
- NonPodPayload
- Requirement: CPU-pinned background consumer
- Design
- Requirement: Settings sources merge per application
- Requirement: Child environment and CPU affinity are configurable
- TracerSlot
- Proposal
- Tasks
- Requirement: Option parsing honors the terminator
- apply_thread_spec_self
- LLVMFuzzerTestOneInput
- Tasks
- hostile_config_test.sh
- subprocess
- Proposal
- Requirement: Size-based file rotation
- Requirement: JSON-lines file sink
- Requirement: Log level accepts canonical enumerator names
- ArtifactCounts
- FormatWithLocation
- Entry
- JsonFileTraceSink::write_record_line
- StatusArgs
- Capabilities
- clang
- Tasks
- Tasks
- Tasks
- nexigen-metricsd/smoke_test.sh
- Proposal
- SessionAnalyzer
- StateShard
- TracerProvider<ShardCapacity, MaxThreads>::get_tracer
- Requirement: Stdout and stderr are captured to files
- Design
- Requirement: Nanosecond UTC timestamps via calibrated clocks
- Requirement: Long and short option forms are supported
- Proposal
- openspec
- Proposal
- Proposal
- Tasks
- Proposal
- Proposal
- Tasks
- Proposal
- Proposal
- Tasks
- on
- Requirement: Effective log level applies to each application
- RequestLineError
- nexigen-traced/smoke_test.sh
- Requirement: Composite metric sink fan-out
- heap_in_use
- ex_stopwatch.cpp
- .take_slot
- json_file_metric_sink.cpp
- Requirement: Suite metadata lives in a deterministic suite directory
- Requirement: Suite types follow the serialization contract
- Requirement: At least one settings directory is required
- RequestLine
- Requirement: Command-line types are serializable
- Requirement: Global and local directory shapes are validated
- Requirement: Unknown, missing, repeated, and positional arguments are handled
- log_record_test.cpp
- Requirement: App name option
- Requirement: Fixed file names produce the four settings pairs
- Requirement: Help short-circuits with success
- Requirement: Each attempt writes to a new artifact directory
- nexigen-logd/smoke_test.sh
- Requirement: Suite stop is final and idempotent
- Requirement: Logger concept core surface
- parse_u64
- trace_daemon_test.cpp
- Requirement: File sink configuration serialization
- Requirement: Settings sources are explicit
- null_logger_test.cpp
- run_suite
- app/command-line Specification
- Requirement: CLI log level overrides loaded settings
- Requirement: Option parsing honors the terminator
- descriptive_statistics_test.cpp
- state_gauge_test.cpp

## God Nodes (most connected - your core abstractions)
1. `LogLevel` - 176 edges
2. `LoggerOptions` - 118 edges
3. `MetricRecord` - 112 edges
4. `SuiteManifest` - 97 edges
5. `SettingsDiagnostic` - 97 edges
6. `SettingsSource` - 96 edges
7. `SpanRecord` - 89 edges
8. `SpanFrame` - 81 edges
9. `ErrorCode` - 80 edges
10. `Node` - 73 edges

## Surprising Connections (you probably didn't know these)
- `Scenario: Readable counter read and snapshot` --references--> `MetricRecord`  [INFERRED]
  openspec/changes/archive/2026-10-28-add-metrics-subsystem/specs/metrics/counter-gauge/spec.md → include/nexigen/metrics/metric_record.hpp
- `Scenario: Snapshot returns the full record` --references--> `MetricRecord`  [INFERRED]
  openspec/changes/archive/2026-10-28-add-metrics-subsystem/specs/metrics/descriptive-statistics/spec.md → include/nexigen/metrics/metric_record.hpp
- `Scenario: Snapshot returns the full record` --references--> `MetricRecord`  [INFERRED]
  openspec/changes/archive/2026-10-28-add-metrics-subsystem/specs/metrics/explicit-histogram/spec.md → include/nexigen/metrics/metric_record.hpp
- `Scenario: Snapshot returns the full record` --references--> `MetricRecord`  [INFERRED]
  openspec/changes/archive/2026-10-28-add-metrics-subsystem/specs/metrics/exponential-histogram/spec.md → include/nexigen/metrics/metric_record.hpp
- `Workflow follow-up` --references--> `MetricRecord`  [INFERRED]
  openspec/changes/archive/2026-11-02-improve-hub-metric-display/tasks.md → include/nexigen/metrics/metric_record.hpp

## Import Cycles
- None detected.

## Communities (1049 total, 113 thin omitted)

### Community 0 - "string"
Cohesion: 0.04
Nodes (23): main(), print_line(), EnumTraits<nexigen::app::InfrastructureStopReason>, entries, from_json(), to_json(), Result, Status (+15 more)

### Community 1 - "format"
Cohesion: 0.04
Nodes (44): Node, inner_, std::formatter<nexigen::com::Node>, to_string(), Client, Publisher, Subscriber, WaitSetAttachmentId (+36 more)

### Community 2 - "optional"
Cohesion: 0.18
Nodes (8): FailChronoPolicy, FailLoggerPolicy, FailMetricsPolicy, FailTracePolicy, FlushFailPolicy, ResolvedPathPolicy, RtPolicy, TestPolicy

### Community 3 - "SpanStatus"
Cohesion: 0.08
Nodes (23): Entry, name, value, EnumTraits<nexigen::tracing::SpanStatus>, entries, from_json(), from_string(), SpanStatus (+15 more)

### Community 4 - "daemon_components_test.cpp"
Cohesion: 0.20
Nodes (6): http_request(), make_counter(), test_aggregation_latest_wins(), test_daemon_components(), test_exposition_and_http(), test_history_append_and_rotation()

### Community 5 - "SuiteManifest"
Cohesion: 0.03
Nodes (82): AppSpec, app, args, command, cpus, dependencies, env, function_groups (+74 more)

### Community 6 - "format_param_t"
Cohesion: 0.07
Nodes (8): format_param_t, NullLogger, levels_, to_string(), CaptureLogger, FlushFailLogger, InfoNeedsLocationModel, LogReturnsStatusModel

### Community 7 - "ResetPolicy"
Cohesion: 0.02
Nodes (83): AccessPolicy, WriteOnly, Entry, name, value, EnumTraits<nexigen::metrics::AccessPolicy>, entries, from_json() (+75 more)

### Community 8 - "tracer_provider.hpp"
Cohesion: 0.18
Nodes (3): Tracer, to_json(), TracerProvider<1024, 32>

### Community 9 - "Snapshot"
Cohesion: 0.04
Nodes (38): CompositeSink, std::formatter<nexigen::chrono::CompositeSink<Sinks...>>, to_json(), to_string(), Snapshot, realtime_offset_ns, stamp, std::formatter<nexigen::chrono::Snapshot> (+30 more)

### Community 10 - "ResourceMetadata"
Cohesion: 0.07
Nodes (25): read_bool(), read_i32(), read_optional_i32(), read_string(), read_u32(), read_u64(), utc_now_rfc3339(), ResourceMetadata (+17 more)

### Community 11 - "MetricProviderConfig"
Cohesion: 0.06
Nodes (33): MetricProviderConfig, collect_interval, max_counters, max_explicit_histograms, max_exponential_histograms, max_gauges, max_meters, max_states (+25 more)

### Community 12 - "HttpServer::Impl"
Cohesion: 0.05
Nodes (36): BindAddress, in, in6, Client, chunked, close_after_write, fd, input (+28 more)

### Community 13 - "MetricIdentity"
Cohesion: 0.07
Nodes (31): MetricIdentity, meter_name, meter_version, metric_name, unit, std::formatter<nexigen::metrics::MetricIdentity>, Context, D1 — Unit lives in `MetricIdentity` as a fixed-size string (+23 more)

### Community 14 - "HistogramSnapshot"
Cohesion: 0.05
Nodes (52): expect(), ExplicitHist, ExponentialHist, main(), measure_durations(), percentiles_ordered(), report_explicit(), report_exponential() (+44 more)

### Community 15 - "MetricRecord"
Cohesion: 0.02
Nodes (102): CaptureSink, seen_, CaptureSink, seen_, MetricBatchSnapshot, record_capacity, record_count, records (+94 more)

### Community 16 - "NullTraceSink"
Cohesion: 0.04
Nodes (53): NullSpan, NullTracer, std::formatter<nexigen::tracing::NullTracer>, to_json(), to_string(), NullTraceSink, to_json(), to_string() (+45 more)

### Community 17 - "LoggerOptions"
Cohesion: 0.05
Nodes (46): LoggerOptions, initial_log_level, log_root_directory, max_file_size_bytes, max_producer_threads, message_capacity, overflow_policy, spam_protection (+38 more)

### Community 18 - "ExponentialHistogram"
Cohesion: 0.11
Nodes (13): ExponentialHistogram, access, cell_, concurrency, fallback_, identity_, ignored_, kind (+5 more)

### Community 19 - "Requirements"
Cohesion: 0.08
Nodes (26): Requirement: Environment overrides are documented and typed, Requirement: Invalid input degrades to best effort, Requirement: Invalid sink configuration degrades to best effort, Requirement: Merge precedence matches the other subsystems, Requirement: Settings loading is cold path only, Requirement: Settings sources are explicit, Requirement: Sink fields merge with documented precedence, Requirement: Sink object reads the file-sink keys (+18 more)

### Community 20 - "HubInstance"
Cohesion: 0.04
Nodes (46): HubApp, app, instances, state, to_string(), HubAttempt, artifact_dir, attempt (+38 more)

### Community 21 - "Decisions"
Cohesion: 0.15
Nodes (12): D2 — Service lives directly in `nexigen::chrono`, D3 — Snapshot record is a 16-byte value type, D5 — Concurrency and memory layout, D6 — Offset sampling skew, D7 — Sink contract mirrors metrics/tracing, D8 — Settings loader reuses the logging vocabulary, D9 — Serialization on every new type, Decisions (+4 more)

### Community 22 - "lens.py"
Cohesion: 0.18
Nodes (3): generate_svg_line_chart(), main(), ProjectAnalyzer

### Community 23 - "LogRecord"
Cohesion: 0.04
Nodes (42): LogRecord, arg_length, arg_payload_, file_name, flags, fmt, function_name, level (+34 more)

### Community 24 - "ExplicitBucketHistogram"
Cohesion: 0.04
Nodes (39): ExplicitBucketHistogram, access, bound_count_, bounds_, cell_, concurrency, fallback_, identity_ (+31 more)

### Community 25 - "StartOptions"
Cohesion: 0.04
Nodes (78): cpus_within_online(), online_cpu_count(), absolute_from_cwd(), absolute_path(), current_directory(), directory_of(), has_parent_segment(), instance_dir_path() (+70 more)

### Community 26 - "StreamManager"
Cohesion: 0.04
Nodes (46): DiagnosticSink, StreamLimits, max_line_bytes, read_chunk_bytes, ring_bytes, ring_records, subscriber_bytes, StreamManager (+38 more)

### Community 27 - ".meter"
Cohesion: 0.05
Nodes (53): D8 — Example and tests, Capabilities, Modified Capabilities, New Capabilities, Proposal, What Changes, Why, Requirement: App exposes logger, metric and tracer factories (+45 more)

### Community 28 - "SettingsPaths"
Cohesion: 0.06
Nodes (48): AppSettingsPaths, chrono, logging, metrics, tracing, to_string(), from_json(), SettingsPaths (+40 more)

### Community 29 - "ErrorCode"
Cohesion: 0.04
Nodes (47): ErrorCode, BufferEmpty, BufferFull, Conflict, InvalidArgument, NotInitialized, Ok, RateLimited (+39 more)

### Community 30 - "ApplicationStateReporter"
Cohesion: 0.16
Nodes (6): application_state_service(), ApplicationStateReporter, app_name_, notifier_, std::formatter<nexigen::exec::ApplicationStateReporter>, to_string()

### Community 31 - ".metrics"
Cohesion: 0.03
Nodes (68): std::formatter<nexigen::metrics::HistogramSnapshot>, 1. Введение и область применения, 2. Жизненный цикл и запуск (Lifecycle & Initialization), 3. Взаимодействие со средой выполнения и ОС (Runtime Environment), 4. Управление потоками и оркестрация ядер (Threading & CPU Affinity), 5. Корректное завершение работы (Termination & Signals), 6. Интеграция с Platform Health Management (PHM), 7. Требования к структурам данных связи (IPC Data Constraints) (+60 more)

### Community 32 - "TracerProviderConfig"
Cohesion: 0.07
Nodes (22): std::formatter<nexigen::tracing::TracerProviderConfig>, TracerProviderConfig, max_spans_per_window, max_tracers, window_ns, worker_poll_interval_us, worker_thread, Requirement: Settings sources are explicit (+14 more)

### Community 33 - "settings_io.hpp"
Cohesion: 0.10
Nodes (48): add_diagnostic(), apply_env_fields(), env_value(), EnvField, env_name, field, find_sink(), format_settings_summary() (+40 more)

### Community 34 - "SettingsSource"
Cohesion: 0.14
Nodes (41): from_json(), SettingsSource, Computed, Environment, Global, Local, to_json(), Context (+33 more)

### Community 35 - "HubSuite"
Cohesion: 0.07
Nodes (39): Discovery, tree_signature_, RunRoot, key, path, to_string(), D3 — Hub containment enforced inside Discovery at tree-build time, 3. Hub: path containment in discovery (P0-3) (+31 more)

### Community 36 - "main"
Cohesion: 0.11
Nodes (22): suite_explicit_histogram(), suite_explicit_histogram_mt(), suite_exponential_histogram(), suite_exponential_histogram_mt(), suite_gauge(), suite_latency(), main(), suite_registration_race() (+14 more)

### Community 37 - "SpanAttribute"
Cohesion: 0.05
Nodes (34): Entry, name, value, EnumTraits<nexigen::tracing::SpanAttributeType>, entries, from_json(), from_string(), SpanAttribute (+26 more)

### Community 38 - "suite_supervisor.cpp"
Cohesion: 0.10
Nodes (41): log_execution_manager(), advance_suite_apps(), advance_suite_restart(), apply_launch_failures(), apply_suite_control_thread(), AppStateMachine, build_suite_status(), dependencies_ready() (+33 more)

### Community 39 - "SpanFrame"
Cohesion: 0.04
Nodes (49): SpanFrame, app_id, attribute_count, attributes, column, end_ns, file_hash, flags (+41 more)

### Community 40 - "EventNotifier"
Cohesion: 0.06
Nodes (15): EventNotifier, inner_, service_name_, ServiceName, inner_, std::formatter<nexigen::com::ServiceName>, to_string(), CheckpointEmitter (+7 more)

### Community 41 - "MetricKind"
Cohesion: 0.03
Nodes (52): ScalarShard, value, ShardedScalar, shards_, slot_kind_index(), thread_slot(), Entry, name (+44 more)

### Community 42 - "app_suite_test.cpp"
Cohesion: 0.11
Nodes (48): Entry, name, value, EnumTraits<nexigen::exec::ImpactOnAppSuiteOperability>, entries, fallback, from_string(), ImpactOnAppSuiteOperability (+40 more)

### Community 43 - "MetricProvider"
Cohesion: 0.04
Nodes (40): KindPool, arena, arena_size, meta, slot_max, used, MetricProvider, alive_ (+32 more)

### Community 44 - "json_field.hpp"
Cohesion: 0.06
Nodes (24): collect_array(), collect_array_into(), collect_object_array(), read_bool(), read_fixed_bytes(), read_fixed_string(), read_object(), read_signed() (+16 more)

### Community 45 - "FileLogger"
Cohesion: 0.04
Nodes (17): apply_to(), FileLogger, producer_, worker_, to_string(), Requirement: Flush synchronizes the file, Scenario: Flush surfaces write failure, Scenario: Flushed data is durable (+9 more)

### Community 46 - "SpanStatus"
Cohesion: 0.08
Nodes (25): ADDED Requirements, Purpose, Requirement: Constexpr span enum names, Requirement: Non-throwing span enum JSON serialization, Requirement: Span enum catalogue, Requirement: Span enum formatting, Requirement: Tracing-wide type diagnostic strings, Requirement: Tracing-wide type JSON and format surfaces (+17 more)

### Community 47 - "ProviderConfig"
Cohesion: 0.06
Nodes (36): ProviderConfig, collect_interval, kind, worker_thread, std::formatter<nexigen::chrono::ProviderConfig>, Capabilities, Modified Capabilities, New Capabilities (+28 more)

### Community 48 - "atomic"
Cohesion: 0.04
Nodes (56): build_bounds(), expect(), main(), print_histogram(), work_units(), CounterMT, CounterST, CounterWindow (+48 more)

### Community 49 - "Settings"
Cohesion: 0.09
Nodes (22): Settings, diagnostics, global_loaded, local_loaded, options, sink, to_string(), MODIFIED Requirements (+14 more)

### Community 50 - "core_test.cpp"
Cohesion: 0.09
Nodes (20): arena_exhausted(), FanoutABC, FanoutProbe, FanoutStubAlpha, visit, FanoutStubBeta, visit, FanoutStubGamma (+12 more)

### Community 51 - "EventListener"
Cohesion: 0.11
Nodes (6): EventListener, inner_, service_name_, std::formatter<nexigen::com::EventListener>, std::formatter<nexigen::com::EventNotifier>, to_string()

### Community 52 - "TracerProvider"
Cohesion: 0.06
Nodes (18): TracerProvider, alive_, binding_, config_, control_, control_enabled_bit, control_sampling_shift, policy_ (+10 more)

### Community 53 - "string_view"
Cohesion: 0.10
Nodes (27): compute_deadline(), DemoConfig, abort_after_s, exit_after_s, role, run_forever, worker_seconds, DemoRole (+19 more)

### Community 54 - ".chrono"
Cohesion: 0.05
Nodes (44): Build, test and verification matrix, Hot path vs cold path, Install and package, Module map, nexigen Architecture, Serialization contract (all modules), Spec-driven process, The hard mandates (+36 more)

### Community 55 - "StopwatchState"
Cohesion: 0.05
Nodes (42): Entry, name, value, EnumTraits<nexigen::diagnostics::StopwatchState>, entries, from_json(), from_string(), StopwatchState (+34 more)

### Community 56 - "ExecutionManager"
Cohesion: 0.07
Nodes (24): ExecutionManager, Context, Design, Goals / Non-Goals, Migration Plan, Open Questions, Risks / Trade-offs, Context (+16 more)

### Community 57 - "Subscriber"
Cohesion: 0.12
Nodes (10): std::formatter<nexigen::com::Subscriber<T, H>>, Subscriber, inner_, service_name_, to_string(), 1. Введение и область применения, 2. Инструментация кода и профилирование (RAII Spans & Instant Events), 3. Сквозная трассировка конвейеров данных через iceoryx2 (End-to-End Causality) (+2 more)

### Community 58 - "SmMetrics"
Cohesion: 0.12
Nodes (15): SmMetrics, arbitration_latency_us, emergency_transitions_total, last_transition_duration_us, rejected_requests_total, transition_timeouts_total, std::formatter<nexigen::sm::SmMetrics>, to_string() (+7 more)

### Community 59 - "SpanId"
Cohesion: 0.18
Nodes (8): active_span_stack(), ActiveFrame, span_id, trace_id, ActiveSpanStack, frames_, D7 — Parent tracking: fixed-depth thread-local stack with index truncation, test_parent_stack()

### Community 60 - "id_generator.hpp"
Cohesion: 0.25
Nodes (7): cached_thread_id(), IdGenerator, state_, next_id(), seed_input(), splitmix64(), D6 — Identifier generation: non-zero thread-local xorshift

### Community 61 - "FaultStorage"
Cohesion: 0.08
Nodes (15): FaultRecord, checkpoint_id, kind, process_index, timestamp_ns, FaultStorage, file_, path_ (+7 more)

### Community 62 - "Requirements"
Cohesion: 0.04
Nodes (47): logging/file-logger Specification, Purpose, Requirement: Asynchronous wait-free JSON-lines file output, Requirement: Automatic dropped-record summary, Requirement: Background writer CPU core affinity, Requirement: Background writer uses the shared idle-backoff policy, Requirement: File logger vocabulary diagnostic strings, Requirement: Hitch-free scheduled directory rotation (+39 more)

### Community 63 - "JsonFileSinkConfig"
Cohesion: 0.10
Nodes (17): JsonFileSinkConfig, append, path, std::formatter<nexigen::chrono::JsonFileSinkConfig>, to_string(), 1. Move the metrics serializers, 2. Move the chrono serializers, 3. Verification (+9 more)

### Community 64 - "enum_name"
Cohesion: 0.06
Nodes (35): enum_name(), enum_value(), from_string(), to_string(), MandateFormatter, to_string(), Entry, name (+27 more)

### Community 65 - "ex_composite_logger.cpp"
Cohesion: 0.17
Nodes (14): find_log_file(), main(), print_line(), verify_log_file(), find_log_file(), main(), print_line(), verify_log_file() (+6 more)

### Community 66 - "StateStorage"
Cohesion: 0.15
Nodes (11): StateStorage, Context, Capabilities, Impact, Proposal, What Changes, Why, 1. Fix the copy helper signatures (+3 more)

### Community 67 - "TracerProviderConfig"
Cohesion: 0.08
Nodes (30): SpanId, TraceId, tracing/record-format, Context, DroppedStats::to_string(), format_hex16(), from_json(), from_json_attributes() (+22 more)

### Community 68 - "SettingsDiagnostic"
Cohesion: 0.12
Nodes (39): from_json(), SettingsDiagnostic, field, message, source, to_json(), to_string(), Requirement: Settings diagnostics are serializable (+31 more)

### Community 69 - "Requirement: `apply_thread_spec_self` delegates to single-attribute applicators"
Cohesion: 0.20
Nodes (9): core/thread-management — internal structure (delta), Purpose, Requirement: Complexity of decomposed functions stays within the project target, Requirement: Refactored internals preserve all observable behavior, Requirement: Required-mode rollback remains centralized, Requirements, Scenario: Existing thread-management tests pass unchanged, Scenario: Post-refactor lizard report (+1 more)

### Community 70 - "CompositeLogger"
Cohesion: 0.12
Nodes (6): CompositeLogger, level_, members_, size, to_string(), D5 — Composite parity: `CompositeLogger::flush` returns the first error

### Community 71 - "RecoveryClient"
Cohesion: 0.07
Nodes (19): RecoveryClient, client_, em_channel_, sm_event_, sm_notifier_, RecoveryRequest, action, function_group (+11 more)

### Community 72 - "SuiteState"
Cohesion: 0.09
Nodes (21): SuiteExit, reason, state, uptime_s, to_string(), Entry, name, value (+13 more)

### Community 73 - "Iceoryx2MetricSink"
Cohesion: 0.11
Nodes (9): Iceoryx2MetricSink, config_, frame_, node_, notifier_, pid_, publisher_, std::formatter<nexigen::metrics::Iceoryx2MetricSink> (+1 more)

### Community 74 - "execution_manager_test.cpp"
Cohesion: 0.15
Nodes (33): contains(), failed_with(), first_allowed_cpu(), has_from_json, has_from_json<
    T, std::void_t<decltype(from_json(std::declval<const nlohmann::json&>(), std::declval<T&>()))>>, has_temporary_files(), identity_for(), main() (+25 more)

### Community 75 - "ChromeTraceSink"
Cohesion: 0.08
Nodes (14): ChromeTraceSink, base_ns_, base_set_, config_, fd_, first_event_, pid_, scratch_ (+6 more)

### Community 76 - "Requirements"
Cohesion: 0.04
Nodes (44): execution_manager/process-supervision Specification, Purpose, Requirement: A failed exec fails the start visibly, Requirement: A failed start reaps the supervisor child, Requirement: Child is launched under a new process group, Requirement: Complexity of decomposed functions stays within the project target, Requirement: Crash cleanup before restart, Requirement: Dependency-ordered startup (+36 more)

### Community 77 - "tracer_concept_test.cpp"
Cohesion: 0.07
Nodes (11): FullSinkModel, FullTracerModel, Guard, GuardWithoutEndModel, NoStartSpanModel, SinkWithoutFlushModel, SinkWithoutStopModel, suite_concepts() (+3 more)

### Community 78 - "self_affinity"
Cohesion: 0.05
Nodes (46): cpuset_to_mask(), pin_self(), self_affinity(), D2 — Header and source layout, 5. Thread slots, affinity, and Welford core, Context, D2 — Header and source layout, 4. Parent stack, affinity, and hot-path details (+38 more)

### Community 79 - "SupervisedProcess"
Cohesion: 0.06
Nodes (44): CheckpointTransition, from_id, to_id, DeadlinePair, max_ms, min_ms, source_id, target_id (+36 more)

### Community 80 - "Requirements"
Cohesion: 0.05
Nodes (43): hub/discovery Specification, Purpose, Requirement: Discovery is cold path only, Requirement: Discovery is read-only, Requirement: Discovery types follow the serialization contract, Requirement: Initial scan builds the run tree, Requirement: Inotify keeps the tree live, Requirement: Non-contained suite metadata degrades gracefully (+35 more)

### Community 81 - "ThreadApplyReport"
Cohesion: 0.09
Nodes (27): Application semantics, `CpuSet`, Diagnostics, JSON manifest form, Module integration, Quick start, See also, Thread management and CPU placement (+19 more)

### Community 82 - "Field"
Cohesion: 0.07
Nodes (19): Field, entry_, entry_id_, last_value_, notifier_, service_name_, update_count_, writer_ (+11 more)

### Community 83 - "Requirements"
Cohesion: 0.22
Nodes (9): Requirement: App name round-trips through serialization, Requirement: Command-line contract is documented, Requirement: Missing JSON fields use defaults, Requirement: Parsing is cold path only, Requirements, Scenario: App name survives a JSON round-trip, Scenario: Missing fields use defaults, Scenario: README shows the complete flow (+1 more)

### Community 84 - "FileLoggerConfig"
Cohesion: 0.06
Nodes (37): FileLoggerConfig, app_name, base_dir, file_name, initial_level, max_file_size_bytes, rotation, throttle (+29 more)

### Community 85 - "Iceoryx2Logger"
Cohesion: 0.06
Nodes (5): Iceoryx2Logger, node_, producer_, worker_, to_string()

### Community 86 - "Discovery"
Cohesion: 0.05
Nodes (34): 3. Сильные стороны, 10. P2 completion notes (2026-11-01), Context, D1: Flush gcov counters explicitly in forked children, D2: Per-module coverage gates in the report tool, D3: Function-pointer diagnostic sink, D4: Status-returning stream reconciliation, D5: Header parsing helper (+26 more)

### Community 87 - "check_capacity"
Cohesion: 0.07
Nodes (24): check_capacity(), D5 — Applying settings is an explicit, lifetime-documented step, Context, Decisions, Design, Goals / Non-Goals, Migration Plan, Risks / Trade-offs (+16 more)

### Community 88 - "main"
Cohesion: 0.06
Nodes (27): suite_affinity(), suite_drop_visibility(), suite_flush_summary_race(), suite_shutdown_drain_race(), suite_console(), suite_file_logger(), suite_json(), suite_logging_json_rejections() (+19 more)

### Community 89 - "Provider"
Cohesion: 0.06
Nodes (19): 1. Quick start, 2. Choosing the policies, 3. Thread capacity and windows, 4. Harvesting, 5. Sinks, 6. Sizing the registry, 7. Real-time checklist, 8. Troubleshooting (+11 more)

### Community 90 - "Settings"
Cohesion: 0.10
Nodes (23): Settings, diagnostics, global_loaded, local_loaded, options, sink, to_string(), Requirement: Settings diagnostics are serializable (+15 more)

### Community 91 - "FileSink"
Cohesion: 0.10
Nodes (15): FileSink, active_key_, bytes_written_, fd_, last_utc_ns_, line_, malformed_, msg_ (+7 more)

### Community 92 - "Requirements"
Cohesion: 0.05
Nodes (37): core/thread-management Specification, Purpose, Requirement: Best-effort application exposes partial success, Requirement: Complexity of decomposed functions stays within the project target, Requirement: CpuSet has canonical diagnostics and JSON, Requirement: CpuSet represents CPUs without the 64-bit mask limit, Requirement: Required application reports failure and rolls back, Requirement: Required-mode rollback remains centralized (+29 more)

### Community 93 - "Requirements"
Cohesion: 0.05
Nodes (39): logging/logging-settings Specification, Purpose, Requirement: Canonical and legacy worker settings merge deterministically, Requirement: Environment overrides are documented and typed, Requirement: Explicit compile-time capacity limits are checked, Requirement: Invalid input degrades to best effort, Requirement: Merge precedence is deterministic, Requirement: Message capacity explicit presence is tracked (+31 more)

### Community 94 - "SpamProtectionConfig"
Cohesion: 0.09
Nodes (24): SpamProtectionConfig, bypass_min_level, enable_repeat_suppression, max_messages_per_sec_per_shard, repeat_suppression_window_ms, to_string(), Requirement: SpamProtectionConfig defines anti-spam quotas, Scenario: Default-constructed spam protection carries documented defaults (+16 more)

### Community 95 - "Counter"
Cohesion: 0.07
Nodes (20): Counter, access, concurrency, identity_, kind, reset, storage_, CounterCell (+12 more)

### Community 96 - "MetricProviderConfig"
Cohesion: 0.07
Nodes (26): ADDED Requirements, Purpose, Requirement: Environment overrides are documented and typed, Requirement: Invalid input degrades to best effort, Requirement: Invalid sink configuration degrades to best effort, Requirement: Merge precedence matches the other subsystems, Requirement: Settings loading is cold path only, Requirement: Settings sources are explicit (+18 more)

### Community 97 - "Settings"
Cohesion: 0.09
Nodes (26): Settings, diagnostics, global_loaded, local_loaded, options, sink, to_string(), REMOVED Requirements (+18 more)

### Community 98 - "JsonFileTraceSink"
Cohesion: 0.08
Nodes (18): JsonFileTraceSink, app_, app_len_, discarded_lines_, dropped_in_window_, fd_, file_, max_per_window_ (+10 more)

### Community 99 - "from_json"
Cohesion: 0.08
Nodes (45): apply_to(), load_chrono_settings(), read_all(), test_chrono_worker_thread_settings(), test_clock_id(), test_enum_contracts(), test_provider_config_edges(), test_provider_config_roundtrip() (+37 more)

### Community 100 - "Requirements"
Cohesion: 0.05
Nodes (38): hub/web-ui Specification, Purpose, Requirement: Connection and error states are visible, Requirement: Embedded script parses and bootstraps, Requirement: Metrics render structured by kind, Requirement: Run tree reflects discovery, Requirement: State panels show execution_manager-derived details, Requirement: Stream viewer supports replay and live modes (+30 more)

### Community 101 - "StampSource"
Cohesion: 0.09
Nodes (14): reanchor(), StampSource, calibrated_, last_ns_, mono_anchor_ns_, scale_, tsc_anchor_, to_string() (+6 more)

### Community 102 - "Appender"
Cohesion: 0.19
Nodes (17): copy_name(), view_name(), Appender, cap, ok, out, used, emit_bucket() (+9 more)

### Community 103 - "ADDED Requirements"
Cohesion: 0.05
Nodes (37): ADDED Requirements, Purpose, Requirement: File replacement and new attempts are followed, Requirement: Lines are framed safely, Requirement: Rings are bounded per stream, Requirement: Slow subscribers are disconnected, Requirement: Streaming is cold path only, Requirement: Streaming types follow the serialization contract (+29 more)

### Community 104 - "ADDED Requirements"
Cohesion: 0.05
Nodes (37): ADDED Requirements, Purpose, Requirement: Best-effort application exposes partial success, Requirement: CpuSet has canonical diagnostics and JSON, Requirement: CpuSet represents CPUs without the 64-bit mask limit, Requirement: Required application reports failure and rolls back, Requirement: Thread enums and specs follow the serialization mandates, Requirement: ThreadApplyReport is a serializable startup snapshot (+29 more)

### Community 105 - "Requirements"
Cohesion: 0.04
Nodes (49): app/application Specification, Purpose, Requirement: Command-line options initialize the infrastructure, Requirement: Help is rejected as an initialization input, Requirement: Infrastructure stop reason is serializable, Requirement: Initialization follows a fixed order, Requirement: Process-global signal ownership guard, Requirement: Run loop observes the stop request (+41 more)

### Community 106 - "Requirements"
Cohesion: 0.06
Nodes (34): execution_manager/app-suite Specification, Purpose, Requirement: Complexity of decomposed functions stays within the project target, Requirement: Function Group configuration, Requirement: Function Group diagnostics, Requirement: Generated settings point at the attempt artifacts, Requirement: Refactored internals preserve all observable behavior, Requirement: `start_suite` is split into validation, conflict check, and supervisor launch (+26 more)

### Community 107 - "Requirements"
Cohesion: 0.05
Nodes (37): hub/streaming Specification, Purpose, Requirement: File replacement and new attempts are followed, Requirement: Lines are framed safely, Requirement: Rings are bounded per stream, Requirement: Slow subscribers are disconnected, Requirement: Streaming is cold path only, Requirement: Streaming types follow the serialization contract (+29 more)

### Community 108 - "Requirement: CPU-pinned background consumer"
Cohesion: 0.07
Nodes (29): Purpose, Requirement: Drop visibility, Requirement: Dynamic runtime control, Requirement: Per-thread shard ownership, Requirement: Pre-allocated provider memory, Requirement: Provider and tracer factory hierarchy, Requirement: Provider serialization surfaces, Requirement: Shutdown drain and flush barrier (+21 more)

### Community 109 - "Meter"
Cohesion: 0.32
Nodes (10): test_exponential_histogram(), test_exponential_histogram_created(), test_exponential_histogram_exact_powers(), test_exponential_histogram_invalid_scale(), test_exponential_histogram_meter(), test_exponential_histogram_probes(), test_exponential_histogram_reanchor(), test_exponential_histogram_serialization() (+2 more)

### Community 110 - "ADDED Requirements"
Cohesion: 0.05
Nodes (36): ADDED Requirements, Purpose, Requirement: Artifact roots come from suite metadata, Requirement: Discovery is cold path only, Requirement: Discovery is read-only, Requirement: Discovery types follow the serialization contract, Requirement: Initial scan builds the run tree, Requirement: Inotify keeps the tree live (+28 more)

### Community 111 - "Server"
Cohesion: 0.10
Nodes (13): Server, inner_, service_name_, telemetry_, ServerRequest, answered_, inner_, std::formatter<nexigen::com::Server<Req, Resp>> (+5 more)

### Community 112 - "Requirement: JSON-lines file sink"
Cohesion: 0.20
Nodes (9): D7 — One `FixedWriter`, one settings-io helper, Scenario: Scratch overflow discards the batch and counts it, Scenario: Scratch overflow discards the batch and counts it, Requirement: JsonFileTraceSink JSONL output, Scenario: Attribute values serialize by their packed type, Scenario: One parseable JSON object per span per line, Scenario: Open and write failures are visible, Scenario: Scratch overflow discards the line and counts it (+1 more)

### Community 113 - "AsyncWorker"
Cohesion: 0.07
Nodes (21): AsyncWorker, flush_, flush_sleep_budget, producer_, sink_, start_, thread_, thread_spec_ (+13 more)

### Community 114 - "instance_lifecycle.cpp"
Cohesion: 0.07
Nodes (39): ExitMetadata, exit_code, signal, terminated_by, uptime_s, to_string(), 1. Suite types and manifest, 2. Launch options for a single instance (+31 more)

### Community 115 - "ScopedSpan"
Cohesion: 0.08
Nodes (11): ScopedSpan, ended_, owner_, sampled_, slot_, stack_index_, SpanPasskey, std::formatter<nexigen::tracing::ScopedSpan<SC, MT>> (+3 more)

### Community 116 - "ADDED Requirements"
Cohesion: 0.06
Nodes (35): ADDED Requirements, Purpose, Requirement: App name is a local-only setting, Requirement: Environment overrides are documented and typed, Requirement: Explicit compile-time capacity limits are checked, Requirement: Invalid input degrades to best effort, Requirement: Merge precedence is deterministic, Requirement: Merged settings apply to logger configuration (+27 more)

### Community 117 - "ADDED Requirements"
Cohesion: 0.06
Nodes (35): ADDED Requirements, Purpose, Requirement: HTTP listener is loopback by default, Requirement: HTTP parsing and errors are bounded, Requirement: HTTP serving is cold path only, Requirement: Live updates use Server-Sent Events, Requirement: Read-only JSON endpoints, Requirement: The API is observation-only (+27 more)

### Community 118 - "ADDED Requirements"
Cohesion: 0.06
Nodes (34): ADDED Requirements, Purpose, Requirement: CPU-pinned background consumer, Requirement: Drop visibility, Requirement: Dynamic runtime control, Requirement: Per-thread shard ownership, Requirement: Pre-allocated provider memory, Requirement: Provider and tracer factory hierarchy (+26 more)

### Community 119 - "HistoryWriter"
Cohesion: 0.15
Nodes (6): build_history_line(), HistoryWriter, bytes_, fd_, max_bytes_, path_

### Community 120 - ".restart"
Cohesion: 0.11
Nodes (25): D10 — Test TU, latency harness, and the Release overhead benchmark, D11 — Example and documentation, D2 — `Stopwatch` layout: 64-byte, single cache line, zero atomics, D3 — Sampling and the clock-failure policy, D4 — Control-path arithmetic (the exact operations), D5 — Enum contract: `stopwatch_state.hpp`, PascalCase spellings, exact pattern copy, D6 — `LapSnapshot`: value struct, state-first layout, 24 bytes, D7 — `ScopedStopwatch`: single class template + CTAD, sink stored inline (+17 more)

### Community 121 - "Producer"
Cohesion: 0.09
Nodes (9): cached_thread_id(), Producer, level_, policy_, pool_, spam_, spam_state_, stamps_ (+1 more)

### Community 122 - "FunctionGroup"
Cohesion: 0.07
Nodes (32): AllowedTransition, from, to, FunctionGroup, allowed_transitions, application_group, name, preparation_timeout_ms (+24 more)

### Community 123 - "StateManager"
Cohesion: 0.07
Nodes (12): GroupRuntime, holder_priority, holder_requester, state_index, StateManager, groups_, machine_, manifest_ (+4 more)

### Community 124 - "ADDED Requirements"
Cohesion: 0.06
Nodes (33): ADDED Requirements, MODIFIED Requirements, REMOVED Requirements, Requirement: App exposes logger, metric and tracer factories, Requirement: App owns infrastructure through a CRTP contract, Requirement: App stop reason is serializable, Requirement: Current accessor is non-owning, Requirement: Heap-only infrastructure factory (+25 more)

### Community 125 - "ADDED Requirements"
Cohesion: 0.06
Nodes (33): ADDED Requirements, Purpose, Requirement: Driver and cleanup scripts run and reset the scenario, Requirement: Impact policy produces the documented timeline, Requirement: Roles and failure modes are environment-driven, Requirement: Settings drive launch through the command line, Requirement: Single configurable suite application executable, Requirement: Suite manifest exercises all three impact levels (+25 more)

### Community 126 - "snapshot_format.cpp"
Cohesion: 0.17
Nodes (30): metrics/snapshot-format, metrics/snapshot-format, from_json(), from_json_payload(), from_json_payload_histogram(), from_json_payload_scalar(), from_json_payload_state(), from_json_payload_statistics() (+22 more)

### Community 127 - "tracing/sinks_test.cpp"
Cohesion: 0.13
Nodes (19): 1. Decomposition pattern (all files), test_trace_writer_escape_matrix(), CountingSink, deliveries, FailingOnSpanSink, iso_shape(), test_sinks(), test_sinks_chrome() (+11 more)

### Community 128 - "Requirements"
Cohesion: 0.06
Nodes (33): core/spsc-ring-buffer Specification, Purpose, Requirement: Allocation-free and lock-free operation, Requirement: Cross-thread visibility of transferred elements, Requirement: Example diagnostic formatting, Requirement: First-in/first-out ordering and element integrity, Requirement: Fixed-capacity queue declared at compile time, Requirement: Full capacity is usable and empty is distinguishable from full (+25 more)

### Community 129 - "Requirements"
Cohesion: 0.06
Nodes (33): examples/suite-demo Specification, Purpose, Requirement: Driver and cleanup scripts run and reset the scenario, Requirement: Impact policy produces the documented timeline, Requirement: Roles and failure modes are environment-driven, Requirement: Settings drive launch through the command line, Requirement: Single configurable suite application executable, Requirement: Suite manifest exercises all three impact levels (+25 more)

### Community 130 - "make_infrastructure"
Cohesion: 0.06
Nodes (27): make_infrastructure(), Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why (+19 more)

### Community 131 - ".nanoseconds"
Cohesion: 0.05
Nodes (34): std::formatter<nexigen::chrono::Timestamp>, D6 — Registry layout and memory budget, Requirement: All deserialization is exception-free under `JSON_NOEXCEPTION=1`, Requirement: Complexity of decomposed functions stays within the project target, Requirement: Identity and scalar snapshots round-trip safely, Requirement: MetricBatchSnapshot deserialization is array-length safe, Requirement: MetricProviderConfig deserialization uses safe nlohmann accessors, Requirement: MetricRecord deserialization selects the union member by kind (+26 more)

### Community 132 - "EventId"
Cohesion: 0.11
Nodes (11): EventId, inner_, std::formatter<nexigen::com::EventId>, to_string(), Context, Decisions, Design, Goals / Non-Goals (+3 more)

### Community 133 - "WorkerTuning"
Cohesion: 0.07
Nodes (32): WorkerTuning, backoff_floor_us, default_poll_interval_us, default_spin_budget, max_poll_interval_us, min_poll_interval_us, poll_interval_us, spin_budget (+24 more)

### Community 134 - "ConsoleLoggerConfig"
Cohesion: 0.09
Nodes (21): ConsoleLoggerConfig, initial_level, worker_poll_interval_us, worker_thread, ADDED Requirements, MODIFIED Requirements, Purpose, Requirement: Background writer uses the shared idle-backoff policy (+13 more)

### Community 135 - "HealthState"
Cohesion: 0.04
Nodes (48): Entry, name, value, EnumTraits<nexigen::phm::HealthState>, entries, from_json(), from_string(), HealthState (+40 more)

### Community 136 - "SamplingDecision"
Cohesion: 0.07
Nodes (27): Entry, name, value, EnumTraits<nexigen::tracing::SamplingDecision>, entries, from_json(), from_string(), SamplingDecision (+19 more)

### Community 137 - "ADDED Requirements"
Cohesion: 0.06
Nodes (32): ADDED Requirements, Purpose, Requirement: Deferred worker start, Requirement: Periodic snapshot delivery and collection period, Requirement: Worker diagnostics, Requirement: Worker logger binding, Requirement: Worker logger rebinding contract, Requirement: Worker shutdown (+24 more)

### Community 138 - "Requirements"
Cohesion: 0.06
Nodes (32): chrono/worker Specification, Purpose, Requirement: Deferred worker start, Requirement: Periodic snapshot delivery and collection period, Requirement: Worker diagnostics, Requirement: Worker logger binding, Requirement: Worker logger rebinding contract, Requirement: Worker shutdown (+24 more)

### Community 139 - "http_server_test.cpp"
Cohesion: 0.23
Nodes (16): test_http_server(), test_http_server_all_stream_sse(), test_http_server_cursor_resume(), test_http_server_ipv6_listen(), test_http_server_keep_alive(), test_http_server_malformed(), test_http_server_methods(), test_http_server_raw() (+8 more)

### Community 140 - "SpamProtection"
Cohesion: 0.08
Nodes (16): message_hash(), SpamProtection, bypass_min_level_, enable_repeat_suppression_, max_messages_per_sec_per_shard_, repeat_suppression_window_ms_, to_string(), SpamShardState (+8 more)

### Community 141 - "Iceoryx2LoggerConfig"
Cohesion: 0.08
Nodes (23): Iceoryx2LoggerConfig, app_id, history_size, initial_level, max_subscribers, service_name, throttle, worker_poll_interval_us (+15 more)

### Community 142 - "LogFrame"
Cohesion: 0.05
Nodes (42): LogFrame, app_id, context_id, dropped_counter, log_level, payload, payload_size, pid (+34 more)

### Community 143 - "resolve_stop_state"
Cohesion: 0.06
Nodes (57): 11. P3 completion notes (2026-11-01), group_alive(), identity_matches(), validate_env_entries(), 1. Runner: identity gate before kill (P1-5), runner/process-supervision, 1. `validate_start_options` decomposition, 3. `Runner::stop` decomposition (+49 more)

### Community 144 - "ADDED Requirements"
Cohesion: 0.06
Nodes (31): ADDED Requirements, Purpose, Requirement: Chrono sink concept, Requirement: Composite chrono sink fan-out, Requirement: Console chrono sink, Requirement: File sink configuration serialization, Requirement: JSON-file chrono sink, Requirement: Null chrono sink (+23 more)

### Community 145 - "ADDED Requirements"
Cohesion: 0.06
Nodes (31): ADDED Requirements, Purpose, Requirement: Identity and resolved paths are serializable, Requirement: Identity validation rejects unsafe values, Requirement: Instance directory is derived when not supplied, Requirement: Invalid identity fails path resolution, Requirement: Run identity is read from a fixed environment contract, Requirement: Sink paths resolve from settings, then instance identity (+23 more)

### Community 146 - "Requirements"
Cohesion: 0.06
Nodes (31): app/telemetry-layout Specification, Purpose, Requirement: Identity and resolved paths are serializable, Requirement: Identity validation rejects unsafe values, Requirement: Instance directory is derived when not supplied, Requirement: Invalid identity fails path resolution, Requirement: Run identity is read from a fixed environment contract, Requirement: Sink paths resolve from settings, then instance identity (+23 more)

### Community 147 - "Requirements"
Cohesion: 0.06
Nodes (31): chrono/chrono-sink Specification, Purpose, Requirement: Chrono sink concept, Requirement: Composite chrono sink fan-out, Requirement: Console chrono sink, Requirement: File sink configuration serialization, Requirement: JSON-file chrono sink, Requirement: Null chrono sink (+23 more)

### Community 148 - "supervise_child"
Cohesion: 0.11
Nodes (23): 2. Runner: waitpid outcome honesty + failed-start reaping (P1-6, P1-14), 4. Metrics: get_meter serialization (P1-8), 5. Tracing: int8 slot invariant (P1-11), 6. Chrono: start guard (P1-12), 7. Hub: IPv6 listen + CLI loopback fix (P1-13), Tasks, Analysis / preparation, hub/http-api (+15 more)

### Community 149 - "TransitionResult"
Cohesion: 0.08
Nodes (27): Entry, name, value, EnumTraits<nexigen::sm::TransitionResult>, entries, from_json(), from_string(), to_json() (+19 more)

### Community 150 - "ADDED Requirements"
Cohesion: 0.06
Nodes (30): ADDED Requirements, Purpose, Requirement: ChromeTraceSink Perfetto-compatible output, Requirement: CompositeTraceSink fan-out, Requirement: ConsoleTraceSink, Requirement: JsonFileTraceSink JSONL output, Requirement: JsonFileTraceSink rotation and rate limiting, Requirement: Live sink serialization surfaces (+22 more)

### Community 151 - "Requirements"
Cohesion: 0.07
Nodes (26): metrics/metrics-settings Specification, Purpose, Requirement: Environment overrides are documented and typed, Requirement: Invalid input degrades to best effort, Requirement: Invalid sink configuration degrades to best effort, Requirement: Merge precedence matches the other subsystems, Requirement: Settings loading is cold path only, Requirement: Sink fields merge with documented precedence (+18 more)

### Community 152 - "tracing_settings.cpp"
Cohesion: 0.18
Nodes (26): apply_env_mask(), apply_env_max_spans(), apply_env_max_tracers(), apply_env_window_ns(), apply_tracing_environment(), merge_tracing_document(), read_core_mask(), read_max_spans_per_window() (+18 more)

### Community 153 - "sample_utc_offset"
Cohesion: 0.12
Nodes (28): utc_from_mono_ns(), D2 — Header/namespace layout: three public headers, `detail` level dropped, D5 — Logging becomes a consumer of chrono, What Changes, 1. Relocate time files into nexigen::chrono, 3. Logging consumers use nexigen::chrono, Context, Context (+20 more)

### Community 154 - "ChronoWorker"
Cohesion: 0.09
Nodes (9): ChronoWorker, config_, log_binding_, startup_error_, startup_published_, wake_cv_, wake_mutex_, worker_ (+1 more)

### Community 155 - "LapSnapshot"
Cohesion: 0.07
Nodes (26): LapSnapshot, lap_ns, lap_number, state, total_ns, to_string(), D1 — Build shape: compiled STATIC target with a single cold-path unit, Design (+18 more)

### Community 156 - "StreamRecord"
Cohesion: 0.07
Nodes (26): GapMarker, dropped, first_missing_sequence, last_missing_sequence, stream_id, to_string(), StreamRecord, kind (+18 more)

### Community 157 - "drain_frames"
Cohesion: 0.11
Nodes (13): drain_frames(), ensure_dir(), flush_interval_of(), HttpFront, server, stop, print_banner(), run_session() (+5 more)

### Community 158 - "OverflowPolicy"
Cohesion: 0.11
Nodes (19): Entry, name, value, EnumTraits<nexigen::logging::OverflowPolicy>, entries, from_json(), from_string(), OverflowPolicy (+11 more)

### Community 159 - "ConsoleTraceSink"
Cohesion: 0.11
Nodes (7): ConsoleTraceSink, fd_, len_, line_, line_capacity, std::formatter<nexigen::tracing::ConsoleTraceSink>, to_string()

### Community 160 - "ADDED Requirements"
Cohesion: 0.07
Nodes (29): ADDED Requirements, Purpose, Requirement: Context extraction, Requirement: Cross-thread context linking, Requirement: Disabled and dropped short-circuit, Requirement: RAII span lifecycle, Requirement: ScopedSpan serialization surface, Requirement: Sub-20-nanosecond enqueue (+21 more)

### Community 161 - "ADDED Requirements"
Cohesion: 0.07
Nodes (29): ADDED Requirements, Purpose, Requirement: App settings aggregate every subsystem, Requirement: App settings are serializable, Requirement: Environment overrides are documented and typed, Requirement: Invalid input degrades to best effort, Requirement: Merge precedence matches logging, Requirement: Metrics and tracing use explicit file pairs (+21 more)

### Community 162 - "ADDED Requirements"
Cohesion: 0.08
Nodes (25): ADDED Requirements, Purpose, Requirement: Cold-path serialization of compiled modules is built once, Requirement: Complexity ceiling is enforced across the maintained source tree, Requirement: Complexity refactors preserve observable behavior and public surface, Requirement: Test suites stay within the ceiling without losing coverage, Requirement: The complexity check runs with the test suite, Scenario: A violating function fails the check (+17 more)

### Community 163 - "ADDED Requirements"
Cohesion: 0.07
Nodes (29): ADDED Requirements, Purpose, Requirement: Concurrent manual triggers never interleave, Requirement: Fixed stamper bank capacity, Requirement: Manual snapshot delivery, Requirement: Provider diagnostics, Requirement: Provider shutdown, Requirement: Snapshot content (+21 more)

### Community 164 - "Requirements"
Cohesion: 0.07
Nodes (29): chrono/provider Specification, Purpose, Requirement: Concurrent manual triggers never interleave, Requirement: Fixed stamper bank capacity, Requirement: Manual snapshot delivery, Requirement: Provider diagnostics, Requirement: Provider shutdown, Requirement: Snapshot content (+21 more)

### Community 165 - "Requirements"
Cohesion: 0.04
Nodes (46): ADDED Requirements, Purpose, Requirement: Asynchronous wait-free console output, Requirement: Bounded single-producer contract, Requirement: Console line content, Requirement: Flush drains asynchronously-accepted records, Requirement: Stream routing by severity, Scenario: Contract is stated on the type (+38 more)

### Community 166 - "Requirements"
Cohesion: 0.07
Nodes (29): Purpose, Requirement: Context extraction, Requirement: Cross-thread context linking, Requirement: Disabled and dropped short-circuit, Requirement: RAII span lifecycle, Requirement: ScopedSpan serialization surface, Requirement: Sub-20-nanosecond enqueue, Requirement: Wait-free span updates (+21 more)

### Community 168 - "ConsoleMetricSink"
Cohesion: 0.11
Nodes (9): ConsoleMetricSink, fd_, len_, line_, line_capacity, std::formatter<nexigen::metrics::ConsoleMetricSink>, to_string(), from_json() (+1 more)

### Community 169 - "ADDED Requirements"
Cohesion: 0.07
Nodes (26): ADDED Requirements, Purpose, Requirement: Daemon aggregates the latest record per metric, Requirement: Daemon renders Prometheus text exposition, Requirement: Daemon serves GET /metrics over HTTP, Requirement: Daemon subscribes and drains frames without polling, Requirement: Daemon writes a quota-bounded JSON-lines history, Requirement: MetricsDaemon configuration (+18 more)

### Community 170 - "ADDED Requirements"
Cohesion: 0.07
Nodes (28): ADDED Requirements, Purpose, Requirement: Bitwise bucket index mapping, Requirement: Bounded bucket storage and coarsening, Requirement: Exponential histogram percentile snapshots, Requirement: Exponential histogram reset windows, Requirement: Exponential scale configuration, Requirement: Multi-threaded exponential histogram recording (+20 more)

### Community 171 - "ADDED Requirements"
Cohesion: 0.07
Nodes (28): ADDED Requirements, Purpose, Requirement: Connection and error states are visible, Requirement: Run tree reflects discovery, Requirement: State panels show runner-derived details, Requirement: Stream viewer supports replay and live modes, Requirement: UI is a single embedded application, Requirement: UI is read-only (+20 more)

### Community 172 - "Requirements"
Cohesion: 0.07
Nodes (28): examples Specification, Purpose, Requirement: CTest smoke coverage for every example, Requirement: Documented example invocation, Requirement: Example execution_manager and cleanup scripts, Requirement: Example exit status reports success, Requirement: Examples pass in every supported build mode, Requirement: Optional example build (+20 more)

### Community 173 - "Requirements"
Cohesion: 0.07
Nodes (28): metrics/exponential-histogram Specification, Purpose, Requirement: Bitwise bucket index mapping, Requirement: Bounded bucket storage and coarsening, Requirement: Exponential histogram percentile snapshots, Requirement: Exponential histogram reset windows, Requirement: Exponential scale configuration, Requirement: Multi-threaded exponential histogram recording (+20 more)

### Community 174 - "Requirements"
Cohesion: 0.07
Nodes (28): Purpose, Requirement: ChromeTraceSink Perfetto-compatible output, Requirement: CompositeTraceSink fan-out, Requirement: ConsoleTraceSink, Requirement: JsonFileTraceSink rotation and rate limiting, Requirement: Live sink serialization surfaces, Requirement: Sink configuration serialization, Requirement: Trace spans carry the application name (+20 more)

### Community 175 - "diagnostics_test.cpp"
Cohesion: 0.11
Nodes (23): arena_exhausted(), main(), median_ns_per_op(), now_ns_f(), operator delete(), operator new(), OutRefGuard, ProbeSink (+15 more)

### Community 176 - "main"
Cohesion: 0.06
Nodes (23): fnv1a_64(), D3 — FNV-1a 64 с memo-кэшем на worker, suite_attributes(), main(), suite_serialization_full(), suite_writer_bounds_full(), suite_writer_escape_matrix(), test_record_serialization_full() (+15 more)

### Community 177 - "SpscRingBuffer"
Cohesion: 0.10
Nodes (13): ConsumerCell, head, tail_cache, ProducerCell, head_cache, tail, SpscRingBuffer, consumer_ (+5 more)

### Community 178 - "Requirements"
Cohesion: 0.08
Nodes (25): Purpose, quality/code-structure Specification, Requirement: Cold-path serialization of compiled modules is built once, Requirement: Complexity ceiling is enforced across the maintained source tree, Requirement: Complexity refactors preserve observable behavior and public surface, Requirement: Test suites stay within the ceiling without losing coverage, Requirement: The complexity check runs with the test suite, Requirements (+17 more)

### Community 179 - "ConsoleSink"
Cohesion: 0.05
Nodes (29): ConsoleSink, fd_, len_, line_, line_capacity, std::formatter<nexigen::chrono::ConsoleSink>, to_string(), metrics/state-gauge Specification (+21 more)

### Community 180 - "SampleMut"
Cohesion: 0.28
Nodes (4): SampleMut, inner_, SampleMutUninit, inner_

### Community 181 - "WaitSet"
Cohesion: 0.08
Nodes (13): std::formatter<nexigen::com::WaitSet>, to_string(), WaitSet, inner_, 1. Введение и область применения, 2. Мониторинг выполнения (Supervision Core), 3. Сбор и обработка локальных сбоев (Health Channel Management), 4. Стратегии восстановления и интеграция с EM (Recovery Actions) (+5 more)

### Community 182 - "Gauge"
Cohesion: 0.09
Nodes (12): Gauge, access, concurrency, identity_, kind, reset, storage_, GaugeStorage (+4 more)

### Community 183 - "MachineState"
Cohesion: 0.08
Nodes (24): Entry, name, value, EnumTraits<nexigen::sm::MachineState>, entries, from_json(), from_string(), MachineState (+16 more)

### Community 184 - "ADDED Requirements"
Cohesion: 0.07
Nodes (27): ADDED Requirements, Purpose, Requirement: Asynchronous wait-free JSON-lines file output, Requirement: Automatic dropped-record summary, Requirement: Background writer CPU core affinity, Requirement: Hitch-free scheduled directory rotation, Requirement: Hot-path rate limiting, Requirement: Nanosecond UTC timestamps via calibrated clocks (+19 more)

### Community 185 - "3. Per-file plan"
Cohesion: 0.18
Nodes (10): 2. Shared state, 3. Per-file plan, 4. No ODR / linkage impact, Context, Decisions, Design, Goals / Non-Goals, Migration Plan (+2 more)

### Community 186 - "nexigen-exec/main.cpp"
Cohesion: 0.20
Nodes (24): D5 — CLI: file-local helpers in one TU, byte-identical output, activate_logging(), apply_control_affinity(), cli_fail(), cli_ok(), CliLogTarget, control_thread, level (+16 more)

### Community 187 - "Requirement: Environment overrides are documented and typed"
Cohesion: 0.20
Nodes (9): MODIFIED Requirements, REMOVED Requirements, Requirement: App name is a local-only setting, Requirement: Environment overrides are documented and typed, Scenario: Affinity override accepts a core list, Scenario: Level override parses enum names, Scenario: Message capacity override parses a positive integer, Scenario: Unset variables do not erase file values (+1 more)

### Community 188 - "ClockId"
Cohesion: 0.08
Nodes (23): ClockId, boottime, monotonic_raw, realtime, Entry, name, value, EnumTraits<nexigen::chrono::ClockId> (+15 more)

### Community 189 - "proc_start_ticks"
Cohesion: 0.11
Nodes (25): proc_start_ticks(), Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, Why, MODIFIED Requirements (+17 more)

### Community 190 - "rotation.cpp"
Cohesion: 0.21
Nodes (20): RotationScratch, dir, file, 2. `open_segment` — phase helpers on the same scratch buffers, Risks / Trade-offs, 3. logging/rotation `open_segment`, 6. detail machinery, build_rotation_dir() (+12 more)

### Community 191 - "Requirements"
Cohesion: 0.07
Nodes (26): metrics/metrics-daemon Specification, Purpose, Requirement: Daemon aggregates the latest record per metric, Requirement: Daemon renders Prometheus text exposition, Requirement: Daemon serves GET /metrics over HTTP, Requirement: Daemon subscribes and drains frames without polling, Requirement: Daemon writes a quota-bounded JSON-lines history, Requirement: MetricsDaemon configuration (+18 more)

### Community 192 - "Decisions"
Cohesion: 0.08
Nodes (23): Context, D1. Public surface and file layout, D2. Grammar and parser algorithm, D4. Log-level precedence, D5. Missing-directory behavior, D6. Infrastructure integration, D7. Serialization and diagnostics, D8. Memory and concurrency (+15 more)

### Community 193 - "Hub_options"
Cohesion: 0.13
Nodes (16): 5. nexigen-hub CLI, classify_listen_host(), hub_diagnostic(), HubOptions, listen, max_line_bytes, ring_bytes, ring_records (+8 more)

### Community 194 - "Requirements"
Cohesion: 0.06
Nodes (30): logging/console-logger Specification, Purpose, Requirement: Asynchronous wait-free console output, Requirement: Background writer uses the shared idle-backoff policy, Requirement: Console line content, Requirement: Console logger diagnostic strings, Requirement: Console writer accepts a thread specification, Requirement: Flush drains asynchronously-accepted records (+22 more)

### Community 195 - "command_line.cpp"
Cohesion: 0.10
Nodes (34): CommandLineResult, CommandLineDiagnostic, message, option, to_string(), add_diagnostic(), Collected, app_name (+26 more)

### Community 196 - "ShardPool"
Cohesion: 0.09
Nodes (8): ShardPool, drain_shard_, drain_shard_pops_, exhausted_, ShardPool<Record, ShardCapacity, MaxThreads>::pop(), ShardPool<Record, ShardCapacity, MaxThreads>::ring_size(), registration_, shards_

### Community 197 - "TraceDaemonSession"
Cohesion: 0.07
Nodes (12): TraceDaemonSession, config_, interval_guard_, node_, recovery_guard_, recovery_listener_, recovery_subscriber_, reporter_ (+4 more)

### Community 198 - "Iceoryx2Sink"
Cohesion: 0.08
Nodes (16): Iceoryx2Sink, frame_, last_dropped_total_, node_, notifier_, producer_, publisher_, settings_ (+8 more)

### Community 199 - "metrics/latency_test.cpp"
Cohesion: 0.25
Nodes (17): Readable, Cumulative, median_ns_per_op(), now_ns(), test_latency(), test_latency_disabled(), test_latency_exp_record(), test_latency_gauge_add() (+9 more)

### Community 200 - "EventListener"
Cohesion: 0.13
Nodes (8): RecoveryHandler, channel_name_, last_group_, manager_, npos, subscriber_, std::formatter<nexigen::sm::RecoveryHandler>, to_string()

### Community 201 - "Tracer"
Cohesion: 0.10
Nodes (13): SpanPasskey, std::formatter<nexigen::tracing::SpanPasskey>, std::formatter<nexigen::tracing::Tracer<SC, MT>>, to_string(), Tracer, name_, provider_, TracerProvider (+5 more)

### Community 202 - "ADDED Requirements"
Cohesion: 0.07
Nodes (28): ADDED Requirements, Purpose, Requirement: Constexpr policy names, Requirement: Non-throwing policy JSON serialization, Requirement: Policy enum catalogue, Requirement: Policy enum formatting, Requirement: Public type diagnostic strings, Requirement: Public type formatting (+20 more)

### Community 203 - "ADDED Requirements"
Cohesion: 0.08
Nodes (25): ADDED Requirements, Purpose, Requirement: Environment overrides are documented and typed, Requirement: Invalid input degrades to best effort, Requirement: Merge precedence is deterministic, Requirement: Merged settings apply to provider configuration, Requirement: Settings diagnostics are serializable, Requirement: Settings loading is cold path only (+17 more)

### Community 204 - "Requirements"
Cohesion: 0.08
Nodes (25): Purpose, Requirement: All deserialization is exception-free under `JSON_NOEXCEPTION=1`, Requirement: Complexity of decomposed functions stays within the project target, Requirement: DroppedStats deserialization is non-throwing and partial-field safe, Requirement: Identifier hex format is fixed and round-trips, Requirement: Serialization helpers do not change hot-path contracts, Requirement: SpanAttribute deserialization is all-or-nothing, Requirement: SpanRecord deserialization delegates to substructure helpers (+17 more)

### Community 205 - "logger_options.cpp"
Cohesion: 0.23
Nodes (14): 2. Templated/parameterized field readers for `from_json`, What Changes, from_json(), LoggerOptions::to_string(), read_bool_field(), read_core_mask_field(), read_flush_cpu_affinity_field(), read_log_level_field() (+6 more)

### Community 206 - "Requirements"
Cohesion: 0.07
Nodes (28): metrics/policy-enums Specification, Purpose, Requirement: Constexpr policy names, Requirement: Non-throwing policy JSON serialization, Requirement: Policy enum catalogue, Requirement: Policy enum formatting, Requirement: Public type diagnostic strings, Requirement: Public type formatting (+20 more)

### Community 207 - "Requirements"
Cohesion: 0.08
Nodes (25): Purpose, Requirement: All deserialization is exception-free under `JSON_NOEXCEPTION=1`, Requirement: Complexity of decomposed functions stays within the project target, Requirement: DroppedStats deserialization is non-throwing and partial-field safe, Requirement: Identifier hex format is fixed and round-trips, Requirement: Serialization helpers do not change hot-path contracts, Requirement: SpanAttribute deserialization is all-or-nothing, Requirement: SpanRecord deserialization delegates to substructure helpers (+17 more)

### Community 208 - "RecordingSink"
Cohesion: 0.09
Nodes (10): FailingFlushSink, RecordingSink, base, count, deliverer_mask, deliverer_tid, flush_calls, over_capacity (+2 more)

### Community 209 - "ThreadStateSnapshot"
Cohesion: 0.13
Nodes (16): restore_affinity(), restore_name(), restore_scheduler(), rollback_applied_attributes(), snapshot_current(), ThreadStateSnapshot, affinity, name (+8 more)

### Community 210 - "JsonFileSink"
Cohesion: 0.11
Nodes (8): JsonFileSink, fd_, len_, scratch_, scratch_capacity, std::formatter<nexigen::chrono::JsonFileSink>, to_json(), to_string()

### Community 211 - "PendingResponse"
Cohesion: 0.13
Nodes (9): ClientTelemetry, correlation_, outstanding_, PendingResponse, inner_, telemetry_, std::formatter<nexigen::com::PendingResponse<Req, Resp>>, to_string() (+1 more)

### Community 212 - "ServiceManifest"
Cohesion: 0.10
Nodes (17): ProvidedService, history_size, max_subscribers, service_name, RequiredService, buffer_capacity, report_overflow_to_phm, service_name (+9 more)

### Community 213 - "Subscriber"
Cohesion: 0.08
Nodes (23): Telemetry, lost_samples_, peak_recipients_, queued_samples_, send_count_, Capabilities, Impact, Modified Capabilities (+15 more)

### Community 214 - ".create_without_signal_handling"
Cohesion: 0.11
Nodes (18): Context, D1 — `create_without_signal_handling()` plus the daemon's own handlers, D2 — Keep the interval attachment as the exit-latency bound, D3 — Regression test as a shell lifecycle test, matching metricsd's smoke test, Decisions, Design, Goals / Non-Goals, Risks / Trade-offs (+10 more)

### Community 215 - "SupervisionStatus"
Cohesion: 0.14
Nodes (14): Entry, name, value, EnumTraits<nexigen::phm::SupervisionStatus>, entries, from_json(), from_string(), SupervisionStatus (+6 more)

### Community 216 - "ADDED Requirements"
Cohesion: 0.08
Nodes (24): ADDED Requirements, Purpose, Requirement: Allocation-free and lock-free operation, Requirement: Cross-thread visibility of transferred elements, Requirement: First-in/first-out ordering and element integrity, Requirement: Fixed-capacity queue declared at compile time, Requirement: Full capacity is usable and empty is distinguishable from full, Requirement: Non-blocking push and pop with classified outcomes (+16 more)

### Community 217 - "MODIFIED Requirements"
Cohesion: 0.08
Nodes (24): ADDED Requirements, MODIFIED Requirements, Requirement: Asynchronous wait-free JSON-lines file output, Requirement: Automatic dropped-record summary, Requirement: File logger vocabulary diagnostic strings, Requirement: Hot-path rate limiting, Requirement: Rotation schedule vocabulary, Requirement: Strict JSON record schema (+16 more)

### Community 218 - "ADDED Requirements"
Cohesion: 0.08
Nodes (24): ADDED Requirements, Purpose, Requirement: Counter and gauge example, Requirement: Descriptive statistics example, Requirement: Histogram example, Requirement: Metrics example targets, Requirement: Provider and sink pipeline example, Scenario: Background harvesting honors interval and affinity (+16 more)

### Community 219 - "ADDED Requirements"
Cohesion: 0.06
Nodes (30): ADDED Requirements, Purpose, Requirement: CPU core affinity, Requirement: Factory hierarchy and passkey construction, Requirement: Meter instrument creation, Requirement: Pre-allocated aligned metric registry, Requirement: Provider configuration serialization, Requirement: Provider shutdown (+22 more)

### Community 220 - "ADDED Requirements"
Cohesion: 0.07
Nodes (28): ADDED Requirements, Purpose, Requirement: Background snapshot delivery and collection period, Requirement: CPU core affinity, Requirement: Provider configuration serialization, Requirement: Provider construction and sink binding, Requirement: Provider control plane, Requirement: Provider diagnostics (+20 more)

### Community 221 - "Requirement: Affinity failures are non-fatal warnings"
Cohesion: 0.08
Nodes (24): MODIFIED Requirements, Requirement: Affinity failures are non-fatal warnings, Requirement: Global logging settings supply operational fields, Requirement: Manifest configures runner thread affinity, Requirement: Suite metadata preserves runner affinity, Scenario: Applications are not affected by a failed pin, Scenario: Applications keep their own affinity, Scenario: Control thread is pinned (+16 more)

### Community 222 - "Requirements"
Cohesion: 0.08
Nodes (24): logging/log-record Specification, Purpose, Requirement: Cached thread identity, Requirement: Consumer-side rendering of packed records, Requirement: Deferred type-erased argument formatting, Requirement: Hot-path latency budget, Requirement: LogRecord diagnostic string, Requirement: Monotonic nanosecond stamping (+16 more)

### Community 223 - "Requirements"
Cohesion: 0.08
Nodes (24): metrics/examples Specification, Purpose, Requirement: Counter and gauge example, Requirement: Descriptive statistics example, Requirement: Histogram example, Requirement: Metrics example targets, Requirement: Provider and sink pipeline example, Requirements (+16 more)

### Community 224 - "Requirements"
Cohesion: 0.05
Nodes (38): metrics/metric-provider Specification, Purpose, Requirement: CPU core affinity, Requirement: Factory hierarchy and passkey construction, Requirement: Meter instrument creation, Requirement: Meter publication is serialized with harvesting, Requirement: Pre-allocated aligned metric registry, Requirement: Provider configuration serialization (+30 more)

### Community 225 - "test_scoped_span"
Cohesion: 0.34
Nodes (18): test_scoped_span(), test_scoped_span_alloc_free(), test_scoped_span_attribute_bounds(), test_scoped_span_context(), test_scoped_span_control_flips(), test_scoped_span_cross_thread(), test_scoped_span_depth_overflow(), test_scoped_span_disabled_short_circuit() (+10 more)

### Community 226 - "RecordingSink"
Cohesion: 0.10
Nodes (12): CountSink, count, FailSink, RecordingClockLogger, messages, RecordingSink, affinity_ok, expected_cpu (+4 more)

### Community 227 - "nexigen-hub"
Cohesion: 0.20
Nodes (9): Discovery, HTTP API, Limitations, Limits, nexigen-hub, Run, Security, Streams (+1 more)

### Community 228 - "StreamKind"
Cohesion: 0.09
Nodes (21): Entry, name, value, EnumTraits<nexigen::hub::StreamKind>, entries, fallback, from_string(), StreamKind (+13 more)

### Community 229 - "ADDED Requirements"
Cohesion: 0.08
Nodes (23): ADDED Requirements, Purpose, Requirement: App owns infrastructure through a CRTP contract, Requirement: App stop reason is serializable, Requirement: Current accessor is non-owning, Requirement: Initialization follows a fixed order, Requirement: Run loop observes the stop request, Requirement: SIGINT and SIGTERM request a graceful stop (+15 more)

### Community 230 - ".inc"
Cohesion: 0.06
Nodes (35): D10 — ExplicitBucketHistogram, D11 — ExponentialHistogram: exact O(1) mapping without `std::log`, D16 — Tests and latency gates, D17 — Examples and documentation (retired), D1 — Module shape: one STATIC target `nexigen_metrics` (alias `nexigen::metrics`), D3 — Naming, D4 — Policy enums and the serialization contract, D5 — Concepts, passkey construction, and handle types (+27 more)

### Community 231 - "chrono/settings.cpp"
Cohesion: 0.23
Nodes (18): apply_env_interval(), apply_env_mask(), apply_environment(), merge_document(), read_collect_interval(), read_core_mask(), read_kind(), read_sink_append() (+10 more)

### Community 232 - "Client"
Cohesion: 0.16
Nodes (6): Client, inner_, service_name_, telemetry_, std::formatter<nexigen::com::Client<Req, Resp>>, to_string()

### Community 233 - "Publisher"
Cohesion: 0.20
Nodes (6): Publisher, inner_, service_name_, telemetry_, to_string(), SpanFrame::to_string()

### Community 234 - "InfrastructureStopReason"
Cohesion: 0.11
Nodes (15): Entry, name, value, from_string(), InfrastructureStopReason, None, Requested, Sigint (+7 more)

### Community 235 - "StateGauge"
Cohesion: 0.11
Nodes (11): MeterPasskey, StateGauge, access, concurrency, identity_, kind, reset, storage_ (+3 more)

### Community 236 - "StartCell"
Cohesion: 0.10
Nodes (15): ControlCell, pad_, state, FlushCell, drained, pad_, requested, sync_error (+7 more)

### Community 237 - "Requirements"
Cohesion: 0.09
Nodes (22): metrics/explicit-histogram Specification, Purpose, Requirement: Explicit bucket configuration, Requirement: Explicit histogram percentile snapshots, Requirement: Explicit histogram recording and aggregation, Requirement: Explicit histogram reset windows, Requirement: Multi-threaded explicit histogram recording, Requirements (+14 more)

### Community 238 - "ADDED Requirements"
Cohesion: 0.09
Nodes (22): ADDED Requirements, Purpose, Requirement: Explicit bucket configuration, Requirement: Explicit histogram percentile snapshots, Requirement: Explicit histogram recording and aggregation, Requirement: Explicit histogram reset windows, Requirement: Multi-threaded explicit histogram recording, Scenario: Bounds are immutable after creation (+14 more)

### Community 239 - "Requirement: Background snapshot delivery and collection period"
Cohesion: 0.09
Nodes (22): ADDED Requirements, MODIFIED Requirements, Requirement: Background snapshot delivery and collection period, Requirement: Deferred worker start, Requirement: Optional worker logger, Requirement: Provider construction and sink binding, Requirement: Provider shutdown, Scenario: Clean shutdown drains the sink (+14 more)

### Community 240 - "ADDED Requirements"
Cohesion: 0.09
Nodes (22): ADDED Requirements, Purpose, Requirement: Multi-threaded last-writer-wins, Requirement: Single-threaded state storage, Requirement: State read surface, Requirement: State recording surface, Requirement: State reset windows, Requirement: State serialization contract (+14 more)

### Community 241 - "ADDED Requirements"
Cohesion: 0.09
Nodes (22): ADDED Requirements, Purpose, Requirement: Fan-out construction constraints, Requirement: Plumbing serialization scope, Requirement: Shared file-open policy, Requirement: Shared full-write loop, Requirement: Shared settings loader vocabulary, Requirement: Variadic fan-out member pack (+14 more)

### Community 242 - "Requirements"
Cohesion: 0.09
Nodes (22): core/sink-plumbing Specification, Purpose, Requirement: Fan-out construction constraints, Requirement: Plumbing serialization scope, Requirement: Shared file-open policy, Requirement: Shared full-write loop, Requirement: Shared settings loader vocabulary, Requirement: Variadic fan-out member pack (+14 more)

### Community 243 - "Requirement: Runnable composite logger example"
Cohesion: 0.09
Nodes (22): logging/composite-logger Specification, Purpose, Requirement: Composite logger diagnostic string, Requirement: Level control propagation, Requirement: Runnable composite logger example, Requirement: Sub-logger access by index and by type, Requirement: Virtual-free fan-out composition, Requirements (+14 more)

### Community 244 - "LogDaemonConfig"
Cohesion: 0.07
Nodes (26): LogDaemonConfig, batch_flush_interval_ms, bg_cpu_affinity, log_directory, max_archive_files, max_file_size_mb, min_log_level_storage, service_name (+18 more)

### Community 245 - "ADDED Requirements"
Cohesion: 0.07
Nodes (27): ADDED Requirements, Purpose, Requirement: Per-span formation stays within its budget, Requirement: Publication never blocks and drops are counted, Requirement: Sequence numbers make gaps observable, Requirement: Sink publishes each span zero-copy from the worker, Requirement: Source hashes are memoized on the worker, Requirement: Source locations cross processes as hashes (+19 more)

### Community 246 - "command_line_test.cpp"
Cohesion: 0.23
Nodes (15): has_diagnostic(), message_contains(), parse_args(), test_command_line_parser(), test_command_line_parser_aliases(), test_command_line_parser_argv0(), test_command_line_parser_directory_validation(), test_command_line_parser_errors() (+7 more)

### Community 247 - "file_logger_test.cpp"
Cohesion: 0.41
Nodes (11): expect_invalid(), file_logger_base(), test_file_logger(), test_file_logger_happy_path(), test_file_logger_rotation(), test_file_logger_soak(), test_file_logger_throttling(), test_file_logger_validation() (+3 more)

### Community 248 - "RecordingSink"
Cohesion: 0.19
Nodes (5): test_registry_zero_alloc(), test_provider_harvester_control(), RecordingSink, flush_status_, status_

### Community 249 - "TransitionAudit"
Cohesion: 0.13
Nodes (8): std::formatter<nexigen::sm::TransitionAudit>, to_string(), TransitionAudit, file_, path_, queue_, shutdown_, worker_

### Community 250 - "RunIdentity"
Cohesion: 0.11
Nodes (27): RunIdentity, instance_dir, instance_id, run_dir, run_id, to_string(), D4. Identity is a plain value loaded from five variables, D5. Validation is conservative but permits an empty identity (+19 more)

### Community 251 - "ADDED Requirements"
Cohesion: 0.09
Nodes (21): ADDED Requirements, Purpose, Requirement: Invalid sink configuration degrades to best effort, Requirement: Metrics and tracing settings carry a sink member, Requirement: Sink configuration is part of settings serialization, Requirement: Sink fields merge with documented precedence, Requirement: Sink objects read the existing file-sink keys, Scenario: Absent local sink field inherits global (+13 more)

### Community 252 - "ADDED Requirements"
Cohesion: 0.09
Nodes (21): ADDED Requirements, Purpose, Requirement: exit.json captures exit metadata, Requirement: Generated settings contain absolute sink paths, Requirement: Instance directory layout is deterministic, Requirement: Lifecycle is cold path only, Requirement: resource.json and exit.json are serializable, Requirement: resource.json captures start-time metadata (+13 more)

### Community 253 - "ADDED Requirements"
Cohesion: 0.09
Nodes (21): ADDED Requirements, Purpose, Requirement: Child is launched under a new process group, Requirement: Idle timeout terminates the child, Requirement: Process supervision is I/O-only on cold paths, Requirement: Status reports lifecycle state, Requirement: Stdout and stderr are captured to files, Requirement: Stop delivers SIGTERM then SIGKILL on timeout (+13 more)

### Community 254 - "Meter"
Cohesion: 0.10
Nodes (21): expect(), main(), step_access_policies(), step_diagnostics(), step_multi_threaded(), step_reset_policies(), step_single_threaded(), Meter (+13 more)

### Community 255 - "ADDED Requirements"
Cohesion: 0.09
Nodes (21): ADDED Requirements, Purpose, Requirement: Compile-time sizing and pool allocation, Requirement: Event notification on publish, Requirement: Non-blocking overflow, Requirement: Port diagnostics, Requirement: Subscriber lag telemetry, Requirement: Subscriber receive (+13 more)

### Community 256 - "ADDED Requirements"
Cohesion: 0.09
Nodes (21): ADDED Requirements, Purpose, Requirement: Alive supervision, Requirement: Babbling idiot protection, Requirement: Core isolation, Requirement: Deadline supervision, Requirement: Logical supervision, Requirement: Status aggregation (+13 more)

### Community 257 - "Requirements"
Cohesion: 0.09
Nodes (22): com/publish-subscribe Specification, Purpose, Requirement: Compile-time sizing and pool allocation, Requirement: Event notification on publish, Requirement: Non-blocking overflow, Requirement: Port diagnostics, Requirement: Subscriber lag telemetry, Requirement: Subscriber receive (+14 more)

### Community 258 - "Requirements"
Cohesion: 0.06
Nodes (30): core/result Specification, Purpose, Requirement: Compile-time error code naming, Requirement: Error code catalogue, Requirement: ErrorCode name parsing, Requirement: Hot-path safety of the error model, Requirement: Result value-or-error type, Requirement: Status type for operations with no value (+22 more)

### Community 259 - "Requirements"
Cohesion: 0.09
Nodes (21): phm/supervision Specification, Purpose, Requirement: Alive supervision, Requirement: Babbling idiot protection, Requirement: Core isolation, Requirement: Deadline supervision, Requirement: Logical supervision, Requirement: Status aggregation (+13 more)

### Community 260 - "nexigen-cpp — project analysis"
Cohesion: 0.10
Nodes (21): 0. Refresh snapshot (2026-11-04), 12. P4 completion notes (2026-11-01), 13. P5 completion notes (2026-11-01), 1. Metrics (2026-11-01 audit), 2. Architecture, 3. Tests and coverage, 4. Specifications and process, 5. Findings (+13 more)

### Community 261 - "AppState"
Cohesion: 0.12
Nodes (17): AppState, Exited, Failed, Restarting, Running, Stopped, Unknown, Entry (+9 more)

### Community 262 - "ex_app_cli_settings.cpp"
Cohesion: 0.14
Nodes (24): artifact_present(), CliCounter, CliFilePolicy, CliGauge, CliTracer, count_lines(), count_trace_ids(), fail() (+16 more)

### Community 263 - "CallbackProgression"
Cohesion: 0.17
Nodes (13): CallbackProgression, Continue, Stop, from_json(), from_string(), to_json(), to_string(), Requirement: WaitSet diagnostics (+5 more)

### Community 264 - "Error"
Cohesion: 0.12
Nodes (20): Error, ConnectionFailure, CreationFailed, IncompatibleTypeLayout, InvalidArgument, Ok, OutOfSamples, ServiceAlreadyExists (+12 more)

### Community 265 - "FaultPoint"
Cohesion: 0.10
Nodes (20): FaultPoint, ExecutionManager_fork, ExecutionManager_open_null, ExecutionManager_pipe2, Hub_bind, Hub_epoll, Hub_eventfd, Hub_socket (+12 more)

### Community 266 - "FixedWriter"
Cohesion: 0.15
Nodes (5): FixedWriter, buf_, cap_, hex_digits, len_

### Community 267 - "Requirements"
Cohesion: 0.06
Nodes (34): Purpose, Requirement: Per-span formation stays within its budget, Requirement: Publication never blocks and drops are counted, Requirement: Sequence numbers make gaps observable, Requirement: Sink publishes each span zero-copy from the worker, Requirement: Source hashes are memoized on the worker, Requirement: Source locations cross processes as hashes, Requirement: SpanFrame is a fixed pointer-free wire record (+26 more)

### Community 268 - "CpuSet"
Cohesion: 0.09
Nodes (14): compute_requested_mask(), CpuSet, max_cpu_id, word_count, words_, Entry, name, value (+6 more)

### Community 269 - "ChildProcess"
Cohesion: 0.04
Nodes (53): ChildProcess, artifact_dir, attempt, child_pid, dependencies_pending, dependency_deadline_ns, exit_consumed, instance_dir (+45 more)

### Community 270 - "RecoveryAction"
Cohesion: 0.07
Nodes (28): Entry, name, value, EnumTraits<nexigen::phm::RecoveryAction>, entries, from_json(), from_string(), RecoveryAction (+20 more)

### Community 271 - "Supervisor"
Cohesion: 0.08
Nodes (17): DeadlineClock, started, ProcessSupervision, checkpoints_this_window, consecutive_failures, deadline_clocks, last_checkpoint, last_checkpoint_time (+9 more)

### Community 272 - "ADDED Requirements"
Cohesion: 0.10
Nodes (20): ADDED Requirements, Purpose, Requirement: CTest smoke coverage for every example, Requirement: Documented example invocation, Requirement: Example exit status reports success, Requirement: Examples pass in every supported build mode, Requirement: Optional example build, Requirement: Runnable example catalogue (+12 more)

### Community 273 - "ADDED Requirements"
Cohesion: 0.10
Nodes (20): ADDED Requirements, Purpose, Requirement: Chrono settings carry a sink configuration, Requirement: Invalid sink configuration degrades to best effort, Requirement: Sink configuration is part of chrono settings serialization, Requirement: Sink fields merge with documented precedence, Scenario: Absent local sink field inherits global, Scenario: Absent sink block leaves an unconfigured sink (+12 more)

### Community 274 - "Tasks"
Cohesion: 0.10
Nodes (19): Context, D2: Deterministic malformed-request corpus, D3: Receiver-allowlisted `.value()` guard, D4: Static-analysis nits, Decisions, Design, Goals / Non-Goals, Migration Plan (+11 more)

### Community 275 - "Requirement: Malformed requests are rejected consistently"
Cohesion: 0.10
Nodes (20): ADDED Requirements, Requirement: Control-plane refresh failures are reported, Requirement: Malformed requests are rejected consistently, Requirement: Request headers are matched case-insensitively, Scenario: A failing refresh does not stop serving, Scenario: A similarly named header is not used, Scenario: Header whitespace is tolerated, Scenario: HTTP version is unsupported (+12 more)

### Community 276 - "Requirements"
Cohesion: 0.06
Nodes (33): ADDED Requirements, Requirement: Stamp source diagnostic string, Requirement: Stamp source dump content, Scenario: Calibrated source reports its anchors, Scenario: Uncalibrated source is identifiable, Spec Delta, D1 — Module shape: one STATIC target `nexigen_chrono` (alias `nexigen::chrono`), Capabilities (+25 more)

### Community 277 - "Requirements"
Cohesion: 0.10
Nodes (20): chrono/timestamp Specification, Purpose, Requirement: Hot-path safety of timestamps, Requirement: Monotonic clock sourcing, Requirement: Nanosecond timestamp value type, Requirement: Timestamp arithmetic, Requirement: Timestamp diagnostic string, Requirements (+12 more)

### Community 278 - "Requirements"
Cohesion: 0.10
Nodes (20): diagnostics/scoped-stopwatch Specification, Purpose, Requirement: Budget-overrun check, Requirement: Construction overloads and default-clock fallback, Requirement: Hot-path and layout guarantees, Requirement: RAII scope measurement, Requirement: ScopedStopwatch diagnostic rendering, Requirements (+12 more)

### Community 279 - "Requirements"
Cohesion: 0.10
Nodes (20): execution_manager/instance-lifecycle Specification, Purpose, Requirement: exit.json captures exit metadata, Requirement: Generated settings contain absolute sink paths, Requirement: Instance directory layout is deterministic, Requirement: Lifecycle is cold path only, Requirement: resource.json and exit.json are serializable, Requirements (+12 more)

### Community 280 - "arena_used"
Cohesion: 0.23
Nodes (7): suite_latency(), test_latency(), test_latency_calibration(), test_latency_measured_pass(), test_latency_non_x86(), test_latency_report(), test_latency_setup()

### Community 281 - "StatisticsSnapshot"
Cohesion: 0.03
Nodes (67): StatisticsSnapshot, count, dropped_samples, ignored_samples, max, mean, min, stddev (+59 more)

### Community 282 - "MetricHttpServer"
Cohesion: 0.11
Nodes (8): ExpositionBuffer, buffers, capacity, lengths, published, MetricHttpServer, endpoint_, listen_fd_

### Community 283 - "ProviderKind"
Cohesion: 0.09
Nodes (23): Entry, name, value, EnumTraits<nexigen::chrono::ProviderKind>, entries, from_json(), from_string(), ProviderKind (+15 more)

### Community 284 - "Requirements"
Cohesion: 0.05
Nodes (40): Entry, name, value, EnumTraits<nexigen::tracing::SpanKind>, entries, from_json(), from_string(), SpanKind (+32 more)

### Community 285 - "TraceDumpHeader"
Cohesion: 0.07
Nodes (23): create_directories(), prune_dumps(), TraceDumpHeader, first_start_ns, flags, frame_count, header_size, last_end_ns (+15 more)

### Community 286 - ".is_full"
Cohesion: 0.11
Nodes (17): D3. SPSC ring buffer: monotonically increasing indices, power-of-two mask, one cache line per direction, 1. Build scaffolding, 2. `result.hpp` — error model, 3. `timestamp.hpp` — monotonic nanoseconds, 4. `spsc_ring_buffer.hpp` — layout and single-thread semantics, 5. `spsc_ring_buffer.hpp` — multithreaded contract, 6. Integration verification across build modes, Tasks (+9 more)

### Community 287 - "FunctionGroupRegistry"
Cohesion: 0.12
Nodes (8): FunctionGroupRegistry, groups_, manifest_, GroupRuntime, app_names, state_index, std::formatter<nexigen::exec::FunctionGroupRegistry>, to_string()

### Community 288 - "StateClient"
Cohesion: 0.12
Nodes (11): StateClient, emergency_server_, identity_, last_command_, manager_, registry_, server_, service_name_ (+3 more)

### Community 289 - "cmd_status"
Cohesion: 0.15
Nodes (23): 6. `nexigen-run` CLI report helpers, 3. `nexigen-run` CLI — per-verb option structs and shared target resolution, Impact, Proposal, What Changes, Why, 1. Baseline and guards, 4. nexigen-run CLI (+15 more)

### Community 290 - "NullMetricSink"
Cohesion: 0.12
Nodes (15): from_json(), NullMetricSink, std::formatter<nexigen::metrics::NullMetricSink>, to_json(), to_string(), D13 — Sinks and binding, Requirement: Null metric sink, Scenario: Null sink diagnostics contract (+7 more)

### Community 291 - "state_manager.cpp"
Cohesion: 0.18
Nodes (13): StateManager::find_group_index(), StateManager::find_state(), StateManager::force_state(), StateManager::group_count(), StateManager::group_name(), StateManager::has_state(), StateManager::is_allowed(), StateManager::preparation_timeout_ms() (+5 more)

### Community 292 - "StatePublication"
Cohesion: 0.19
Nodes (4): StatePublication, fields_, std::formatter<nexigen::sm::StatePublication>, to_string()

### Community 293 - "TransitionPreparation"
Cohesion: 0.14
Nodes (7): std::formatter<nexigen::sm::TransitionPreparation>, to_string(), TransitionPreparation, publisher_, service_name_, TransitionPreparationPayload, target_state

### Community 294 - "RotationPolicy"
Cohesion: 0.12
Nodes (18): Entry, name, value, EnumTraits<nexigen::tracing::RotationPolicy>, entries, from_json(), from_string(), RotationPolicy (+10 more)

### Community 295 - "opsx-onboard.md"
Cohesion: 0.10
Nodes (19): Codebase Analysis, Graceful Exit Handling, Guardrails, Phase 10: Archive, Phase 11: Recap & Next Steps, Phase 1: Welcome, Phase 2: Task Selection, Phase 3: Explore Demo (+11 more)

### Community 296 - "openspec-onboard/SKILL.md"
Cohesion: 0.10
Nodes (19): Codebase Analysis, Graceful Exit Handling, Guardrails, Phase 10: Archive, Phase 11: Recap & Next Steps, Phase 1: Welcome, Phase 2: Task Selection, Phase 3: Explore Demo (+11 more)

### Community 297 - "MODIFIED Requirements"
Cohesion: 0.10
Nodes (19): MODIFIED Requirements, Requirement: Consumer drains every active shard, Requirement: Pre-allocated per-thread shard array, Requirement: Sharded drop accounting without contention, Requirement: Slot exhaustion drops instead of blocking, Requirement: Thread-local slot registration, Scenario: All threads' records reach the sink, Scenario: Each shard keeps SPSC wait-free layout (+11 more)

### Community 298 - "Requirements"
Cohesion: 0.07
Nodes (26): core/serialization Specification, Purpose, Requirement: Field-extraction helpers are non-throwing and cold-path only, Requirement: Manifest and config deserialization uses a shared field vocabulary, Requirement: nlohmann_json build integration, Requirement: Non-throwing enum from_json under -fno-exceptions, Requirement: Universal cold-path to_string contract, Requirement: Universal enum std::formatter specialization (+18 more)

### Community 299 - "Requirements"
Cohesion: 0.12
Nodes (15): logging/logger-concept Specification, Purpose, Requirement: Convenience methods carry call-site location, Requirement: Drop reporting moves to snapshots, Requirement: Format arguments reach the sink verbatim, Requirement: Logger concept convenience methods, Requirement: Universal conformance assertion, Requirements (+7 more)

### Community 300 - "logging/latency_test.cpp"
Cohesion: 0.13
Nodes (14): measure_one_pass(), Medians, accepted, filtered, filtered_max, filtered_p999, overhead, place_benchmark_threads() (+6 more)

### Community 301 - "ADDED Requirements"
Cohesion: 0.07
Nodes (27): ADDED Requirements, Purpose, Requirement: Environment overrides are documented and typed, Requirement: Invalid input degrades to best effort, Requirement: Invalid sink configuration degrades to best effort, Requirement: Merge precedence matches the other subsystems, Requirement: Settings loading is cold path only, Requirement: Sink fields merge with documented precedence (+19 more)

### Community 302 - "Session"
Cohesion: 0.07
Nodes (29): configure_realtime(), EmClient, load_manifest(), main(), open_control_channels(), open_state_channels(), open_transport(), Options (+21 more)

### Community 303 - "ADDED Requirements"
Cohesion: 0.07
Nodes (28): ADDED Requirements, Purpose, Requirement: A snapshot is a versioned binary record, Requirement: Daemon drains spans through a blocking wait set, Requirement: Lifecycle and steady-state I/O, Requirement: Snapshot retention is bounded, Requirement: The ring overwrites the oldest frames, Requirement: Trace daemon configuration (+20 more)

### Community 304 - "TelemetryPacket"
Cohesion: 0.07
Nodes (28): await_first_full(), EndpointStats, last_error, retries, main(), print_line(), report(), run_consumer() (+20 more)

### Community 305 - "RecoveryNotification"
Cohesion: 0.08
Nodes (22): Configuration, Deferred, Running, Snapshot format, Trace Daemon (`nexigen-traced`), Trigger and shutdown, RecoveryNotification, function_group (+14 more)

### Community 306 - "enum_from_json"
Cohesion: 0.13
Nodes (16): enum_from_json(), enum_to_json(), read_enum(), Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal (+8 more)

### Community 307 - "unique_path"
Cohesion: 0.22
Nodes (14): test_app_settings_full_coverage(), test_settings_loaders(), test_settings_sink(), EnvGuard, had, name, saved, make_dir() (+6 more)

### Community 308 - "ConsoleSink"
Cohesion: 0.16
Nodes (6): ConsoleSink, len_, line_, line_capacity, msg_, msg_capacity

### Community 309 - "ShardPoolState"
Cohesion: 0.11
Nodes (15): ExhaustedCell, pad_, slot_exhausted, RegistrationCell, active, next_slot, pad_, ShardPool<Record, ShardCapacity, MaxThreads>::buffer_full_total() (+7 more)

### Community 310 - "DescriptiveStatistics"
Cohesion: 0.13
Nodes (12): DescriptiveStatistics, access, cell_, concurrency, dropped_, identity_, ignored_, kind (+4 more)

### Community 311 - "JsonFileMetricSink"
Cohesion: 0.15
Nodes (9): JsonFileMetricSink, app_, app_len_, discarded_batches_, fd_, scratch_, stamp_text_, std::formatter<nexigen::metrics::JsonFileMetricSink> (+1 more)

### Community 312 - ".on_batch"
Cohesion: 0.11
Nodes (14): 1. Add the unit to the metric identity, 2. Thread the unit through creation, 3. Serialize and render the unit, 4. Verify, Tasks, Workflow follow-up, Context, D1 — Serialize State records in the sink (+6 more)

### Community 313 - "Decisions"
Cohesion: 0.10
Nodes (19): D10. Clock calibration for UTC JSON timestamps, D11. Directory rotation on the consumer thread, D12. JSON line formatting — exact schema, escaping, no allocation, D13. ConsoleLogger sink details, D14. FileLogger configuration surface, D15. Affinity: `pthread_setaffinity_np` in `affinity.cpp`, D16. CompositeLogger, D17. CMake and test wiring (+11 more)

### Community 314 - "ADDED Requirements"
Cohesion: 0.11
Nodes (18): ADDED Requirements, Purpose, Requirement: Compile-time error code naming, Requirement: Error code catalogue, Requirement: Hot-path safety of the error model, Requirement: Result value-or-error type, Requirement: Status type for operations with no value, Scenario: Constructing and reading a failure (+10 more)

### Community 315 - "ADDED Requirements"
Cohesion: 0.11
Nodes (18): ADDED Requirements, Purpose, Requirement: nlohmann_json build integration, Requirement: Non-throwing enum from_json under -fno-exceptions, Requirement: Universal cold-path to_string contract, Requirement: Universal enum std::formatter specialization, Requirement: Universal enum string and JSON vocabulary, Scenario: Configure succeeds without a system package (+10 more)

### Community 316 - "ADDED Requirements"
Cohesion: 0.11
Nodes (18): ADDED Requirements, Purpose, Requirement: Consumer drains every active shard, Requirement: Pre-allocated per-thread shard array, Requirement: Sharded drop accounting without contention, Requirement: Slot exhaustion drops instead of blocking, Requirement: Thread-local slot registration, Scenario: All threads' records reach the sink (+10 more)

### Community 317 - "ADDED Requirements"
Cohesion: 0.11
Nodes (18): ADDED Requirements, Purpose, Requirement: Compile-time access gating, Requirement: Instrument conformance is pinned, Requirement: Metric concept core surface, Requirement: Readable metric concept, Scenario: Conformance assertions compile, Scenario: Conforming instrument satisfies the concept (+10 more)

### Community 318 - "Design"
Cohesion: 0.08
Nodes (23): Context, D1 — Use `Transfer-Encoding: chunked` and frame every SSE write, Decisions, Design, Goals / Non-Goals, Migration Plan, Open Questions, Risks / Trade-offs (+15 more)

### Community 319 - "Requirement: Suite manifest is validated before launch"
Cohesion: 0.11
Nodes (18): MODIFIED Requirements, Requirement: Manifest launch settings reach each child, Requirement: Suite manifest is validated before launch, Scenario: Arguments are passed exactly, Scenario: CPU above 63 is not rejected merely for its width, Scenario: CPU affinity is restricted, Scenario: CPU index outside the online set is rejected, Scenario: Duplicate application identity is rejected (+10 more)

### Community 320 - "Requirements"
Cohesion: 0.06
Nodes (34): chrono/chrono-settings Specification, Purpose, Requirement: Environment overrides are documented and typed, Requirement: Invalid input degrades to best effort, Requirement: Invalid sink configuration degrades to best effort, Requirement: Manual kind records ignored worker fields, Requirement: Merge precedence is deterministic, Requirement: Settings loading is cold path only (+26 more)

### Community 321 - "Requirement: Readable metric concept"
Cohesion: 0.11
Nodes (18): metrics/metric-concepts Specification, Purpose, Requirement: Compile-time access gating, Requirement: Instrument conformance is pinned, Requirement: Metric concept core surface, Requirement: Readable metric concept, Requirements, Scenario: Conformance assertions compile (+10 more)

### Community 322 - "Requirements"
Cohesion: 0.09
Nodes (21): metrics/snapshot-format Specification, Purpose, Requirement: All deserialization is exception-free under `JSON_NOEXCEPTION=1`, Requirement: Complexity of decomposed functions stays within the project target, Requirement: Identity and scalar snapshots round-trip safely, Requirement: MetricBatchSnapshot deserialization is array-length safe, Requirement: MetricRecord deserialization selects the union member by kind, Requirement: Serialization helpers do not change hot-path contracts (+13 more)

### Community 323 - "file_logger_config.cpp"
Cohesion: 0.24
Nodes (11): apply_to(), from_json(), to_json(), try_read_bool_field(), try_read_log_level_field(), try_read_rotation_field(), try_read_rotation_kind_field(), try_read_string_field() (+3 more)

### Community 324 - "LogLevel"
Cohesion: 0.03
Nodes (47): from_json(), LogLevel, Debug, Error, Fatal, Info, Off, Verbose (+39 more)

### Community 325 - "JsonFileMetricSinkConfig"
Cohesion: 0.12
Nodes (16): JsonFileMetricSinkConfig, app_name, append, path, std::formatter<nexigen::metrics::JsonFileMetricSinkConfig>, to_string(), Requirement: Metrics settings carry a sink member, Scenario: Absent sink block leaves an unconfigured sink (+8 more)

### Community 326 - "execution_manager_logging_test.cpp"
Cohesion: 0.25
Nodes (16): contains(), cpu_is_set(), first_allowed_cpu(), main(), read_text(), remove_tree(), run_suite(), test_affinity_and_logger_mask() (+8 more)

### Community 327 - "TraceContext"
Cohesion: 0.08
Nodes (31): std::formatter<nexigen::tracing::TraceContext>, to_string(), TraceContext, source_span_id, trace_id, ADDED Requirements, Purpose, Requirement: NEW_ROOT starts a fresh trace (+23 more)

### Community 328 - ".now"
Cohesion: 0.10
Nodes (16): D1. `Result`/`Status` are thin aliases over `std::expected`, not a bespoke type, D2. `Timestamp` is a 8-byte value type; `now()` returns `Result<Timestamp>`, D4. Zero-dependency single-file test harness, D5. CMake shape: one INTERFACE library per namespace module plus a `tests/` subdir, Decisions, Impact, Requirement: Sampled from the nexigen chrono clock domain, Scenario: Default construction uses the raw monotonic clock (+8 more)

### Community 329 - "ADDED Requirements"
Cohesion: 0.11
Nodes (17): ADDED Requirements, Purpose, Requirement: Compile-time level naming, Requirement: Runtime level control, Requirement: Severity catalogue, Requirement: Sub-threshold calls short-circuit, Scenario: Enumerators are distinct and severity-ordered, Scenario: Filtered call performs no logging work (+9 more)

### Community 330 - "ADDED Requirements"
Cohesion: 0.11
Nodes (17): ADDED Requirements, Purpose, Requirement: Cached thread identity, Requirement: Hot-path latency budget, Requirement: Monotonic nanosecond stamping, Requirement: Wait-free producer enqueue, Requirement: Zero runtime allocation in record handling, Scenario: Budget excludes background work (+9 more)

### Community 331 - "Requirement: Logger concept core surface"
Cohesion: 0.11
Nodes (17): ADDED Requirements, MODIFIED Requirements, Requirement: Convenience methods carry call-site location, Requirement: Drop reporting moves to snapshots, Requirement: Format arguments reach the sink verbatim, Requirement: Logger concept convenience methods, Requirement: Logger concept core surface, Scenario: Argument packs are accepted (+9 more)

### Community 332 - "ADDED Requirements"
Cohesion: 0.11
Nodes (17): ADDED Requirements, Purpose, Requirement: Hot-path safety of timestamps, Requirement: Monotonic clock sourcing, Requirement: Nanosecond timestamp value type, Requirement: Timestamp arithmetic, Scenario: Clock discipline, Scenario: Clock failure is non-fatal (+9 more)

### Community 333 - "5. Oversized tests — split into focused sub-functions"
Cohesion: 0.22
Nodes (21): 5. Oversized tests — split into focused sub-functions, exp_hist_containment(), exp_hist_empty(), exp_hist_fallback(), exp_hist_log_uniform(), exp_hist_sharded(), exp_hist_windowed(), test_exponential_histogram_mt() (+13 more)

### Community 334 - "Decisions"
Cohesion: 0.11
Nodes (17): Context, D10. Build and tests, D1. Process and loop model, D2. Module layout, D3. Run keys, tree model, and stable identities, D4. Initial scan and inotify, D5. Tailers and rings, D6. Subscribers and replay (+9 more)

### Community 335 - "Decisions"
Cohesion: 0.11
Nodes (17): Context, D10. Concurrency and memory model, D11. Migration and rollback, D1. Manifest schema (v1), D2. Directory layout and metadata, D3. Process model: suite supervisor over per-instance supervisors, D4. Start handshake, D5. Restart policy and state machine (+9 more)

### Community 336 - "ADDED Requirements"
Cohesion: 0.11
Nodes (17): ADDED Requirements, Purpose, Requirement: Attachment lifecycle, Requirement: Callback dispatch and source identity, Requirement: Deadline attachment, Requirement: Event notification attachment, Requirement: Interval attachment, Requirement: Multiplexed blocking wait (+9 more)

### Community 337 - "ADDED Requirements"
Cohesion: 0.11
Nodes (17): ADDED Requirements, Purpose, Requirement: Managed startup, Requirement: Memory locking, Requirement: Scheduler priorities, Requirement: Self-health supervision, Requirement: Thread affinity, Requirement: Transition watchdog (+9 more)

### Community 338 - "Requirements"
Cohesion: 0.11
Nodes (17): com/waitset Specification, Purpose, Requirement: Attachment lifecycle, Requirement: Callback dispatch and source identity, Requirement: Deadline attachment, Requirement: Event notification attachment, Requirement: Interval attachment, Requirement: Multiplexed blocking wait (+9 more)

### Community 339 - "Requirements"
Cohesion: 0.11
Nodes (17): diagnostics/examples Specification, Purpose, Requirement: ex_stopwatch demonstrates loop tick budgeting, Requirement: ex_stopwatch_histogram is buildable, runnable, and a CTest smoke test, Requirement: ex_stopwatch_histogram records measured scopes into a histogram, Requirement: ex_stopwatch_histogram renders the histogram on the console, Requirement: ex_stopwatch is buildable, runnable, and a CTest smoke test, Requirements (+9 more)

### Community 340 - "Requirements"
Cohesion: 0.11
Nodes (17): Purpose, Requirement: Managed startup, Requirement: Memory locking, Requirement: Scheduler priorities, Requirement: Self-health supervision, Requirement: Thread affinity, Requirement: Transition watchdog, Requirement: WaitSet event loop (+9 more)

### Community 341 - "function_group_registry.cpp"
Cohesion: 0.19
Nodes (10): FunctionGroupRegistry::bound_apps(), FunctionGroupRegistry::command_of(), FunctionGroupRegistry::find_group(), FunctionGroupRegistry::FunctionGroupRegistry(), FunctionGroupRegistry::group_count(), FunctionGroupRegistry::group_name(), FunctionGroupRegistry::group_state(), FunctionGroupRegistry::is_off_target() (+2 more)

### Community 342 - "RotationSchedule"
Cohesion: 0.09
Nodes (21): Entry, name, value, EnumTraits<nexigen::logging::RotationSchedule>, entries, from_json(), from_string(), RotationSchedule (+13 more)

### Community 343 - "supervisor.cpp"
Cohesion: 0.20
Nodes (9): Supervisor::is_allowed_transition(), Supervisor::on_alive_tick(), Supervisor::on_checkpoint(), Supervisor::process(), Supervisor::process_count(), Supervisor::record_failure(), Supervisor::reset_failures(), Supervisor::status() (+1 more)

### Community 344 - "iceoryx2_metric_sink_test.cpp"
Cohesion: 0.15
Nodes (14): main(), run_suite(), test_burst_no_drop(), test_concept_and_diagnostics(), test_out_of_samples_no_block(), test_publish_roundtrip(), test_sink(), load_config() (+6 more)

### Community 345 - "MODIFIED Requirements"
Cohesion: 0.12
Nodes (16): ADDED Requirements, MODIFIED Requirements, Requirement: Composite logger diagnostic string, Requirement: Level control propagation, Requirement: Sub-logger access by index and by type, Requirement: Virtual-free fan-out composition, Scenario: Composite of composites works, Scenario: Composite short-circuit matches its own level (+8 more)

### Community 346 - "ADDED Requirements"
Cohesion: 0.12
Nodes (16): ADDED Requirements, MODIFIED Requirements, Requirement: LogLevel JSON serialization, Requirement: LogLevel name parsing, Requirement: std::formatter for LogLevel, Requirement: Sub-threshold calls short-circuit, Scenario: Every level name round-trips with to_string, Scenario: Filtered call performs no logging work (+8 more)

### Community 347 - "ADDED Requirements"
Cohesion: 0.12
Nodes (16): ADDED Requirements, MODIFIED Requirements, Requirement: Consumer-side rendering of packed records, Requirement: Deferred type-erased argument formatting, Requirement: Hot-path latency budget, Requirement: LogRecord diagnostic string, Requirement: Wait-free producer enqueue, Scenario: Budget excludes background work (+8 more)

### Community 348 - "ADDED Requirements"
Cohesion: 0.12
Nodes (16): ADDED Requirements, Purpose, Requirement: Calibrated monotonic stamp source, Requirement: Monotonic-to-UTC conversion, Requirement: Stamp source calibration contract, Requirement: UTC offset snapshot calibration, Scenario: Calibration failure is reported, not fatal, Scenario: Clock read failure during sampling (+8 more)

### Community 349 - "ADDED Requirements"
Cohesion: 0.10
Nodes (20): ADDED Requirements, Purpose, Requirement: Budget-overrun check, Requirement: Construction overloads and default-clock fallback, Requirement: Hot-path and layout guarantees, Requirement: RAII scope measurement, Requirement: ScopedStopwatch diagnostic rendering, Scenario: Default budget participates when configured (+12 more)

### Community 350 - "Requirements"
Cohesion: 0.12
Nodes (15): metrics/iceoryx2-metric-sink Specification, Purpose, Requirement: Batches are published as frames, Requirement: Iceoryx2 metric sink satisfies the sink concept, Requirement: Sink diagnostics, Requirement: Sink flush is a no-op, Requirements, Scenario: Every record becomes a frame (+7 more)

### Community 351 - "Decisions"
Cohesion: 0.12
Nodes (16): Context, D1. File layout and fixed names, D2. Preparation order and cleanup safety, D3. Generated settings contents, D4. Command-line composition, D5. Workload and bounded runtime, D6. Stateless file policy, D7. Runner and cleanup scripts (+8 more)

### Community 352 - "Decisions"
Cohesion: 0.12
Nodes (16): Context, D10. Migration and rollback, D1. Free-function logging facade, no new public type, D2. Path and level resolution, D3. Global logging settings, D4. Runner thread affinity, D5. CLI command ordering, D6. Suite supervisor integration and fork suspension (+8 more)

### Community 353 - "Requirements"
Cohesion: 0.12
Nodes (16): hub/http-api — internal structure (delta), Purpose, Requirement: Client read logic is layered, Requirement: Complexity of decomposed functions stays within the project target, Requirement: `HttpServer::Impl::start` is split into setup phases, Requirement: Refactored internals preserve all observable behavior, Requirement: The server run loop uses an explicit state stepper, Requirements (+8 more)

### Community 354 - "Decisions"
Cohesion: 0.13
Nodes (14): Context, D1. Module structure: `nexigen::runner` library + `nexigen-run` binary, D2b. Detached supervisor, D3. Settings generation reuses `app/telemetry-layout` resolution, D4. resource.json and exit.json are plain structs with the serialisation trio, D5. `Runner::stop` uses SIGTERM → grace → SIGKILL, D6. `Runner::status` checks /proc, D7. Cold-path and concurrency impact (+6 more)

### Community 355 - "Header compilation audit — compiled-module JSON pairs"
Cohesion: 0.18
Nodes (8): Audit command, Follow-up candidates (not in this change), Header compilation audit — compiled-module JSON pairs, Result, Rule, Verification for future moves, What this change moved, D4 — Placement rule for P2, with an explicit exemption list

### Community 356 - "ADDED Requirements"
Cohesion: 0.12
Nodes (16): ADDED Requirements, Purpose, Requirement: Asynchronous pending responses, Requirement: Correlation of requests, Requirement: Method port diagnostics, Requirement: Method zero-copy and non-POD rejection, Requirement: Synchronous call with timeout, Requirement: Typed client and server (+8 more)

### Community 357 - "Requirement: App settings aggregate every subsystem"
Cohesion: 0.12
Nodes (16): app/settings Specification, Purpose, Requirement: App settings aggregate every subsystem, Requirement: App settings are serializable, Requirement: Application name is app-level, Requirement: Settings loading is cold path only, Requirements, Scenario: App name is set on the aggregate (+8 more)

### Community 358 - "Requirements"
Cohesion: 0.12
Nodes (16): com/request-response Specification, Purpose, Requirement: Asynchronous pending responses, Requirement: Correlation of requests, Requirement: Method port diagnostics, Requirement: Method zero-copy and non-POD rejection, Requirement: Synchronous call with timeout, Requirement: Typed client and server (+8 more)

### Community 359 - "Requirements"
Cohesion: 0.12
Nodes (16): diagnostics/stopwatch Specification, Purpose, Requirement: Hot path performs zero allocation and meets latency budgets, Requirement: Stopwatch diagnostic rendering, Requirement: Stopwatch state machine with accumulated intervals, Requirements, Scenario: Fresh stopwatch is idle with zero elapsed, Scenario: Hot operations never allocate (+8 more)

### Community 360 - "SpacingSink"
Cohesion: 0.15
Nodes (10): Sample, affinity_ok, ns, SpacingSink, affinity_ok_, batch_status_, expected_cpu_, flush_status_ (+2 more)

### Community 361 - ".emit"
Cohesion: 0.23
Nodes (9): arg_offsets(), arg_total(), pack_one(), render_impl(), render_text(), render_tuple(), slot_size(), string_slot_size() (+1 more)

### Community 362 - "ADDED Requirements"
Cohesion: 0.12
Nodes (15): ADDED Requirements, Purpose, Requirement: Batches are published as frames, Requirement: Iceoryx2 metric sink satisfies the sink concept, Requirement: Sink diagnostics, Requirement: Sink flush is a no-op, Scenario: Every record becomes a frame, Scenario: Flush succeeds (+7 more)

### Community 363 - "ADDED Requirements"
Cohesion: 0.12
Nodes (15): ADDED Requirements, Purpose, Requirement: Hot-path safety of timestamps, Requirement: Monotonic clock sourcing, Requirement: Nanosecond timestamp value type, Requirement: Timestamp arithmetic, Scenario: Clock discipline, Scenario: Clock failure is non-fatal (+7 more)

### Community 364 - "Tasks"
Cohesion: 0.07
Nodes (29): FileRead, Absent, Failed, Ok, fsync_fd(), open_output(), write_all(), combine() (+21 more)

### Community 365 - "Requirement: Example runner and cleanup scripts"
Cohesion: 0.12
Nodes (15): ADDED Requirements, MODIFIED Requirements, Requirement: Documented example invocation, Requirement: Example runner and cleanup scripts, Requirement: Runnable example catalogue, Scenario: Cleanup is idempotent, Scenario: Cleanup removes generated directories, Scenario: Example executables exist after a default build (+7 more)

### Community 366 - "ADDED Requirements"
Cohesion: 0.12
Nodes (15): ADDED Requirements, Purpose, Requirement: Monotonic series key, Requirement: Signed UTC offset semantics, Requirement: Snapshot serialization, Requirement: Snapshot value type, Scenario: Copy preserves the value, Scenario: Negative offset is preserved (+7 more)

### Community 367 - "Requirement: Environment overrides are documented and typed"
Cohesion: 0.12
Nodes (15): ADDED Requirements, MODIFIED Requirements, Requirement: Environment overrides are documented and typed, Requirement: Explicit compile-time capacity limits are checked, Requirement: Message capacity explicit presence is tracked, Scenario: Affinity override accepts a core list, Scenario: Default message capacity is not flagged, Scenario: Excess explicit message capacity is rejected (+7 more)

### Community 368 - "Requirement: Long and short option forms are supported"
Cohesion: 0.12
Nodes (15): ADDED Requirements, MODIFIED Requirements, Requirement: App name option, Requirement: App name round-trips through serialization, Requirement: Long and short option forms are supported, Scenario: App name accepts all value forms, Scenario: App name is parsed, Scenario: App name survives a JSON round-trip (+7 more)

### Community 369 - "Requirement: Identity validation rejects unsafe values"
Cohesion: 0.12
Nodes (15): MODIFIED Requirements, Requirement: Identity validation rejects unsafe values, Requirement: Instance directory is derived when not supplied, Requirement: Run identity is read from a fixed environment contract, Scenario: Complete environment populates every field, Scenario: Derived instance directory, Scenario: Empty identity is valid, Scenario: Explicit instance directory wins (+7 more)

### Community 370 - "SpanContext"
Cohesion: 0.05
Nodes (41): SpanContext, sampled, span_id, trace_id, ADDED Requirements, Purpose, Requirement: Automatic intra-thread parent tracking, Requirement: Propagable span context (+33 more)

### Community 371 - "metric_provider_test.cpp"
Cohesion: 0.15
Nodes (13): suite_get_meter_harvest_race(), suite_metrics_worker_thread_settings(), suite_provider_config(), suite_provider_harvester(), suite_reset_window_race(), test_metrics_worker_thread_settings(), test_provider_harvester(), test_provider_harvester_cpus() (+5 more)

### Community 372 - "Requirements"
Cohesion: 0.12
Nodes (15): chrono/chrono-snapshot Specification, Purpose, Requirement: Monotonic series key, Requirement: Signed UTC offset semantics, Requirement: Snapshot serialization, Requirement: Snapshot value type, Requirements, Scenario: Copy preserves the value (+7 more)

### Community 373 - "Requirements"
Cohesion: 0.11
Nodes (18): core/log-level Specification, Purpose, Requirement: Compile-time level naming, Requirement: LogLevel JSON serialization, Requirement: LogLevel name parsing, Requirement: Severity catalogue, Requirement: std::formatter for LogLevel, Requirements (+10 more)

### Community 374 - "Requirements"
Cohesion: 0.12
Nodes (15): metrics/metric-sinks Specification, Purpose, Requirement: Console metric sink, Requirement: Metric batches carry the application name, Requirement: Metric sink concept, Requirements, Scenario: Batch is rendered and written, Scenario: Batch line carries the app field (+7 more)

### Community 375 - "Stopwatch"
Cohesion: 0.11
Nodes (22): std::formatter<nexigen::diagnostics::Stopwatch>, Stopwatch, accumulated_ns_, anchor_, lap_anchor_, no_lap_value, to_string(), Context (+14 more)

### Community 376 - "logging_test_support.hpp"
Cohesion: 0.04
Nodes (28): fail(), main(), write_file(), Edge, child, parent, find_span(), main() (+20 more)

### Community 377 - ".emplace_logger"
Cohesion: 0.24
Nodes (3): fail(), main(), SimplePolicy

### Community 378 - "2. Production deserializers (CCN ≤ 15, cold-path in `src/`)"
Cohesion: 0.24
Nodes (9): D3 — Decomposition shape: flat `from_json` + per-type sub-parsers in the defining unit, 2. Production deserializers (CCN ≤ 15, cold-path in `src/`), from_json(), parse_provided_service(), parse_required_service(), ProvidedService::to_string(), RequiredService::to_string(), ServiceManifest::to_string() (+1 more)

### Community 379 - "CommandLineOptions"
Cohesion: 0.11
Nodes (18): CommandLineError, code, diagnostics, to_string(), CommandLineOptions, app_name, global_settings_dir, help (+10 more)

### Community 380 - "TraceDaemonConfig"
Cohesion: 0.09
Nodes (17): std::formatter<nexigen::com::Publisher<T, H>>, std::formatter<nexigen::tracing::TraceDaemonConfig>, to_string(), TraceDaemonConfig, app_name, bg_cpu_affinity, drain_interval_ms, dump_directory (+9 more)

### Community 381 - "Decisions"
Cohesion: 0.08
Nodes (24): ChronoLoggerBridge, logger, LogBinding, self, SinkBinding, self, Context, D1 — Granularity rule (unchanged from the logging exemplar) (+16 more)

### Community 382 - "WaitSetGuard"
Cohesion: 0.08
Nodes (13): std::formatter<nexigen::com::WaitSetAttachmentId>, to_string(), WaitSetAttachmentId, inner_, std::formatter<nexigen::com::WaitSetGuard>, to_string(), WaitSetGuard, inner_ (+5 more)

### Community 383 - "Decisions"
Cohesion: 0.11
Nodes (17): D10 — Chrono worker logger start guard: reject with `Conflict`, D1 — Identity anchor is `proc_start_ticks`, recorded at spawn, compared from `/proc`, D2 — `waitpid` outcome model: `status_valid`, D3 — Failed starts reap the supervisor: bounded blocking wait, D4 — Env values are NUL-screened on the CLI path, D5 — Spam config: per-field atomics (honest "race-free", not "wait-free-linearized"), D6 — Delete `SpscShardRing`/`SpscShardPool` headers and their specs, D7 — `claim_slot()` TLS cache 4→8 with no-burn overflow (+9 more)

### Community 384 - "CompositeMetricSink"
Cohesion: 0.16
Nodes (4): CompositeMetricSink, std::formatter<nexigen::metrics::CompositeMetricSink<Sinks...>>, to_json(), to_string()

### Community 385 - "ThrottleConfig"
Cohesion: 0.11
Nodes (17): ThrottleConfig, enabled, max_records_per_window, window_ns, to_string(), Context, D2 — Target file tree (~33 headers), D3 — Promote vocabulary types out of `detail`, delete aliases (+9 more)

### Community 386 - "ADDED Requirements"
Cohesion: 0.13
Nodes (14): ADDED Requirements, Purpose, Requirement: Level control propagation, Requirement: Sub-logger access by index and by type, Requirement: Virtual-free fan-out composition, Scenario: Composite of composites works, Scenario: Composite short-circuit matches its own level, Scenario: Empty composite is valid (+6 more)

### Community 387 - "ADDED Requirements"
Cohesion: 0.13
Nodes (14): ADDED Requirements, Purpose, Requirement: Convenience methods carry call-site location, Requirement: Logger concept convenience methods, Requirement: Logger concept core surface, Requirement: Universal conformance assertion, Scenario: Conforming type satisfies the concept, Scenario: Convenience set is part of the contract (+6 more)

### Community 388 - "Requirement: Runnable command-line settings example"
Cohesion: 0.13
Nodes (14): MODIFIED Requirements, Requirement: Runnable command-line settings example, Scenario: All four artifact kinds are written, Scenario: Command-line options drive initialization, Scenario: Existing output directory is cleared, Scenario: Failure demo exercises a real failure path, Scenario: Failures are reported, Scenario: Help prints usage without initializing (+6 more)

### Community 389 - "ADDED Requirements"
Cohesion: 0.04
Nodes (44): ADDED Requirements, Purpose, Requirement: Counter reset windows, Requirement: Counter serialization contract, Requirement: Gauge recording surface, Requirement: Gauge reset windows, Requirement: Gauge serialization contract, Scenario: Concurrent window records land once (+36 more)

### Community 390 - "ADDED Requirements"
Cohesion: 0.12
Nodes (16): ADDED Requirements, Purpose, Requirement: Hot path performs zero allocation and meets latency budgets, Requirement: Stopwatch diagnostic rendering, Requirement: Stopwatch state machine with accumulated intervals, Scenario: Fresh stopwatch is idle with zero elapsed, Scenario: Hot operations never allocate, Scenario: Layout stays within one cache line (+8 more)

### Community 391 - "Requirement: Metrics render structured by kind"
Cohesion: 0.13
Nodes (14): ADDED Requirements, MODIFIED Requirements, Requirement: Metrics render structured by kind, Requirement: Stream viewer supports replay and live modes, Scenario: Counter and gauge render as name and value, Scenario: Descriptive statistics render their summary, Scenario: Histogram renders its summary, Scenario: Malformed batch falls back to raw text (+6 more)

### Community 392 - "Requirement: Runnable minimal infrastructure example"
Cohesion: 0.13
Nodes (14): MODIFIED Requirements, Requirement: Command-line options initialize the infrastructure, Requirement: Initialization follows a fixed order, Requirement: Runnable minimal infrastructure example, Scenario: Example exercises logging, metrics and tracing, Scenario: Example reports failures, Scenario: Example stops and flushes, Scenario: Example uses a direct main flow (+6 more)

### Community 393 - "Requirement: Drop summaries are synthesized by the worker"
Cohesion: 0.13
Nodes (14): ADDED Requirements, logging/multi-producer delta spec, MODIFIED Requirements, Requirement: Drop summaries are synthesized by the worker, Requirement: Reported drop cells reset after synthesis, Requirement: Sharded drop accounting without contention, Scenario: Concurrent bump during consumer reset loses no increments, Scenario: Drops accumulating after a summary reach the next summary (+6 more)

### Community 394 - "nexigen-phmd/main.cpp"
Cohesion: 0.11
Nodes (22): attach_alive_tick(), attach_processes(), configure_realtime(), load_manifest(), main(), open_recovery(), open_transport(), Options (+14 more)

### Community 395 - "Design"
Cohesion: 0.13
Nodes (14): Context, D1 — `.MandateToString<T>` concept + ONE generic formatter, D2 — ADL-reached JSON pairs: the empirical boundary, D2 — Enum JSON pair from the mandate header, D3 — CRTP base removed in favor of a constrained generic free, D3 — CRTP `MandateString<T>` base, D4 — Concept correction discovered during the sweep, D4 — Deliberate overload ambiguity and std-type risk (+6 more)

### Community 396 - "Requirement: CPU-pinned background consumer"
Cohesion: 0.13
Nodes (14): MODIFIED Requirements, Requirement: CPU-pinned background consumer, Requirement: Provider serialization surfaces, Scenario: CPU above 63 is supported, Scenario: Handles and snapshots render diagnostics, Scenario: Idle consumer wake-ups are bounded by the ceiling, Scenario: Live provider objects are not deserializable, Scenario: Per-shard FIFO and fair drain hold (+6 more)

### Community 397 - "Requirement: Error code catalogue"
Cohesion: 0.13
Nodes (14): MODIFIED Requirements, Requirement: Compile-time error code naming, Requirement: Error code catalogue, Scenario: Conflict formats by name, Scenario: Conflict has stable value and naming, Scenario: Conflict is distinguishable from NotInitialized, Scenario: Enumerators are available and distinct, Scenario: Name of a known code is resolvable at compile time (+6 more)

### Community 398 - "Requirement: Fixed-width ISO-8601 nanosecond formatting"
Cohesion: 0.13
Nodes (14): chrono/time-format Specification, Purpose, Requirement: Civil time diagnostic string, Requirement: Fixed-width ISO-8601 nanosecond formatting, Requirement: UTC civil-time decomposition, Requirements, Scenario: Byte-exact rendering, Scenario: Decomposition renders fully (+6 more)

### Community 399 - "Requirements"
Cohesion: 0.13
Nodes (14): packaging Specification, Purpose, Requirement: Command-line tools install when built, Requirement: Exported CMake package, Requirement: Installable header and library tree, Requirement: Transitive dependency discovery, Requirements, Scenario: Archives are installed (+6 more)

### Community 400 - "Iceoryx2TraceSink"
Cohesion: 0.08
Nodes (13): HashCacheEntry, hash, text, Iceoryx2TraceSink, frame_, hash_cache_, hash_cache_capacity, node_ (+5 more)

### Community 401 - "Fixture"
Cohesion: 0.13
Nodes (21): test_http_server_failure_report(), test_http_server_header_case(), test_http_server_query_escape(), test_http_server_routes(), test_http_start_errors(), capture_diagnostic(), diagnostic_contains(), DiagnosticCapture (+13 more)

### Community 402 - "StopArgs"
Cohesion: 0.10
Nodes (20): 1. Execution-manager CLI renames, 2. Hub CLI rename, 3. Tracing test renames and result-value finding, 4. Verification, Tasks, Workflow follow-up, scan_start_value(), StartArgs (+12 more)

### Community 403 - "name"
Cohesion: 0.36
Nodes (11): steps, steps, steps, steps, steps, steps, name, run (+3 more)

### Community 404 - "CivilTime"
Cohesion: 0.11
Nodes (14): CivilTime, day, hour, minute, month, nanosecond, second, year (+6 more)

### Community 405 - "state_client.cpp"
Cohesion: 0.11
Nodes (12): StateCommand, function_group, target_state, std::formatter<nexigen::exec::StateCommand>, to_string(), StateClient::create(), StateClient::execute(), StateClient::instance_dir_for() (+4 more)

### Community 406 - "TransitionRecord"
Cohesion: 0.12
Nodes (11): std::formatter<nexigen::sm::TransitionRecord>, to_string(), TransitionRecord, function_group, new_state, old_state, requester_id, result (+3 more)

### Community 407 - "Decisions"
Cohesion: 0.11
Nodes (17): Context, D10 — Migration and tests, D1 — Composition replaces inheritance, D2 — Stateless policy contract, D3 — `Infrastructure` API and member order, D4 — Heap-only factory, D5 — Process-global signal ownership guard, D6 — Initialization order and rollback (+9 more)

### Community 408 - "Requirement: Asynchronous wait-free console output"
Cohesion: 0.14
Nodes (13): ADDED Requirements, MODIFIED Requirements, REMOVED Requirements, Requirement: Asynchronous wait-free console output, Requirement: Bounded single-producer contract, Requirement: Console line content, Requirement: Console logger diagnostic strings, Scenario: Config and logger render (+5 more)

### Community 409 - "ThreadApplyMode"
Cohesion: 0.11
Nodes (16): `ThreadSpec`, ThreadApplyMode, BestEffort, Required, Requirement: `apply_thread_spec_self` delegates to single-attribute applicators, Scenario: Affinity application is isolated, Scenario: Name application is isolated, Scenario: Scheduler application is isolated (+8 more)

### Community 410 - "ADDED Requirements"
Cohesion: 0.14
Nodes (13): ADDED Requirements, Purpose, Requirement: Nested and cross-thread spans example, Requirement: Perfetto Chrome trace example, Requirement: Tracing examples pass in every build mode, Scenario: Full build-mode matrix is green, Scenario: The example guides the user, Scenario: The example is self-contained (+5 more)

### Community 411 - "Requirement: Base clock-domain identifiers"
Cohesion: 0.14
Nodes (13): ADDED Requirements, Purpose, Requirement: Base clock-domain identifiers, Requirement: Compile-time domain declaration, Scenario: Declaration is a constant expression, Scenario: Enum enumerates the base domains, Scenario: Formatter matches to_string, Scenario: JSON round-trip under no exceptions (+5 more)

### Community 412 - "Requirement: Size-based file rotation"
Cohesion: 0.14
Nodes (13): ADDED Requirements, MODIFIED Requirements, Requirement: File logger vocabulary diagnostic strings, Requirement: Size-based file rotation, Scenario: Config dump is complete, Scenario: Drop snapshot renders, Scenario: Flush covers the active segment, Scenario: Limit reached starts the next segment (+5 more)

### Community 413 - "Requirement: App settings are serializable"
Cohesion: 0.14
Nodes (13): ADDED Requirements, MODIFIED Requirements, Requirement: App settings aggregate every subsystem, Requirement: App settings are serializable, Requirement: Chrono settings use an explicit file pair, Scenario: Chrono pair is supplied, Scenario: Chrono settings render through App_settings, Scenario: Loader output has no from_json (+5 more)

### Community 414 - "Requirement: Factory hierarchy and passkey construction"
Cohesion: 0.14
Nodes (13): MODIFIED Requirements, Requirement: Factory hierarchy and passkey construction, Requirement: Meter instrument creation, Scenario: Counter and gauge creation, Scenario: Direct construction is rejected, Scenario: Histogram creation parameters, Scenario: Instrument types are distinct, Scenario: Invalid histogram parameters (+5 more)

### Community 415 - "ADDED Requirements"
Cohesion: 0.14
Nodes (13): ADDED Requirements, Requirement: Command-line tools install when built, Requirement: Exported CMake package, Requirement: Installable header and library tree, Requirement: Transitive dependency discovery, Scenario: Archives are installed, Scenario: Consumer links without manual dependency setup, Scenario: External discovery succeeds (+5 more)

### Community 416 - "Requirement: Child environment and CPU affinity are configurable"
Cohesion: 0.14
Nodes (13): ADDED Requirements, MODIFIED Requirements, Requirement: Child environment and CPU affinity are configurable, Requirement: Stdout and stderr are captured to files, Scenario: Child stderr goes to the instance log, Scenario: Child stdout goes to the instance log, Scenario: CPU affinity restricts the child, Scenario: Environment entries are visible to the child (+5 more)

### Community 417 - "Requirement: CPU core affinity"
Cohesion: 0.14
Nodes (13): MODIFIED Requirements, Requirement: CPU core affinity, Requirement: Provider configuration serialization, Scenario: Configuration formats, Scenario: Configuration JSON round-trip, Scenario: Configuration renders diagnostics, Scenario: CPU above 63 is supported, Scenario: Disabled harvesting applies nothing (+5 more)

### Community 418 - "Requirement: CPU-pinned background consumer"
Cohesion: 0.14
Nodes (13): MODIFIED Requirements, Purpose, Requirement: CPU-pinned background consumer, Requirement: Provider serialization surfaces, Scenario: Handles and snapshots render diagnostics, Scenario: Idle consumer wake-ups are bounded by the ceiling, Scenario: Live provider objects are not deserializable, Scenario: Per-shard FIFO and fair drain hold (+5 more)

### Community 419 - "Requirement: Base clock-domain identifiers"
Cohesion: 0.14
Nodes (13): chrono/clock-id Specification, Purpose, Requirement: Base clock-domain identifiers, Requirement: Compile-time domain declaration, Requirements, Scenario: Declaration is a constant expression, Scenario: Enum enumerates the base domains, Scenario: Formatter matches to_string (+5 more)

### Community 420 - "Requirements"
Cohesion: 0.14
Nodes (13): exec/state-client Specification, Purpose, Requirement: Function Group execution, Requirement: SafeState priority, Requirement: StateClient command reception, Requirement: StateClient diagnostics, Requirements, Scenario: EM receives a SetState command (+5 more)

### Community 421 - "Requirement: LoggerOptions defines the engine configuration"
Cohesion: 0.14
Nodes (13): logging/logger-options Specification, Purpose, Requirement: LoggerOptions defines the engine configuration, Requirement: OverflowPolicy enum controls full-ring behavior, Requirement: Runtime reconfiguration is wait-free, Requirements, Scenario: Default-constructed options carry documented defaults, Scenario: Overflow policy values are serializable (+5 more)

### Community 422 - "Requirement: Dynamic quota updates are race-free"
Cohesion: 0.14
Nodes (13): logging/spam-protection Specification, Purpose, Requirement: Dynamic quota updates are race-free, Requirement: Per-shard token-bucket rate limiter, Requirement: Repeat suppression collapses identical messages, Requirements, Scenario: Boundary mixture of fields is tolerated, Scenario: Config change takes effect without stopping producers (+5 more)

### Community 423 - "Requirement: Nested and cross-thread spans example"
Cohesion: 0.14
Nodes (13): Purpose, Requirement: Nested and cross-thread spans example, Requirement: Perfetto Chrome trace example, Requirement: Tracing examples pass in every build mode, Requirements, Scenario: Full build-mode matrix is green, Scenario: The example guides the user, Scenario: The example is self-contained (+5 more)

### Community 424 - "Sink"
Cohesion: 0.21
Nodes (13): append_frame(), drain_subscriber(), flush_and_close(), format_frame(), open_log_file(), rotate_if_needed(), run_session(), Sink (+5 more)

### Community 425 - "run_suite"
Cohesion: 0.24
Nodes (7): run_hub_http_server_suites(), run_suite(), main(), run_hub_model_suites(), run_hub_streaming_suites(), run_hub_web_assets_suites(), test_web_assets()

### Community 426 - "metrics/sinks_test.cpp"
Cohesion: 0.26
Nodes (11): test_sinks_json(), test_sinks_json_append(), test_sinks_json_composite(), test_sinks_json_diagnostics(), test_sinks_json_file(), test_sinks_json_open_failure(), test_sinks_json_opened(), test_sinks_json_records() (+3 more)

### Community 427 - "make_record"
Cohesion: 0.20
Nodes (16): LatencyStats, max_ns, median_ns, p999_ns, p99_ns, make_record(), receive_frames(), stats_of() (+8 more)

### Community 428 - "ShardState"
Cohesion: 0.07
Nodes (30): Entry, name, value, EnumTraits<nexigen::logging::ShardState>, entries, from_json(), from_string(), ShardState (+22 more)

### Community 429 - "Tasks"
Cohesion: 0.15
Nodes (9): std::formatter<nexigen::tracing::JsonFileTraceSink>, 1. Build + output fixes (P2-16, P2-17), 2. Sink overflow observability (P2-18; spec deltas metric-sinks + trace-sinks), 3. Gauge/StateGauge honesty (P2-20), 5. Composite parity (P2-22), 6. App/runner hardening (P2-23), 7. Shared detail unification (P3-24, P3-25), 8. Repo + verification matrix (+1 more)

### Community 430 - "ADDED Requirements"
Cohesion: 0.07
Nodes (28): ADDED Requirements, Purpose, Requirement: Composite metric sink fan-out, Requirement: Console metric sink, Requirement: File sink configuration serialization, Requirement: JSON-lines file sink, Requirement: Metric sink concept, Scenario: Batch becomes one JSON line (+20 more)

### Community 431 - "ADDED Requirements"
Cohesion: 0.15
Nodes (12): ADDED Requirements, Requirement: Command-line options initialize the infrastructure, Requirement: Help is rejected as an initialization input, Requirement: Settings-loading failures are fail-fast, Scenario: Best-effort settings remain available, Scenario: Caller can opt into degraded startup, Scenario: Existing settings overload still works, Scenario: Help options are not initialized (+4 more)

### Community 432 - "Decisions"
Cohesion: 0.12
Nodes (15): Context, D1 — Один поток: WaitSet, interval + notification, D2 — Ring: pre-allocated массив, drop-oldest, без атомиков, D3 — Триггер: PHM→SM RecoveryNotification, не SM-поле, D4 — Бинарный снимок с версионированным заголовком, D5 — Политика сброса: freeze, temp+rename, clear, no dump on SIGTERM, D6 — Ротация дампов, D7 — Lifecycle, affinity, mlockall, readiness (+7 more)

### Community 433 - "Requirement: Runnable command-line settings example"
Cohesion: 0.15
Nodes (12): MODIFIED Requirements, Requirement: Runnable command-line settings example, Scenario: All four artifact kinds are written, Scenario: Command-line options drive initialization, Scenario: Existing output directory is cleared, Scenario: Failures are reported, Scenario: Help prints usage without initializing, Scenario: Inputs are prepared before parsing (+4 more)

### Community 434 - "Decisions"
Cohesion: 0.15
Nodes (12): Context, D1 — Standard GNU install layout, D2 — One export set, namespace `nexigen::`, D3 — Version compatibility is same-minor, D4 — Package config resolves dependencies explicitly, D5 — Tools install only when built, D6 — Clang CI pairs the compiler with libstdc++ 15, D7 — Example uses the mandated free `to_string` (+4 more)

### Community 435 - "Decisions"
Cohesion: 0.15
Nodes (12): Context, D1 — Keep the coverage target unit-test-only, D2 — Derive module names from gcov data, D3 — CI uses a GCC 15 container, D4 — Interim coverage gate, D5 — Workflow triggers and concurrency, D6 — Latency gates are opt-out for shared CI, D7 — Tests create their scratch root (+4 more)

### Community 436 - "ADDED Requirements"
Cohesion: 0.12
Nodes (16): ADDED Requirements, Purpose, Requirement: Daemon formats a final JSON line, Requirement: Daemon subscribes and drains log frames, Requirement: Daemon writes to disk with rotation, Requirement: LogDaemon configuration, Requirement: LogDaemon diagnostics, Scenario: Config loads from file (+8 more)

### Community 437 - "Requirement: CPU core affinity"
Cohesion: 0.15
Nodes (12): MODIFIED Requirements, Requirement: CPU core affinity, Requirement: Provider configuration serialization, Scenario: Configuration JSON round-trip, Scenario: Configuration renders and formats, Scenario: CPU above 63 is supported, Scenario: Invalid mask is visible without stopping delivery, Scenario: Required failure prevents snapshot delivery (+4 more)

### Community 438 - "Requirement: Background writer CPU core affinity"
Cohesion: 0.15
Nodes (12): MODIFIED Requirements, Requirement: Background writer CPU core affinity, Requirement: File logger vocabulary diagnostic strings, Scenario: Best-effort failure remains non-fatal and visible, Scenario: Config dump is complete, Scenario: Config thread spec round-trips, Scenario: CPU above 63 is supported, Scenario: Drop snapshot renders (+4 more)

### Community 439 - "Requirements"
Cohesion: 0.12
Nodes (16): logging/log-daemon Specification, Purpose, Requirement: Daemon formats a final JSON line, Requirement: Daemon subscribes and drains log frames, Requirement: Daemon writes to disk with rotation, Requirement: LogDaemon configuration, Requirement: LogDaemon diagnostics, Requirements (+8 more)

### Community 440 - "ADDED Requirements"
Cohesion: 0.15
Nodes (12): ADDED Requirements, Purpose, Requirement: Field diagnostics, Requirement: Field get-set-notify, Requirement: Field is owned by one writer, Requirement: Last value on connect, Scenario: Field renders its state, Scenario: Late subscriber reads the current value (+4 more)

### Community 441 - "ADDED Requirements"
Cohesion: 0.15
Nodes (12): ADDED Requirements, Purpose, Requirement: Background-thread CPU isolation, Requirement: Node and daemon-less discovery, Requirement: Protocol-independent boundary, Requirement: Service naming, Scenario: Business code is transport-agnostic, Scenario: Infrastructure threads pin to background cores (+4 more)

### Community 442 - "Requirements"
Cohesion: 0.15
Nodes (12): com/field Specification, Purpose, Requirement: Field diagnostics, Requirement: Field get-set-notify, Requirement: Field is owned by one writer, Requirement: Last value on connect, Requirements, Scenario: Field renders its state (+4 more)

### Community 443 - "Requirements"
Cohesion: 0.15
Nodes (12): com/service-management Specification, Purpose, Requirement: Background-thread CPU isolation, Requirement: Node and daemon-less discovery, Requirement: Protocol-independent boundary, Requirement: Service naming, Requirements, Scenario: Business code is transport-agnostic (+4 more)

### Community 444 - "Requirement: Pinning the calling thread by core mask"
Cohesion: 0.15
Nodes (12): core/affinity Specification, Purpose, Requirement: Pinning the calling thread by core mask, Requirement: Querying the calling thread's affinity, Requirement: Single implementation behind module shims, Requirements, Scenario: Audit finds one implementation and four forwarders, Scenario: Each thread pins only itself (+4 more)

### Community 445 - "Decisions"
Cohesion: 0.12
Nodes (14): D1 — Reuse `chrono::Chrono_settings` directly, D2 — Policy contract gains a required `Chrono_sink` + `emplace_chrono_sink`, D3 — Two-phase construction and shutdown order, D4 — Member lifetime order, D5 — Module-local worker log hook, D6 — No new concurrent or atomic state, Decisions, 1. ChronoProvider two-phase lifecycle (+6 more)

### Community 446 - "Requirements"
Cohesion: 0.15
Nodes (12): hub/http-api Specification, Purpose, Requirement: Complexity of decomposed functions stays within the project target, Requirement: HTTP serving is cold path only, Requirement: Refactored internals preserve all observable behavior, Requirement: The server run loop uses an explicit state stepper, Requirements, Scenario: Application ticks are untouched (+4 more)

### Community 447 - "What Changes"
Cohesion: 0.27
Nodes (9): Design, Goals / Non-Goals, Migration Plan, Risks / Trade-offs, stress_base(), test_stress(), test_stress_composite(), test_stress_console() (+1 more)

### Community 448 - "ADDED Requirements"
Cohesion: 0.11
Nodes (18): ADDED Requirements, Purpose, Requirement: Compile-time level naming, Requirement: LogLevel JSON serialization, Requirement: LogLevel name parsing, Requirement: Severity catalogue, Requirement: std::formatter for LogLevel, Scenario: Enumerators are distinct and severity-ordered (+10 more)

### Community 451 - "FakeProvider"
Cohesion: 0.14
Nodes (5): FakeProvider, stamper_, FakeStamper, counter, system_clock_ns()

### Community 452 - "http_parse.hpp"
Cohesion: 0.41
Nodes (9): ascii_lower(), equals_ignore_case(), header_value(), parse_request_line(), query_value(), trim_header(), url_decode(), D1: Extract only pure helpers (+1 more)

### Community 453 - "Tasks"
Cohesion: 0.17
Nodes (11): format_param, 10. Verification, 1. Convention anchor, 2. Leaf enums, 4. Config structs, 5. Format/record helpers, 7. Logger classes, 8. src/logging split (+3 more)

### Community 454 - ".pop"
Cohesion: 0.28
Nodes (6): SinkBinding, self, TracerProvider<ShardCapacity, MaxThreads>::answer_flush(), TracerProvider<ShardCapacity, MaxThreads>::deliver_batch(), TracerProvider<ShardCapacity, MaxThreads>::shutdown_drain(), TracerProvider<ShardCapacity, MaxThreads>::TracerProvider()

### Community 455 - "openspec-explore/SKILL.md"
Cohesion: 0.17
Nodes (11): Check for context, Ending Discovery, Guardrails, Handling Different Entry Points, OpenSpec Awareness, Planning a Change, The Stance, What You Don't Have To Do (+3 more)

### Community 456 - "ADDED Requirements"
Cohesion: 0.17
Nodes (11): ADDED Requirements, Requirement: Example diagnostic formatting, Requirement: Runnable SPSC telemetry example, Requirement: Wait-free outcome handling in the example, Scenario: Empty queue is classified, Scenario: Error codes render directly, Scenario: Final queue summary, Scenario: Full queue is classified (+3 more)

### Community 457 - "Requirement: Conforming no-op logger"
Cohesion: 0.17
Nodes (11): ADDED Requirements, MODIFIED Requirements, Requirement: Calls compile away, Requirement: Conforming no-op logger, Requirement: NullLogger diagnostic string, Scenario: Dump names the threshold, Scenario: Every call succeeds silently, Scenario: Level control is still honored (+3 more)

### Community 458 - "Requirement: Fixed-width ISO-8601 nanosecond formatting"
Cohesion: 0.17
Nodes (11): ADDED Requirements, Purpose, Requirement: Fixed-width ISO-8601 nanosecond formatting, Requirement: UTC civil-time decomposition, Scenario: Byte-exact rendering, Scenario: Formatting touches no shared state, Scenario: Known epoch decomposes exactly, Scenario: Leap day and year-end boundaries (+3 more)

### Community 459 - "Design"
Cohesion: 0.17
Nodes (11): Context, D1 — Extend `emit_summary_if_needed` to all causes, D2 — Always-on summary trigger, D3 — Flush-time summary, D4 — Retry safety, Decisions, Design, Goals / Non-Goals (+3 more)

### Community 460 - "ADDED Requirements"
Cohesion: 0.17
Nodes (11): ADDED Requirements, Purpose, Requirement: Dynamic quota updates are wait-free, Requirement: Per-shard token-bucket rate limiter, Requirement: Repeat suppression collapses identical messages, Scenario: Config change takes effect without stopping producers, Scenario: Identical consecutive messages are collapsed, Scenario: Messages at or above bypass_min_level always pass (+3 more)

### Community 461 - "Requirement: Coverage includes forked supervisors"
Cohesion: 0.17
Nodes (11): ADDED Requirements, Purpose, Requirement: Coverage includes forked supervisors, Requirement: Runner and hub coverage gates, Scenario: Application setup contributes coverage, Scenario: Coverage gate fails a regression, Scenario: Hub stays above the gate, Scenario: Non-coverage builds are unaffected (+3 more)

### Community 462 - "Requirement: Generated settings contain absolute sink paths"
Cohesion: 0.17
Nodes (11): MODIFIED Requirements, Requirement: Generated settings contain absolute sink paths, Requirement: Instance directory layout is deterministic, Scenario: Artifact root relocates sink paths, Scenario: Directory layout is created on start, Scenario: Every sink.path points under the instance directory, Scenario: Four settings files are written, Scenario: Logging settings use the correct root (+3 more)

### Community 463 - "Decisions"
Cohesion: 0.12
Nodes (13): std::formatter<nexigen::metrics::MetricProviderConfig>, D1 — LTO via `INTERPROCEDURAL_OPTIMIZATION_<CONFIG>`, D2 — `core_mask` renders as `{:#x}`, D3 — Scratch-overflow loss counters in the JSON-line sinks, D4 — `Gauge<MultiThreaded>::set` assigns, not adds, D6 — `SignalWatcher::start` guards double start, D8 — `.gitignore` gains `build-*/`, Decisions (+5 more)

### Community 464 - "Requirement: Suite-metadata read targets are run-root contained"
Cohesion: 0.17
Nodes (11): ADDED Requirements, hub/discovery delta spec, REMOVED Requirements, Requirement: Artifact roots come from suite metadata, Requirement: Non-contained suite metadata degrades gracefully, Requirement: Suite-metadata read targets are run-root contained, Scenario: Artifact root changes are observed, Scenario: Contained artifact root is still followed (+3 more)

### Community 465 - "Requirement: Global logging settings supply operational fields"
Cohesion: 0.17
Nodes (11): MODIFIED Requirements, Requirement: Global logging settings supply operational fields, Requirement: Runner diagnostics use the project file logger, Scenario: Application logs stay separate, Scenario: Invalid global file is non-fatal, Scenario: Missing global file keeps runner defaults, Scenario: Operational fields are applied, Scenario: Records are append-only (+3 more)

### Community 466 - "JsonFileTraceSinkConfig"
Cohesion: 0.12
Nodes (15): Settings, app_name, chrono, logging, metrics, tracing, to_string(), 1. Remove app_name from logging (+7 more)

### Community 467 - "Requirements"
Cohesion: 0.12
Nodes (16): Purpose, Requirement: Propagable span context, Requirement: Span attribute capacity bounds, Requirement: Span attribute string storage, Requirement: Trace and span identifier generation, Requirements, Scenario: Attribute capacity is bounded, Scenario: Context renders and parses as a diagnostic (+8 more)

### Community 468 - "ADDED Requirements"
Cohesion: 0.17
Nodes (11): ADDED Requirements, Purpose, Requirement: Request arbitration, Requirement: Startup and shutdown sequencing, Requirement: State machines execution, Scenario: Higher priority wins, Scenario: Invalid transition is rejected, Scenario: Shutdown sequences the groups (+3 more)

### Community 469 - "ADDED Requirements"
Cohesion: 0.17
Nodes (11): ADDED Requirements, Purpose, Requirement: Drop count crosses the process boundary, Requirement: Iceoryx2 logger diagnostics, Requirement: Worker publishes frames off the hot path, Scenario: A frame reaches a subscriber, Scenario: Logger renders its state, Scenario: Overflow is reported in the next frame (+3 more)

### Community 470 - ".loan_uninit"
Cohesion: 0.15
Nodes (13): 1. Введение и область применения, 2. Модель взаимодействия (Communication Patterns), 3. Динамический поиск сервисов (Service Discovery & Namespace), 4. Архитектурная абстракция и управление памятью (Zero-Copy & Memory Management), 5. Качество обслуживания и диагностика очередей (QoS & Diagnostics), 6. Конфигурация (Service Manifest), Спецификация требований к модулю Communication Management (nexigen::com) на базе iceoryx2, Requirement: Zero-copy publication (+5 more)

### Community 471 - "Requirements"
Cohesion: 0.17
Nodes (11): logging/iceoryx2-logger Specification, Purpose, Requirement: Drop count crosses the process boundary, Requirement: Iceoryx2 logger diagnostics, Requirement: Worker publishes frames off the hot path, Requirements, Scenario: A frame reaches a subscriber, Scenario: Logger renders its state (+3 more)

### Community 472 - "Requirement: Runtime level control"
Cohesion: 0.17
Nodes (11): logging/log-level Specification, Purpose, Requirement: Runtime level control, Requirement: Sub-threshold calls short-circuit, Requirements, Scenario: Filtered call performs no logging work, Scenario: Filtered call reports success, Scenario: Filtered-path latency is bounded (+3 more)

### Community 473 - "Requirement: Conforming no-op logger"
Cohesion: 0.17
Nodes (11): logging/null-logger Specification, Purpose, Requirement: Calls compile away, Requirement: Conforming no-op logger, Requirement: NullLogger diagnostic string, Requirements, Scenario: Dump names the threshold, Scenario: Every call succeeds silently (+3 more)

### Community 474 - ".get_meter"
Cohesion: 0.16
Nodes (12): 4.1. Критические, 4.2. Высокие / средние, 4. Проблемы (по убыванию тяжести), Scenario: Meter creation goes through the provider, Context, Design, Goals / Non-Goals, Migration Plan (+4 more)

### Community 475 - "Requirement: Coverage includes forked supervisors"
Cohesion: 0.17
Nodes (11): Purpose, quality/coverage Specification, Requirement: Coverage includes forked supervisors, Requirement: ExecutionManager and hub coverage gates, Requirements, Scenario: Application setup contributes coverage, Scenario: Coverage gate fails a regression, Scenario: ExecutionManager stays above the gate (+3 more)

### Community 476 - "Requirement: State machines execution"
Cohesion: 0.17
Nodes (11): Purpose, Requirement: Request arbitration, Requirement: Startup and shutdown sequencing, Requirement: State machines execution, Requirements, Scenario: Higher priority wins, Scenario: Invalid transition is rejected, Scenario: Shutdown sequences the groups (+3 more)

### Community 477 - "Color"
Cohesion: 0.18
Nodes (10): Color, Blue, Green, Red, Entry, name, value, EnumTraits<probe::Color> (+2 more)

### Community 478 - "nexigen-run application suites"
Cohesion: 0.18
Nodes (11): Application fields, Commands, Limitations, nexigen-run application suites, Restart and impact policy, Runner logging, Runner thread affinity, Runtime artifacts (+3 more)

### Community 479 - "Sample"
Cohesion: 0.31
Nodes (3): Sample, inner_, to_string()

### Community 480 - "Design"
Cohesion: 0.18
Nodes (10): fault(), Context, D3: Inline test-only fault seam, D4: Analyzer opt-ins, Decisions, Design, Goals / Non-Goals, Migration Plan (+2 more)

### Community 481 - "ADDED Requirements"
Cohesion: 0.15
Nodes (12): ADDED Requirements, Purpose, Requirement: Pinning the calling thread by core mask, Requirement: Querying the calling thread's affinity, Requirement: Single implementation behind module shims, Scenario: Audit finds one implementation and four forwarders, Scenario: Each thread pins only itself, Scenario: Query reports the pre-pinning platform state (+4 more)

### Community 482 - "TraceHeader"
Cohesion: 0.14
Nodes (14): make_trace_header(), std::formatter<nexigen::tracing::TraceHeader>, to_string(), TraceHeader, publish_timestamp_ns, sequence_number, source_span_id, trace_id (+6 more)

### Community 483 - "example_of_system_manifest.yaml"
Cohesion: 0.18
Nodes (10): function_groups, initial_state, name, states, global_settings, default_bg_cpu_mask, iceoryx2_root_dir, ipc_cleanup_on_crash (+2 more)

### Community 484 - "prometheus_http"
Cohesion: 0.18
Nodes (11): exporters, ipc_summary_publisher, prometheus_http, enabled, history_size, service_name, bind_address, enabled (+3 more)

### Community 485 - "phm_health_alerts"
Cohesion: 0.18
Nodes (11): shm_free_space_min_mb, soc_temp_critical_celsius, soc_temp_warning_celsius, phm_health_alerts, enabled, hardware_limits, phm_ipc_channel, process_resource_limits (+3 more)

### Community 486 - "scraping"
Cohesion: 0.18
Nodes (11): composite_reset_method, scalar_reset_method, scraping, reset_policies, scrape_interval_ms, shm_registry, fallback_to_last_valid, max_retries (+3 more)

### Community 487 - "opsx-explore.md"
Cohesion: 0.18
Nodes (10): Check for context, Ending Discovery, Guardrails, OpenSpec Awareness, Planning a Change, The Stance, What You Don't Have To Do, What You Might Do (+2 more)

### Community 488 - "LatencyStats"
Cohesion: 0.06
Nodes (32): 1. Harness scaffold, 3. Verification matrix, Tasks, Workflow follow-up, BestMedians, direct, direct_stats, event_stats (+24 more)

### Community 489 - "ADDED Requirements"
Cohesion: 0.18
Nodes (10): ADDED Requirements, Purpose, Requirement: Compile-time format/argument checking, Requirement: FormatWithLocation wrapper type, Requirement: Wrapper adds no producer-side formatting, Scenario: Hot path sees only storage, Scenario: Implicit construction from a literal with arguments, Scenario: Location is the caller's statement (+2 more)

### Community 490 - "Requirement: Runnable command-line settings example"
Cohesion: 0.18
Nodes (10): ADDED Requirements, Requirement: Runnable command-line settings example, Scenario: All four artifact kinds are written, Scenario: Command-line options drive initialization, Scenario: Existing output directory is cleared, Scenario: Failures are reported, Scenario: Help prints usage without initializing, Scenario: Inputs are prepared before parsing (+2 more)

### Community 491 - "Design"
Cohesion: 0.18
Nodes (10): Context, D1 — Inline stateless policy, D2 — Direct main, default settings, D3 — Demonstrate all three paths, D4 — Failure visibility, Decisions, Design, Goals / Non-Goals (+2 more)

### Community 492 - "Design"
Cohesion: 0.18
Nodes (10): Context, D1: Target remaining failure branches, not synthetic line hits, D2: Clang-tidy profile with documented exclusions, D3: Fix findings in production code, not by suppressions, Decisions, Design, Goals / Non-Goals, Migration Plan (+2 more)

### Community 493 - "Requirement: Portable Release mode"
Cohesion: 0.18
Nodes (10): ADDED Requirements, Purpose, Requirement: Downloaded dependencies are content-pinned, Requirement: Portable Release mode, Scenario: Correct archive is accepted, Scenario: Native mode remains the default, Scenario: Portable binaries run on another CPU, Scenario: Portable mode removes host-specific flags (+2 more)

### Community 494 - "ADDED Requirements"
Cohesion: 0.18
Nodes (10): ADDED Requirements, Purpose, Requirement: Suite stop is final and idempotent, Requirement: Suite supervision is cold path only, Scenario: No supervision work on application hot paths, Scenario: Stop is idempotent, Scenario: Stop succeeds when the suite supervisor is gone, Scenario: Stop terminates every application (+2 more)

### Community 495 - "Requirement: Documented example invocation"
Cohesion: 0.18
Nodes (10): MODIFIED Requirements, Requirement: Documented example invocation, Requirement: Runnable example catalogue, Scenario: Example executables exist after a default build, Scenario: Examples are self-contained, Scenario: Reader can build and run an example from the README, Scenario: Reader can run and clean the command-line settings example, Scenario: Reader can run and clean the suite demonstration (+2 more)

### Community 496 - "Requirement: Settings diagnostics are serializable"
Cohesion: 0.18
Nodes (10): MODIFIED Requirements, Requirement: Runnable simple settings example, Requirement: Settings diagnostics are serializable, Scenario: Diagnostic JSON round-trips, Scenario: Diagnostic renders its source and field, Scenario: Example creates its own inputs, Scenario: Failures are visible, Scenario: Logger initializes from the merged files (+2 more)

### Community 497 - "Requirement: Console writer accepts a thread specification"
Cohesion: 0.18
Nodes (10): ADDED Requirements, MODIFIED Requirements, Requirement: Console logger diagnostic strings, Requirement: Console writer accepts a thread specification, Scenario: Config and logger render, Scenario: Config JSON and formatter preserve the worker spec, Scenario: Console writer applies valid attributes, Scenario: Default console behavior is unchanged (+2 more)

### Community 498 - "Requirement: Child environment and CPU affinity are configurable"
Cohesion: 0.18
Nodes (10): MODIFIED Requirements, Requirement: Child environment and CPU affinity are configurable, Scenario: Application name is passed via argv, Scenario: CPU affinity restricts the child, Scenario: CPUs above 63 reach the child unchanged, Scenario: Empty CPU set retains existing reset behavior, Scenario: Environment entries are visible to the child, Scenario: Invalid CPU index is rejected before fork (+2 more)

### Community 499 - "Requirement: Affinity failures are non-fatal warnings"
Cohesion: 0.18
Nodes (10): MODIFIED Requirements, Requirement: Affinity failures are non-fatal warnings, Scenario: Applications are not affected by a failed pin, Scenario: Invalid control CPU is ignored, Scenario: Invalid logger CPU is ignored, Scenario: Kernel rejection is ignored, Scenario: Partial application is reported accurately, Scenario: Required control or logger failure aborts initialization (+2 more)

### Community 500 - "Design"
Cohesion: 0.18
Nodes (10): 1. Validator decomposition, 2. Reuse the existing `read_log_level` pattern, 3. Keep helpers internal, 4. Test through `load_suite_manifest`, Context, Decisions, Design, Goals / Non-Goals (+2 more)

### Community 501 - "Requirements"
Cohesion: 0.12
Nodes (16): Purpose, Requirement: Complexity of decomposed functions stays within the project target, Requirement: Refactored internals preserve all observable behavior, Requirement: `Runner::start` is split into validation, layout, and launch phases, Requirement: `Runner::stop` uses state helpers, Requirement: `supervise_child` delegates to wait helpers, Requirements, runner/process-supervision — internal structure (delta) (+8 more)

### Community 502 - "ADDED Requirements"
Cohesion: 0.18
Nodes (10): ADDED Requirements, Purpose, Requirement: Function Group execution, Requirement: StateClient command reception, Requirement: StateClient diagnostics, Scenario: EM receives a SetState command, Scenario: Group processes are switched, Scenario: StateClient renders its last command (+2 more)

### Community 503 - "ADDED Requirements"
Cohesion: 0.18
Nodes (10): ADDED Requirements, Purpose, Requirement: Current state publication, Requirement: Prepare-for-transition handshake, Requirement: TriggerIn request service, Scenario: Application requests a mode change, Scenario: Late process reads the state, Scenario: Services acknowledge before the switch (+2 more)

### Community 504 - "Requirements"
Cohesion: 0.18
Nodes (10): logging/format-api Specification, Purpose, Requirement: Compile-time format/argument checking, Requirement: FormatWithLocation wrapper type, Requirement: Wrapper adds no producer-side formatting, Requirements, Scenario: Hot path sees only storage, Scenario: Implicit construction from a literal with arguments (+2 more)

### Community 505 - "Requirement: Portable Release mode"
Cohesion: 0.18
Nodes (10): Purpose, quality/reproducible-builds Specification, Requirement: Downloaded dependencies are content-pinned, Requirement: Portable Release mode, Requirements, Scenario: Correct archive is accepted, Scenario: Native mode remains the default, Scenario: Portable binaries run on another CPU (+2 more)

### Community 506 - "Requirements"
Cohesion: 0.18
Nodes (10): Purpose, Requirement: Current state publication, Requirement: Prepare-for-transition handshake, Requirement: TriggerIn request service, Requirements, Scenario: Application requests a mode change, Scenario: Late process reads the state, Scenario: Services acknowledge before the switch (+2 more)

### Community 507 - "sink_pack_negative.cpp"
Cohesion: 0.22
Nodes (8): Ala, visit, Bet, visit, DupA, visit, DupPack, Triple

### Community 508 - "What Changes"
Cohesion: 0.20
Nodes (11): What Changes, Requirement: Multi-unit elapsed queries do not mutate state, Scenario: Elapsed remains non-decreasing while running, Scenario: Queries are valid in running and stopped states, Scenario: Unit conversions scale correctly against the nanosecond total, Requirement: Multi-unit elapsed queries do not mutate state, Scenario: Elapsed remains non-decreasing while running, Scenario: Queries are valid in running and stopped states (+3 more)

### Community 509 - "message_non_pod_negative.cpp"
Cohesion: 0.22
Nodes (5): HasVirtual, OwnsContainer, values, OwnsString, name

### Community 510 - "REMOVED Requirements"
Cohesion: 0.15
Nodes (12): REMOVED Requirements, Requirement: Background snapshot delivery and collection period, Requirement: CPU core affinity, Requirement: Deferred worker start, Requirement: Optional worker logger, Requirement: Provider configuration serialization, Requirement: Provider construction and sink binding, Requirement: Provider control plane (+4 more)

### Community 511 - "ci.yml"
Cohesion: 0.17
Nodes (8): concurrency, cancel-in-progress, group, env, CMAKE_BUILD_PARALLEL_LEVEL, name, permissions, contents

### Community 512 - "Fanout"
Cohesion: 0.24
Nodes (3): Fanout, members_, size

### Community 513 - "MetricDaemonConfig"
Cohesion: 0.08
Nodes (22): MetricDaemonConfig, batch_flush_interval_ms, bg_cpu_affinity, bind_address, enable_http, endpoint, max_metrics, max_storage_mb (+14 more)

### Community 514 - "TriggerRequest"
Cohesion: 0.27
Nodes (6): std::formatter<nexigen::sm::TriggerRequest>, to_string(), TriggerRequest, function_group, requester_id, target_state

### Community 515 - "DroppedStats"
Cohesion: 0.24
Nodes (5): DroppedStats, buffer_full, rate_limited, slot_exhausted, std::formatter<nexigen::tracing::DroppedStats>

### Community 516 - "environment_variables"
Cohesion: 0.20
Nodes (10): NEXIGEN_BG_CPU_MASK, NEXIGEN_CGROUP_ROOT, NEXIGEN_DEFAULT_LOG_LEVEL, NEXIGEN_INSTANCE_ID, NEXIGEN_LOG_DIR, NEXIGEN_MAX_METRICS_PER_APP, NEXIGEN_SM_RT_CPU_MASK, NEXIGEN_TRACE_DUMP_DIR (+2 more)

### Community 517 - "Requirement: Error code catalogue"
Cohesion: 0.20
Nodes (9): MODIFIED Requirements, Requirement: Compile-time error code naming, Requirement: Error code catalogue, Scenario: Enumerators are available and distinct, Scenario: Name of a known code is resolvable at compile time, Scenario: Naming allocates nothing, Scenario: Success is representable, Scenario: Throttle rejection is distinguishable from queue rejection (+1 more)

### Community 518 - "Requirement: Conforming no-op logger"
Cohesion: 0.20
Nodes (9): ADDED Requirements, Purpose, Requirement: Calls compile away, Requirement: Conforming no-op logger, Scenario: Every call succeeds silently, Scenario: Level control is still honored, Scenario: NullLogger satisfies the concept, Scenario: Optimized loop leaves no logging residue (+1 more)

### Community 519 - "Tasks"
Cohesion: 0.20
Nodes (9): 1. nlohmann_json build integration (design D1), 2. Enum vocabulary: from_string, JSON, formatter (design D2), 3. Universal to_string inventory (design D9), 4. FormatWithLocation, packing, deferred LogRecord (design D3–D5), 5. Multi-producer shard transport (design D6–D7), 6. Logger API migration (design D8, D10), 7. Concurrency and latency verification, 8. Full-matrix integration checks (+1 more)

### Community 520 - "Requirement: Sharded drop accounting without contention"
Cohesion: 0.20
Nodes (9): MODIFIED Requirements, Requirement: Sharded drop accounting without contention, Scenario: Drop summary emitted without throttle, Scenario: Failed summary push preserves counters, Scenario: Flush-time drop summary is emitted, Scenario: Full shard drops only that shard's producer, Scenario: Snapshot aggregates all shards, Scenario: Summary includes all drop causes (+1 more)

### Community 521 - "ADDED Requirements"
Cohesion: 0.20
Nodes (9): ADDED Requirements, Purpose, Requirement: Pre-allocated shard pool, Requirement: Shard Draining and pool return, Requirement: Thread-local slot registration and pool exhaustion, Scenario: Freed shard is reusable, Scenario: Pool exhaustion counts drops instead of aborting, Scenario: Zero steady-state allocation (+1 more)

### Community 522 - "ADDED Requirements"
Cohesion: 0.15
Nodes (12): ADDED Requirements, Purpose, Requirement: CLI log level overrides loaded settings, Requirement: Command-line contract is documented, Requirement: Missing JSON fields use defaults, Requirement: Parsing is cold path only, Scenario: Absent CLI level preserves the loaded value, Scenario: CLI level wins over files and environment (+4 more)

### Community 523 - "Proposal"
Cohesion: 0.20
Nodes (9): Capabilities, Coverage budget, Impact, Modified Capabilities, New Capabilities, Proposal, Unreachable-line policy, What Changes (+1 more)

### Community 524 - "MODIFIED Requirements"
Cohesion: 0.20
Nodes (9): MODIFIED Requirements, Requirement: Initialization follows a fixed order, Requirement: Stateless infrastructure policy, Scenario: Components outlive the providers, Scenario: Failure rolls back partial initialization, Scenario: Logger is ready before providers start, Scenario: Policy types are validated at compile time, Scenario: Repeated initialization is idempotent (+1 more)

### Community 525 - "Requirement: Error code catalogue"
Cohesion: 0.20
Nodes (9): MODIFIED Requirements, Requirement: Error code catalogue, Scenario: Conflict formats by name, Scenario: Conflict has stable value and naming, Scenario: Conflict is distinguishable from NotInitialized, Scenario: Enumerators are available and distinct, Scenario: Success is representable, Scenario: Throttle rejection is distinguishable from queue rejection (+1 more)

### Community 526 - "Requirement: Runnable minimal infrastructure example"
Cohesion: 0.20
Nodes (9): ADDED Requirements, REMOVED Requirements, Requirement: Runnable application lifecycle example, Requirement: Runnable minimal infrastructure example, Scenario: Example exercises logging, metrics and tracing, Scenario: Example reports failures, Scenario: Example stops and flushes, Scenario: Example uses a direct main flow (+1 more)

### Community 527 - "ADDED Requirements"
Cohesion: 0.20
Nodes (9): ADDED Requirements, Purpose, Requirement: Runner logging is cold path only, Requirement: Suite metadata preserves runner affinity, Scenario: Application hot paths are untouched, Scenario: Missing runner lists mean no pinning, Scenario: Only runner processes write the runner log, Scenario: Suite directory reproduces affinity (+1 more)

### Community 528 - "Tasks"
Cohesion: 0.20
Nodes (9): 1. Runner module boilerplate, 2. Resource_metadata / Exit_metadata, 3. Instance directory creation and resource.json, 4. Settings generation, 5. Process launch and supervision, 6. CLI binary, 7. Build and full verification matrix, Follow-up (not part of this change) (+1 more)

### Community 529 - "Design"
Cohesion: 0.10
Nodes (19): Context, D1 — Resolve the runner log path as `<artifacts_path>/<suite_id>/runner.jsonl`, D2 — Align the test fixture with the real runner layout, Decisions, Design, Goals / Non-Goals, Migration Plan, Open Questions (+11 more)

### Community 530 - "Requirement: Live updates use Server-Sent Events"
Cohesion: 0.20
Nodes (9): MODIFIED Requirements, Requirement: Live updates use Server-Sent Events, Scenario: All streams are available, Scenario: Heartbeat keeps the connection alive, Scenario: Replay then live, Scenario: Slow client is closed, Scenario: SSE headers are correct, Scenario: Tree snapshots are streamed (+1 more)

### Community 531 - "StatsBank"
Cohesion: 0.13
Nodes (10): StatsBank, count, m2, maximum, mean, minimum, StatsSlot, active (+2 more)

### Community 532 - "Requirement: JSON-lines file sink"
Cohesion: 0.20
Nodes (9): MODIFIED Requirements, Requirement: JSON-lines file sink, Scenario: Batch becomes one JSON line, Scenario: Flush persists pending data, Scenario: JSON strings are escaped, Scenario: Multiple batches append, Scenario: Open and write failures surface, Scenario: State records serialize their value (+1 more)

### Community 533 - "Design"
Cohesion: 0.20
Nodes (9): Context, D1 — Header-only `tools/detail/cli.hpp` with `nexigen::tools::parse_u64`, D2 — Unify on the `parse_u64` name, Decisions, Design, Goals / Non-Goals, Migration Plan, Open Questions (+1 more)

### Community 534 - "Requirement: HTTP listener is loopback by default"
Cohesion: 0.20
Nodes (9): hub/http-api delta spec, MODIFIED Requirements, Requirement: HTTP listener is loopback by default, Scenario: Bracketed IPv6 listener is honored, Scenario: Default listener is localhost, Scenario: Full-dual-stack wildcard binds both families, Scenario: Invalid address fails startup, Scenario: Listen override is honored (+1 more)

### Community 535 - "ADDED Requirements"
Cohesion: 0.20
Nodes (9): ADDED Requirements, Requirement: A failed start reaps the supervisor child, Requirement: Supervisor environment rejects NUL in values, Requirement: Unreapable child errors do not fabricate success, runner/process-supervision delta spec, Scenario: ECHILD during the SIGKILL leg reports unknown, Scenario: Failed handshake leaves no zombie, Scenario: Normal reaping is unchanged (+1 more)

### Community 536 - "Requirement: Merged settings apply to provider configuration"
Cohesion: 0.20
Nodes (9): ADDED Requirements, MODIFIED Requirements, Requirement: Merged settings apply to provider configuration, Requirement: Worker thread settings preserve legacy affinity compatibility, Scenario: Canonical worker fields merge across sources, Scenario: Legacy affinity maps to the worker CPU set, Scenario: Legacy environment override remains effective, Scenario: Provider config receives every mapped field (+1 more)

### Community 537 - "ADDED Requirements"
Cohesion: 0.20
Nodes (9): ADDED Requirements, Purpose, Requirement: Backoff sleeps grow geometrically to the ceiling, Requirement: Poll ceiling is clamped to a supported range, Requirement: Timer slack is widened on the worker thread, Scenario: Requests outside the range are clamped, Scenario: Slack never drops below the kernel default, Scenario: Sleeps double up to the ceiling (+1 more)

### Community 538 - "ADDED Requirements"
Cohesion: 0.20
Nodes (9): ADDED Requirements, Requirement: Crash cleanup before restart, Requirement: Dependency-ordered startup, Requirement: Startup-timeout recovery policy, Scenario: Dependent waits for its dependency, Scenario: Missing dependency fails startup, Scenario: Relaunch reuses the same identifiers, Scenario: Timed-out process is logged and recovered (+1 more)

### Community 539 - "ADDED Requirements"
Cohesion: 0.20
Nodes (9): ADDED Requirements, Purpose, Requirement: Declarative health manifest, Requirement: Manifest diagnostics, Requirement: Manifest serialization, Scenario: Manifest configures supervision, Scenario: Manifest renders its processes, Scenario: Manifest round-trips (+1 more)

### Community 540 - "ADDED Requirements"
Cohesion: 0.20
Nodes (9): ADDED Requirements, Purpose, Requirement: Declarative state-management manifest, Requirement: Manifest diagnostics, Requirement: Manifest serialization, Scenario: Manifest configures the arbitration, Scenario: Manifest renders its groups, Scenario: Manifest round-trips (+1 more)

### Community 541 - "ADDED Requirements"
Cohesion: 0.20
Nodes (9): ADDED Requirements, Purpose, Requirement: Performance metrics, Requirement: Telemetry diagnostics, Requirement: Transition audit log, Scenario: Metrics reflect arbitration behavior, Scenario: Telemetry renders its counters, Scenario: Transitions are journaled without blocking (+1 more)

### Community 542 - "Requirements"
Cohesion: 0.20
Nodes (9): core/worker-tuning Specification, Purpose, Requirement: Backoff sleeps grow geometrically to the ceiling, Requirement: Poll ceiling is clamped to a supported range, Requirement: Timer slack is widened on the worker thread, Requirements, Scenario: Requests outside the range are clamped, Scenario: Slack never drops below the kernel default (+1 more)

### Community 543 - "Decisions"
Cohesion: 0.09
Nodes (17): Context, D10. Concept re-expression, D1. JSON dependency rides `nexigen::core`'s INTERFACE, D2. Enum vocabulary headers (ADL by co-location), D3. `FormatWithLocation<Args...>` (new `include/nexigen/logging/format_with_location.hpp`), D4. Deferred-payload `LogRecord`: pinned 192 B layout, D5. Argument packing: fixed-offset trivially-copyable serialization, D6. `detail::Producer` becomes a shard array (`MaxThreads = 32` default, template parameter) (+9 more)

### Community 544 - "Requirements"
Cohesion: 0.20
Nodes (9): phm/manifest Specification, Purpose, Requirement: Declarative health manifest, Requirement: Manifest diagnostics, Requirement: Manifest serialization, Requirements, Scenario: Manifest configures supervision, Scenario: Manifest renders its processes (+1 more)

### Community 545 - "Requirements"
Cohesion: 0.20
Nodes (9): Purpose, Requirement: Declarative state-management manifest, Requirement: Manifest diagnostics, Requirement: Manifest serialization, Requirements, Scenario: Manifest configures the arbitration, Scenario: Manifest renders its groups, Scenario: Manifest round-trips (+1 more)

### Community 546 - "Requirements"
Cohesion: 0.20
Nodes (9): Purpose, Requirement: Performance metrics, Requirement: Telemetry diagnostics, Requirement: Transition audit log, Requirements, Scenario: Metrics reflect arbitration behavior, Scenario: Telemetry renders its counters, Scenario: Transitions are journaled without blocking (+1 more)

### Community 547 - "ProbeSink"
Cohesion: 0.22
Nodes (6): ProbeSink, batch_status, batches, flush_status, flushes, suite_composite_injection()

### Community 548 - "SpanRing"
Cohesion: 0.19
Nodes (3): SpanRing, head_, slots_

### Community 549 - "PodPayload"
Cohesion: 0.22
Nodes (8): MethodRequest, id, MethodResponse, value, PodPayload, funky, x, y

### Community 550 - "Decisions"
Cohesion: 0.13
Nodes (14): D1 — Extend existing suites; no new executables, D2 — Settings loaders: valid + invalid per key, D3 — JSON writers: drive escaping through sinks, D4 — Sinks: success and failure paths, D5 — App branches, D6 — Remaining modules, D7 — Measurement loop, D8 — Documented unreachable exceptions (+6 more)

### Community 551 - "Iceoryx2TraceSinkConfig"
Cohesion: 0.10
Nodes (16): Iceoryx2TraceSinkConfig, app_id, service_name, subscriber_max_buffer_depth, std::formatter<nexigen::tracing::Iceoryx2TraceSinkConfig>, to_string(), Requirement: Sink configuration and diagnostics, Scenario: Configuration round-trips through JSON (+8 more)

### Community 552 - "Iceoryx2MetricSinkConfig"
Cohesion: 0.14
Nodes (14): Iceoryx2MetricSinkConfig, app_id, service_name, subscriber_max_buffer_size, std::formatter<nexigen::metrics::Iceoryx2MetricSinkConfig>, to_string(), Requirement: Iceoryx2 metric sink configuration, Scenario: Config diagnostics contract (+6 more)

### Community 553 - "StateAck"
Cohesion: 0.31
Nodes (4): StateAck, accepted, std::formatter<nexigen::exec::StateAck>, to_string()

### Community 554 - "FaultKind"
Cohesion: 0.15
Nodes (13): Entry, name, value, EnumTraits<nexigen::phm::FaultKind>, entries, FaultKind, AliveFailure, DeadlineFailure (+5 more)

### Community 555 - "prometheus_render_test.cpp"
Cohesion: 0.45
Nodes (10): contains(), make_frame(), render(), test_prometheus_counter(), test_prometheus_descriptive_statistics(), test_prometheus_gauge(), test_prometheus_histogram(), test_prometheus_overflow() (+2 more)

### Community 556 - "DroppedStats"
Cohesion: 0.31
Nodes (5): DroppedStats, buffer_full, rate_limited, slot_exhausted, to_string()

### Community 557 - "JsonFileTraceSinkConfig"
Cohesion: 0.10
Nodes (15): JsonFileTraceSinkConfig, app_name, max_spans_per_window, path, rotation, window_ns, std::formatter<nexigen::tracing::JsonFileTraceSinkConfig>, D3 — Metrics and tracing get their own settings module headers (+7 more)

### Community 558 - "span_frame_format.cpp"
Cohesion: 0.35
Nodes (12): force_inline_string(), from_json(), read_app_id(), read_attributes(), read_identity_numbers(), read_ids(), read_name(), read_source_numbers() (+4 more)

### Community 559 - "ADDED Requirements"
Cohesion: 0.12
Nodes (15): ADDED Requirements, Purpose, Requirement: Checkpoint reporting, Requirement: Function Group state read, Requirement: Health status reporting, Requirement: Supervised entity configuration, Requirement: Supervised entity diagnostics, Scenario: Application observes SafeState (+7 more)

### Community 560 - "provided_services"
Cohesion: 0.22
Nodes (9): provided_services, required_services, communication, history_size, max_subscribers, service_name, buffer_capacity, report_overflow_to_phm (+1 more)

### Community 561 - "phm_supervision"
Cohesion: 0.22
Nodes (9): max_duration_ms, source_checkpoint_id, target_checkpoint_id, alive_checkpoint_id, cycle_time_ms, deadline_supervision, max_margin_ms, recovery_action (+1 more)

### Community 562 - "processes"
Cohesion: 0.22
Nodes (9): ADAS_FG, MachineState, processes, app_id, args, dependencies, executable_path, function_groups (+1 more)

### Community 563 - "storage"
Cohesion: 0.22
Nodes (9): buffer_size_kb, max_interval_ms, storage, buffer_flush, default_min_level, enabled, file_prefix, format (+1 more)

### Community 564 - "crash_dump"
Cohesion: 0.22
Nodes (9): enabled, min_level, output_directory, ring_buffer_capacity, triggers, crash_dump, on_level, on_signal (+1 more)

### Community 565 - "descriptive_statistics"
Cohesion: 0.22
Nodes (9): compute_mean, compute_min_max, compute_stddev, zero_count_behavior, export_raw_buckets, target_quantiles, math_post_processing, descriptive_statistics (+1 more)

### Community 566 - ".emplace_logger"
Cohesion: 0.26
Nodes (3): fail(), main(), SimplePolicy

### Community 567 - "Высокоуровневые требования: Архитектура управления состояниями платформы Nexigen"
Cohesion: 0.22
Nodes (8): 1. Общие положения, 2.1. `nexigen::sm` — Управление состояниями системы (State Management), 2.2. `nexigen::exec` — Исполнительная часть (Execution Management), 2.3. `nexigen::phm` — Контроль здоровья платформы (Platform Health Management), 2. Требования к распределению ответственности по пространствам имен, 3. Схема взаимодействия подсистем (Sequence Diagram), 4. Требования к реализации межпроцессного взаимодействия (C++23 + iceoryx2), Высокоуровневые требования: Архитектура управления состояниями платформы Nexigen

### Community 568 - "Requirements"
Cohesion: 0.12
Nodes (15): phm/supervised-entity Specification, Purpose, Requirement: Checkpoint reporting, Requirement: Function Group state read, Requirement: Health status reporting, Requirement: Supervised entity configuration, Requirement: Supervised entity diagnostics, Requirements (+7 more)

### Community 569 - "Requirement: Runnable composite logger example"
Cohesion: 0.22
Nodes (8): ADDED Requirements, Requirement: Runnable composite logger example, Scenario: Both accessor forms address the same sink, Scenario: Composite diagnostics render, Scenario: Global level control fans out, Scenario: Heterogeneous sinks receive the same record, Scenario: Per-sink configuration stays independent, Spec Delta

### Community 570 - "Requirement: Runnable file logger example"
Cohesion: 0.22
Nodes (8): ADDED Requirements, Requirement: Runnable file logger example, Scenario: Affinity outcome is reported, Scenario: Cold-path read-back verification, Scenario: Configuration is inspectable, Scenario: Configured JSONL file is produced, Scenario: UTF-8 messages survive the pipeline, Spec Delta

### Community 571 - "ADDED Requirements"
Cohesion: 0.22
Nodes (8): ADDED Requirements, Requirement: ErrorCode name parsing, Requirement: std::formatter for ErrorCode, Scenario: Bogus name is rejected, Scenario: Error code formats by name, Scenario: Error code formats inside a log call, Scenario: Every enumerator name parses, Spec Delta

### Community 572 - "MODIFIED Requirements"
Cohesion: 0.22
Nodes (8): MODIFIED Requirements, Requirement: Documented example invocation, Requirement: Runnable example catalogue, Scenario: Example executables exist after a default build, Scenario: Examples are self-contained, Scenario: Reader can build and run an example from the README, Scenario: Stale duplicated build notes are removed, Spec Delta

### Community 573 - "ADDED Requirements"
Cohesion: 0.22
Nodes (8): ADDED Requirements, Purpose, Requirement: ex_stopwatch demonstrates loop tick budgeting, Requirement: ex_stopwatch is buildable, runnable, and a CTest smoke test, Scenario: Example runs to exit zero, Scenario: Example seeds its own data and needs no harness, Scenario: Tick budget run prints per-tick timing, Spec Delta

### Community 574 - "MODIFIED Requirements"
Cohesion: 0.22
Nodes (8): MODIFIED Requirements, Requirement: Documented example invocation, Requirement: Runnable example catalogue, Scenario: Example executables exist after a default build, Scenario: Examples are self-contained, Scenario: Reader can build and run an example from the README, Scenario: Stale duplicated build notes are removed, Spec Delta

### Community 575 - "MODIFIED Requirements"
Cohesion: 0.22
Nodes (8): MODIFIED Requirements, Requirement: Documented example invocation, Requirement: Runnable example catalogue, Scenario: Example executables exist after a default build, Scenario: Examples are self-contained, Scenario: Reader can build and run an example from the README, Scenario: Stale duplicated build notes are removed, Spec Delta

### Community 576 - "MODIFIED Requirements"
Cohesion: 0.22
Nodes (8): MODIFIED Requirements, Requirement: Documented example invocation, Requirement: Runnable example catalogue, Scenario: Example executables exist after a default build, Scenario: Examples are self-contained, Scenario: Reader can build and run an example from the README, Scenario: Stale duplicated build notes are removed, Spec Delta

### Community 577 - "MODIFIED Requirements"
Cohesion: 0.22
Nodes (8): MODIFIED Requirements, Requirement: Documented example invocation, Requirement: Runnable example catalogue, Scenario: Example executables exist after a default build, Scenario: Examples are self-contained, Scenario: Reader can build and run an example from the README, Scenario: Stale duplicated build notes are removed, Spec Delta

### Community 578 - "MODIFIED Requirements"
Cohesion: 0.22
Nodes (8): MODIFIED Requirements, Requirement: Documented example invocation, Requirement: Runnable example catalogue, Scenario: Example executables exist after a default build, Scenario: Examples are self-contained, Scenario: Reader can build and run an example from the README, Scenario: Stale duplicated build notes are removed, Spec Delta

### Community 579 - "Tasks"
Cohesion: 0.22
Nodes (8): 1. Hub model, 2. Discovery and inotify, 3. Streaming, 4. HTTP API and SSE, 5. Web UI, 6. CLI, build, and documentation, 7. Integration verification, Tasks

### Community 580 - "ADDED Requirements"
Cohesion: 0.22
Nodes (8): ADDED Requirements, Requirement: A failed exec fails the start visibly, Requirement: Setup handshake completes only after a successful exec, runner/process-supervision delta spec, Scenario: Nonexistent binary fails visibly without resource.json, Scenario: Redirected streams do not hide the failure, Scenario: Setup failures still fail the start before exec, Scenario: Successful exec observes EOF, not a pre-exec magic write

### Community 581 - "Requirement: Dynamic quota updates are race-free"
Cohesion: 0.22
Nodes (8): ADDED Requirements, logging/spam-protection delta spec, REMOVED Requirements, Requirement: Dynamic quota updates are race-free, Requirement: Dynamic quota updates are wait-free, Scenario: Boundary mixture of fields is tolerated, Scenario: Config change takes effect without stopping producers, Scenario: No torn quota read

### Community 582 - "Requirement: Child environment and CPU affinity are configurable"
Cohesion: 0.22
Nodes (8): MODIFIED Requirements, Requirement: Child environment and CPU affinity are configurable, Scenario: Application name is passed via argv, Scenario: CPU affinity restricts the child, Scenario: Environment entries are visible to the child, Scenario: Invalid CPU index is rejected before fork, Scenario: Runner identity takes precedence, Spec Delta

### Community 583 - "Requirement: Trace spans carry the application name"
Cohesion: 0.22
Nodes (8): ADDED Requirements, MODIFIED Requirements, Requirement: Sink configuration serialization, Requirement: Trace spans carry the application name, Scenario: Empty app name is omitted or empty, Scenario: Sink configs round-trip through JSON, Scenario: Span line carries the app field, Spec Delta

### Community 584 - "Requirement: Initialization follows a fixed order"
Cohesion: 0.22
Nodes (8): MODIFIED Requirements, Requirement: Initialization follows a fixed order, Scenario: Best-effort partial application does not trigger rollback, Scenario: Failure rolls back partial initialization, Scenario: Logger is ready before providers start, Scenario: Repeated initialization is idempotent, Scenario: Required worker-attribute failure starts no non-RT fallback, Spec Delta

### Community 585 - "Requirement: Canonical and legacy worker settings merge deterministically"
Cohesion: 0.22
Nodes (8): ADDED Requirements, MODIFIED Requirements, Requirement: Canonical and legacy worker settings merge deterministically, Scenario: Canonical fields override legacy fields in one source, Scenario: Console does not inherit legacy file affinity, Scenario: Explicit empty canonical CPUs clear affinity, Scenario: Legacy environment affinity remains compatible, Spec Delta

### Community 586 - "Requirement: LoggerOptions defines the engine configuration"
Cohesion: 0.22
Nodes (8): MODIFIED Requirements, Purpose, Requirement: LoggerOptions defines the engine configuration, Scenario: Default-constructed options carry documented defaults, Scenario: Poll interval is applied by the loggers, Scenario: `spsc_ring_buffer_capacity` rounds up to power of two, Scenario: `to_string` renders every option field, Spec Delta

### Community 587 - "CompositeTraceSink"
Cohesion: 0.15
Nodes (4): CompositeTraceSink, std::formatter<nexigen::tracing::CompositeTraceSink<Sinks...>>, to_json(), to_string()

### Community 588 - "check_result_value.py"
Cohesion: 0.20
Nodes (9): allowed(), blank_literals_and_comments(), find_in_text(), is_digit_separator(), main(), receiver_before(), scan(), self_test() (+1 more)

### Community 589 - "Tasks"
Cohesion: 0.22
Nodes (8): 1. Leaf enums (settings vocabulary), 2. settings_io, 3. shard_pool, 4. thread_management — leaf enums, 6. Single-entity confirmation, 7. Include-site updates, 8. Verification, Tasks

### Community 590 - "Decisions"
Cohesion: 0.13
Nodes (14): Actual thread inventory, Context, D1. Fixed 1024-bit `CpuSet`, not a dynamic bitset, D2. `ThreadSpec` is a fixed, immutable-at-start value, D3. Self-application with an explicit report and bounded rollback, D4. Startup publication is the only new shared state, D5. Integration does not alter worker algorithms, D8. Root-free deterministic verification (+6 more)

### Community 591 - "ADDED Requirements"
Cohesion: 0.22
Nodes (8): ADDED Requirements, Purpose, Requirement: Application state diagnostics, Requirement: Report application state, Scenario: Application reports running, Scenario: Reporter renders its state, Scenario: Unreported application stays starting, Spec Delta

### Community 592 - "MetricFrame"
Cohesion: 0.07
Nodes (23): MetricAggregator, slots_, used_, Slot, frame, used, MetricFrame, app_id (+15 more)

### Community 593 - "make_test_infra"
Cohesion: 0.26
Nodes (9): make_test_infra(), TestInfra, test_infrastructure_conflict(), test_infrastructure_failures(), test_infrastructure_lifecycle(), test_infrastructure_required_thread_rollback(), test_infrastructure_signals(), test_infrastructure_stop_and_mask() (+1 more)

### Community 594 - "exec/application-state Specification"
Cohesion: 0.22
Nodes (8): exec/application-state Specification, Purpose, Requirement: Application state diagnostics, Requirement: Report application state, Requirements, Scenario: Application reports running, Scenario: Reporter renders its state, Scenario: Unreported application stays starting

### Community 595 - "Requirement: JSON-lines file sink"
Cohesion: 0.20
Nodes (9): metrics/metric-sinks delta spec, MODIFIED Requirements, Requirement: JSON-lines file sink, Scenario: Batch becomes one JSON line, Scenario: Flush persists pending data, Scenario: JSON strings are escaped, Scenario: Multiple batches append, Scenario: Open and write failures surface (+1 more)

### Community 596 - "MODIFIED Requirements"
Cohesion: 0.14
Nodes (13): MODIFIED Requirements, Requirement: Chrono settings carry a sink configuration, Requirement: Invalid sink configuration degrades to best effort, Requirement: Merged settings apply to provider configuration, Scenario: Absent sink block leaves an unconfigured sink, Scenario: Empty path is accepted as unconfigured, Scenario: Non-object sink keeps the previous sink, Scenario: Provider config receives every mapped field (+5 more)

### Community 597 - "NullSink"
Cohesion: 0.19
Nodes (5): from_json(), NullSink, std::formatter<nexigen::chrono::NullSink>, to_json(), to_string()

### Community 598 - "Tasks"
Cohesion: 0.29
Nodes (6): 1. Dependency spike and build gate, 3. Publish-Subscribe + Event, 5. Manifest and telemetry, 6. Full gate, Tasks, Workflow follow-up

### Community 599 - "Decisions"
Cohesion: 0.13
Nodes (14): Context, D1. Reuse the existing sink configuration types as settings members, D2. The `sink` object is field-merged; empty `path` is the unconfigured sentinel, D3. Sink paths are file-only; environment carries identity, not output paths, D6. Resolution precedence is per signal, with no side effects, D7. Serialization follows the project-wide trio, with settings still one-way, D8. Integration point: resolve before `initialize`, then use the settings member, D9. Cold-path and concurrency impact (+6 more)

### Community 600 - "span_context_test.cpp"
Cohesion: 0.22
Nodes (12): test_attributes(), test_ids_and_context(), test_ids_and_context_alloc_free(), test_ids_and_context_concurrent(), test_ids_and_context_diagnostics(), test_ids_and_context_handoff(), test_ids_and_context_hex_json(), test_ids_and_context_sentinels() (+4 more)

### Community 601 - "include"
Cohesion: 0.13
Nodes (15): container, name, runs-on, strategy, timeout-minutes, image, options, build_type (+7 more)

### Community 602 - "TriggerResponse"
Cohesion: 0.36
Nodes (4): std::formatter<nexigen::sm::TriggerResponse>, to_string(), TriggerResponse, result

### Community 603 - "example_of_log_daemon_config.yaml"
Cohesion: 0.25
Nodes (7): daemon_name, sinks, throttling, burst_limit, enabled, max_messages_per_sec_per_context, version

### Community 604 - "network"
Cohesion: 0.25
Nodes (8): enabled, listen_port, control_server, default_min_level, enabled, endpoint, protocol, network

### Community 605 - "Спецификация требований к демону логирования (LogDaemon)"
Cohesion: 0.25
Nodes (7): 1. Введение и область применения, 2. Жизненный цикл и системное окружение (Lifecycle & Infrastructure), 3. Маршрутизация и раздельная фильтрация по приемникам (Sink-Specific Filtering), 4. Сохранение на диск и кольцевая ротация (Storage Management), 5. Сетевая трансляция и динамическое управление (Network Sinks & Control), 6. Конфигурация (LogDaemon Manifest), Спецификация требований к демону логирования (LogDaemon)

### Community 606 - "example_of_metrics_daemon_config.yaml"
Cohesion: 0.25
Nodes (7): daemon_name, resources, bg_cpu_affinity, max_memory_mb, priority_nice, scheduling_policy, version

### Community 607 - "os_telemetry"
Cohesion: 0.25
Nodes (8): cgroup_root, enabled, network_interfaces, proc_fs_path, thermal_zones, os_telemetry, name, path

### Community 608 - "Спецификация требований к демону сбора метрик и мониторинга ресурсов (MetricsDaemon)"
Cohesion: 0.25
Nodes (7): 1. Введение и область применения, 2. Жизненный цикл и изоляция ресурсов (Lifecycle & CPU Affinity), 3. Механизм чтения, сброса и математической постобработки (Scraping & Post-Processing), 4. Контроль квот и интеграция с PHM (Resource Watchdog & Alerting), 5. Интерфейсы экспорта и хранения (Exporters & Persistence), 6. Конфигурация (MetricsDaemon Manifest), Спецификация требований к демону сбора метрик и мониторинга ресурсов (MetricsDaemon)

### Community 609 - "Спецификация требований к демону трассировки (TraceDaemon)"
Cohesion: 0.25
Nodes (7): 1. Введение и область применения, 2. Жизненный цикл и изоляция ресурсов (Lifecycle & CPU Affinity), 3. Режимы работы и сбор данных (Collection Modes), 4. Интеграция с трассировкой ядра ОС (Kernel & Scheduling Trace), 5. Формат экспорта и визуализация (Perfetto Serialization), 6. Конфигурация (TraceDaemon Manifest), Спецификация требований к демону трассировки (TraceDaemon)

### Community 610 - "run_suite"
Cohesion: 0.29
Nodes (7): run_app_settings_suites(), run_suite(), run_app_command_line_suites(), run_app_infrastructure_suites(), main(), run_app_stop_reason_suites(), run_app_telemetry_layout_suites()

### Community 611 - "Requirement: Runnable console logger example"
Cohesion: 0.25
Nodes (7): ADDED Requirements, Requirement: Runnable console logger example, Scenario: Console diagnostics render, Scenario: Levels format directly, Scenario: Multi-threaded console logging completes, Scenario: Runtime level filtering is visible, Spec Delta

### Community 612 - "HubSuite"
Cohesion: 0.16
Nodes (10): HubSuite, apps, artifacts_path, execution_manager_log, reason, state, suite_dir, suite_id (+2 more)

### Community 613 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 614 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 615 - "Requirement: SpscShardRing overflow policies"
Cohesion: 0.25
Nodes (7): ADDED Requirements, Purpose, Requirement: SpscShardRing overflow policies, Scenario: DropNewest never blocks the producer, Scenario: Overflow policy is configurable at runtime, Scenario: OverwriteOldest is TSan-clean, Spec Delta

### Community 616 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 617 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 618 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 619 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 620 - "load_logging_settings"
Cohesion: 0.08
Nodes (23): 1. Settings vocabulary and diagnostics, 2. Global/local loader and merge, 3. Apply helpers and logger initialization, 4. Size-based file rotation, 5. Example, documentation and integration, Tasks, Workflow follow-up, Capabilities (+15 more)

### Community 621 - "Settings"
Cohesion: 0.15
Nodes (11): Settings, diagnostics, global_loaded, local_loaded, max_threads_set, message_capacity_set, options, ring_capacity_set (+3 more)

### Community 622 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 623 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 624 - "Requirement: Settings diagnostic records a finding"
Cohesion: 0.15
Nodes (12): ADDED Requirements, Purpose, Requirement: Settings diagnostic records a finding, Requirement: Settings source enumerates the merge layers, Requirement: Settings vocabulary is cold path only, Scenario: Diagnostic renders source, field, and message, Scenario: Diagnostic round-trips through JSON, Scenario: Hostile JSON leaves the diagnostic unchanged (+4 more)

### Community 625 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 626 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 627 - "Tasks"
Cohesion: 0.25
Nodes (7): 1. Coverage above 95%, 2. Coverage gate, 3. clang-tidy, 4. Documentation, 5. Verification, Tasks, Workflow follow-up

### Community 628 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 629 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 630 - "Requirement: Fixed file names produce the four settings pairs"
Cohesion: 0.17
Nodes (11): MODIFIED Requirements, Requirement: CLI log level overrides loaded settings, Requirement: Command-line contract is documented, Requirement: Fixed file names produce the four settings pairs, Scenario: Absent CLI level preserves the loaded value, Scenario: Absent directory disables that source, Scenario: Both directories produce eight paths, Scenario: CLI level wins over files and environment (+3 more)

### Community 631 - "Requirement: State write failures are reported"
Cohesion: 0.25
Nodes (7): ADDED Requirements, Requirement: State write failures are reported, Scenario: Failed instance exit write is recorded, Scenario: Failed suite exit write is recorded, Scenario: Failed suite state write is recorded, Scenario: Successful writes stay quiet, Spec Delta

### Community 632 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 633 - "Tasks"
Cohesion: 0.25
Nodes (7): 1. Fuzzable HTTP parser, 2. Fault injection, 3. Analyzer depth, 4. Documentation, 5. Verification, Tasks, Workflow follow-up

### Community 634 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 635 - "Requirements"
Cohesion: 0.12
Nodes (16): Purpose, Requirement: Complexity of decomposed functions stays within the project target, Requirement: Refactored internals preserve all observable behavior, Requirement: `start_suite` is split into validation, conflict check, and supervisor launch, Requirement: `stop_suite` is split into request, wait, and finalize phases, Requirement: `suite_status` is split into metadata/state loading and per-app merging, Requirements, runner/app-suite — internal structure (delta) (+8 more)

### Community 636 - "Requirement: Settings sources merge per application"
Cohesion: 0.25
Nodes (8): Requirement: Settings sources merge per application, Scenario: Application global path replaces the suite path, Scenario: Invalid settings values fail the launch, Scenario: Local path is resolved from the manifest directory, Scenario: Local settings override global settings, Scenario: Missing settings files keep defaults, Scenario: Suite global settings apply by default, Scenario: Unreadable settings file fails the launch

### Community 637 - "Requirement: Suite manifest is validated before launch"
Cohesion: 0.25
Nodes (8): Requirement: Suite manifest is validated before launch, Scenario: CPU index outside the online set is rejected, Scenario: Duplicate application identity is rejected, Scenario: Invalid settings paths are rejected, Scenario: Unknown impact value is rejected, Scenario: Unknown log level is rejected, Scenario: Unsafe identity or relative run directory is rejected, Scenario: Validation failure starts nothing

### Community 638 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 639 - "Tasks"
Cohesion: 0.25
Nodes (7): 1. Runner logging facade and global settings, 2. Runner affinity and child isolation, 3. CLI logging, 4. Suite supervisor logging, 5. Documentation, 6. Integration verification, Tasks

### Community 640 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 641 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 642 - "Requirement: Optional worker logger"
Cohesion: 0.25
Nodes (7): chrono/chrono-provider delta spec, MODIFIED Requirements, Requirement: Optional worker logger, Scenario: Clock log level serializes, Scenario: Double start with a logger is rejected without rebinding, Scenario: Worker logs through the bound logger, Scenario: Worker runs without a logger

### Community 643 - "Requirement: Thread-local slot registration"
Cohesion: 0.25
Nodes (7): logging/multi-producer delta spec, MODIFIED Requirements, Requirement: Thread-local slot registration, Scenario: Registration happens once per thread, Scenario: Steady-state path never contends, Scenario: TLS cache exhaustion drops without burning registration slots, Scenario: Two threads get two distinct shards

### Community 644 - "Requirement: Suite manifest has a fixed schema"
Cohesion: 0.25
Nodes (7): MODIFIED Requirements, Requirement: Suite manifest has a fixed schema, Scenario: Complete manifest is accepted, Scenario: Optional fields default deterministically, Scenario: Runner name is optional, Scenario: Unknown fields are ignored, Spec Delta

### Community 645 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 646 - "ADDED Requirements"
Cohesion: 0.17
Nodes (11): ADDED Requirements, Purpose, Requirement: Fault storage, Requirement: Local process recovery, Requirement: Recovery diagnostics, Requirement: SafeState escalation, Scenario: Critical failure escalates to SafeState, Scenario: Expired process is restarted (+3 more)

### Community 647 - "int32_t"
Cohesion: 0.23
Nodes (6): boundary_master(), bucket_lower_edge(), floor_div_pow2(), MeterPasskey, D10 — Enabler 4: exponential coarsening loss fix (scope addition, user-approved), 1. Module build enablers and example scaffolding

### Community 648 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 649 - "Tasks"
Cohesion: 0.25
Nodes (7): 1. Baseline and scaffolding, 2. Extract JSON enum validators, 3. Recompose `json_enum_fields_valid`, 4. Extend unit tests, 5. Verification and quality gates, Tasks, Workflow follow-up

### Community 650 - "SuiteManifest"
Cohesion: 0.11
Nodes (45): AppSpec::to_string(), attempt_dir_path(), from_json(), FunctionGroupConfig::to_string(), invalid_argument(), is_valid_log_level(), json_enum_fields_valid(), load_suite_manifest() (+37 more)

### Community 651 - "ExponentialSlot"
Cohesion: 0.15
Nodes (11): ExponentialSlot, active, bank, seq, D1 — Shared field vocabulary as free functions in `include/nexigen/core/detail/json_field.hpp`, D2 — Range-checked unsigned reads replace unchecked `get<T>()`, D5 — Daemon entry points: per-tool staging, no new shared runner, D6 — Gate widening and CTest wiring (+3 more)

### Community 652 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 653 - "MeterPasskey"
Cohesion: 0.28
Nodes (3): MeterPasskey, std::formatter<nexigen::metrics::MeterPasskey>, to_string()

### Community 654 - "LinesSink"
Cohesion: 0.09
Nodes (24): test_composite_fanout(), test_console(), basename_of(), count_char(), EnvGuard, had, name, saved (+16 more)

### Community 655 - "nexigen-cpp — анализ кода"
Cohesion: 0.17
Nodes (10): 1. Общие сведения, 2. Архитектура, 5. Код-смеллы, 6. Тесты: пробелы, коррелирующие с багами, 7. Гигиена репозитория, 8. Общая оценка и приоритетные рекомендации, nexigen-cpp — анализ кода, Приложение A. Оценка по модулям (+2 more)

### Community 656 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 657 - "Tasks"
Cohesion: 0.25
Nodes (7): 1. Vocabulary, 2. Core WaitSet entities, 3. Error mapping, 4. WaitSet behavior tests, 5. Gates, Tasks, test_waitset()

### Community 658 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 659 - "ADDED Requirements"
Cohesion: 0.25
Nodes (7): ADDED Requirements, Requirement: Function Group configuration, Requirement: Function Group diagnostics, Scenario: Group switch starts its processes, Scenario: Group switch stops its processes, Scenario: Groups render their states, Spec Delta

### Community 660 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 661 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 662 - "ADDED Requirements"
Cohesion: 0.25
Nodes (7): ADDED Requirements, Purpose, Requirement: PHM fault-event interface, Requirement: Recovery diagnostics, Scenario: Fault event drives SafeState, Scenario: Recovery renders its last fault, Spec Delta

### Community 663 - "Requirement: JsonFileTraceSink JSONL output"
Cohesion: 0.22
Nodes (8): MODIFIED Requirements, Requirement: JsonFileTraceSink JSONL output, Scenario: Attribute values serialize by their packed type, Scenario: One parseable JSON object per span per line, Scenario: Open and write failures are visible, Scenario: Scratch overflow discards the line and counts it, Scenario: Timestamps render in UTC ISO-8601 shape, tracing/trace-sinks delta spec

### Community 664 - "Tasks"
Cohesion: 0.25
Nodes (7): 1. LogFrame type, 2. Iceoryx2LoggerConfig, 3. Sink + logger, 4. Tests, 5. Verification matrix, Tasks, Workflow follow-up

### Community 665 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 666 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 667 - "rules"
Cohesion: 0.25
Nodes (7): context, rules, design, proposal, specs, tasks, schema

### Community 668 - "Requirement: Settings diagnostic records a finding"
Cohesion: 0.15
Nodes (12): core/settings-vocabulary Specification, Purpose, Requirement: Settings diagnostic records a finding, Requirement: Settings source enumerates the merge layers, Requirement: Settings vocabulary is cold path only, Requirements, Scenario: Diagnostic renders source, field, and message, Scenario: Diagnostic round-trips through JSON (+4 more)

### Community 669 - "Requirement: Affinity failures are non-fatal warnings"
Cohesion: 0.25
Nodes (8): Requirement: Affinity failures are non-fatal warnings, Scenario: Applications are not affected by a failed pin, Scenario: Invalid control CPU is ignored, Scenario: Invalid logger CPU is ignored, Scenario: Kernel rejection is ignored, Scenario: Partial application is reported accurately, Scenario: Required control or logger failure aborts initialization, Scenario: Scheduler rejection is reported by attribute

### Community 670 - "check_naming.py"
Cohesion: 0.26
Nodes (7): blank_literals_and_comments(), find_in_text(), is_digit_separator(), main(), scan(), self_test(), tracked_sources()

### Community 671 - "Requirement: HTTP listener is loopback by default"
Cohesion: 0.25
Nodes (8): Requirement: HTTP listener is loopback by default, Scenario: Default listener is localhost, Scenario: Full-dual-stack wildcard binds both families, Scenario: Invalid address fails startup, Scenario: IPv6 address forms bind AF_INET6, Scenario: Listen override is honored, Scenario: Loopback classification does not truncate IPv6 literals, Scenario: Non-loopback bind warns

### Community 672 - "Requirement: Malformed requests are rejected consistently"
Cohesion: 0.25
Nodes (8): Requirement: Malformed requests are rejected consistently, Scenario: HTTP version is unsupported, Scenario: Invalid limit and cursor values are ignored, Scenario: Invalid percent escape is treated literally, Scenario: Request line exceeds its limit, Scenario: Request line has no target, Scenario: Unsupported method does not change state, Scenario: URL cannot bypass the request-line limit

### Community 673 - "Requirements"
Cohesion: 0.25
Nodes (7): Purpose, Requirement: PHM fault-event interface, Requirement: Recovery diagnostics, Requirements, Scenario: Fault event drives SafeState, Scenario: Recovery renders its last fault, sm/recovery-handler Specification

### Community 674 - "StressResult"
Cohesion: 0.18
Nodes (11): fill_payload(), Payload, field, StressResult, consumer_retries, order_violations, pop_sum, producer_retries (+3 more)

### Community 675 - "coverage_report.py"
Cohesion: 0.28
Nodes (4): clean_counters(), collect_line_counts(), main(), summarize()

### Community 676 - "SpanRecord"
Cohesion: 0.04
Nodes (45): TimelineSink, SpanRecord, attribute_count, attributes, column, end, file_name, flag_ended_explicit (+37 more)

### Community 677 - "app_suite.cpp"
Cohesion: 0.04
Nodes (115): file_exists(), monotonic_now_ns(), process_alive(), sleep_ms(), read_json_into(), Requirement: Suite supervisor loop uses an explicit application state machine, Scenario: Abnormal Failure-impact app triggers suite restart, Scenario: App restart timer expires (+107 more)

### Community 678 - "Deferred: отложенные работы платформы (не забыть доделать)"
Cohesion: 0.17
Nodes (7): Deferred: отложенные работы платформы (не забыть доделать), EM — продакшн-харденинг (отложено до этапа production hardening), SM — event-нотификация смены состояния (future), SM — интеграция с UCM (отложено до появления Update Manager), Контекст аудита EM (2026-11-10), Манифесты — YAML-вход (отложено из-за mandate 12), Переименование под SRS-номенклатуру (planned)

### Community 679 - "Settings"
Cohesion: 0.18
Nodes (10): Settings, app, app_len, base_dir, base_len, custom_ns, file_name, file_name_len (+2 more)

### Community 680 - "RotationConfig"
Cohesion: 0.38
Nodes (4): RotationConfig, custom_ns, kind, to_string()

### Community 681 - "custom_metric_rules"
Cohesion: 0.29
Nodes (7): app_id, critical_threshold, metric_name, phm_Fault_id, statistic, warning_threshold, custom_metric_rules

### Community 682 - "disk_flight_recorder"
Cohesion: 0.29
Nodes (7): enabled, file_format, flush_interval_sec, flush_on_safestate, max_total_storage_mb, storage_directory, disk_flight_recorder

### Community 683 - ".shutdown"
Cohesion: 0.06
Nodes (37): Context, D1 — New module and dependency direction, D2 — CRTP contract and member order, D3 — Initialization order and rollback, D5 — SIGINT/SIGTERM via signalfd watcher, D6 — Shutdown order and idempotency, D7 — Settings model and loaders, Decisions (+29 more)

### Community 684 - "Proposal"
Cohesion: 0.29
Nodes (6): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, Why

### Community 685 - "Requirement: Suite manifest is validated before launch"
Cohesion: 0.22
Nodes (9): Requirement: Suite manifest is validated before launch, Scenario: CPU above 63 is not rejected merely for its width, Scenario: CPU index outside the online set is rejected, Scenario: Duplicate application identity is rejected, Scenario: Invalid settings paths are rejected, Scenario: Unknown impact value is rejected, Scenario: Unknown log level is rejected, Scenario: Unsafe identity or relative run directory is rejected (+1 more)

### Community 686 - "app_settings.cpp"
Cohesion: 0.28
Nodes (4): AppSettingsPaths::to_string(), load_app_settings(), Settings::to_string(), to_json()

### Community 687 - "Requirement: Runnable simple settings example"
Cohesion: 0.29
Nodes (6): ADDED Requirements, Requirement: Runnable simple settings example, Scenario: Example creates its own inputs, Scenario: Failures are visible, Scenario: Logger initializes from the merged files, Spec Delta

### Community 688 - "Tasks"
Cohesion: 0.29
Nodes (6): 1. Simple settings example, 2. Build and CTest registration, 3. Documentation, 4. Verification matrix, Tasks, Workflow follow-up

### Community 689 - "Requirement: CTest smoke coverage for every example"
Cohesion: 0.29
Nodes (6): MODIFIED Requirements, Requirement: CTest smoke coverage for every example, Scenario: Argument-driven smoke variants run under ctest, Scenario: Disabled examples register no tests, Scenario: Examples run under ctest, Spec Delta

### Community 690 - "Tasks"
Cohesion: 0.29
Nodes (6): 1. Fix the coverage target, 2. Add CI, 3. Documentation, 4. Verification, Tasks, Workflow follow-up

### Community 691 - "Requirement: Manifest launch settings reach each child"
Cohesion: 0.29
Nodes (7): Requirement: Manifest launch settings reach each child, Scenario: Arguments are passed exactly, Scenario: CPU affinity is restricted, Scenario: Environment additions are visible to the child, Scenario: Identity variables take precedence, Scenario: Idle timeout terminates the application, Scenario: Rejected affinity is a launch failure

### Community 692 - "Requirement: Suite status reports aggregate and application state"
Cohesion: 0.29
Nodes (7): Requirement: Suite status reports aggregate and application state, Scenario: Degraded and failed suites are distinguishable, Scenario: JSON output matches text output, Scenario: Running suite reports every application, Scenario: Status is independent of the query route, Scenario: Stopped suite reports stopped, Scenario: Unknown suite has no record

### Community 693 - "Proposal"
Cohesion: 0.29
Nodes (6): Capabilities, Impact, Modified Capabilities, Proposal, What Changes, Why

### Community 694 - "Requirement: Suite runner log path follows the runner layout"
Cohesion: 0.29
Nodes (6): ADDED Requirements, Requirement: Suite runner log path follows the runner layout, Scenario: Missing suite id keeps the runner log unset, Scenario: Runner log resolves under the suite artifact directory, Scenario: Runner log stream is discoverable and openable, Spec Delta

### Community 695 - "Proposal"
Cohesion: 0.29
Nodes (6): Capabilities, Impact, Modified Capabilities, Proposal, What Changes, Why

### Community 696 - "Proposal"
Cohesion: 0.29
Nodes (6): Capabilities, Impact, Modified Capabilities, Proposal, What Changes, Why

### Community 697 - "Requirement: Embedded script parses and bootstraps"
Cohesion: 0.29
Nodes (6): ADDED Requirements, Requirement: Embedded script parses and bootstraps, Scenario: Connection badge leaves the initial state, Scenario: Initial tree is fetched and rendered, Scenario: Script parses without error, Spec Delta

### Community 698 - "Proposal"
Cohesion: 0.29
Nodes (6): Capabilities, Impact, Modified Capabilities, Proposal, What Changes, Why

### Community 699 - "metric_frame_test.cpp"
Cohesion: 0.17
Nodes (10): suite_iceoryx2_metric_sink_config(), suite_metric_daemon_config(), test_metric_daemon_config(), test_metric_daemon_config_roundtrip(), test_metric_daemon_config_validation(), suite_metric_frame(), test_metric_frame(), test_metric_frame_diagnostics() (+2 more)

### Community 700 - "Requirement: Stateless infrastructure policy"
Cohesion: 0.29
Nodes (6): MODIFIED Requirements, Requirement: Stateless infrastructure policy, Scenario: Components outlive the providers, Scenario: Logger and sinks receive the application name, Scenario: Policy types are validated at compile time, Spec Delta

### Community 701 - "Requirement: Application name is app-level"
Cohesion: 0.29
Nodes (6): ADDED Requirements, Requirement: Application name is app-level, Scenario: App name is set on the aggregate, Scenario: App name renders and serializes, Scenario: Empty app name is allowed, Spec Delta

### Community 702 - "Decisions"
Cohesion: 0.14
Nodes (13): Context, D1 — One binary, role and timing via environment (not CLI tokens), D2 — Settings via `--global-settings <instance_dir>/settings`, D3 — Three-role manifest with concrete timings, D4 — App structure mirrors `ex_app_cli_settings`, D5 — Exit path per role, D6 — Script-generated manifest (no machine-specific absolute paths), D7 — No new concurrent/atomic component (+5 more)

### Community 703 - "Tasks"
Cohesion: 0.10
Nodes (17): D7. Runner applies, never computes, Capabilities, Modified Capabilities, New Capabilities, Proposal, What Changes, Why, 1. Core CPU and thread vocabulary (+9 more)

### Community 704 - "Requirement: LoggerOptions defines the engine configuration"
Cohesion: 0.22
Nodes (8): MODIFIED Requirements, Requirement: LoggerOptions defines the engine configuration, Scenario: Default-constructed options carry documented defaults, Scenario: Poll interval is applied by the loggers, Scenario: `spsc_ring_buffer_capacity` rounds up to power of two, Scenario: `to_string` renders every option field, Scenario: Worker thread JSON and diagnostics round-trip, Spec Delta

### Community 705 - "WelfordState"
Cohesion: 0.24
Nodes (9): population_std(), welford_add(), welford_merge(), WelfordState, count, m2, max, mean (+1 more)

### Community 706 - "ExponentialBank"
Cohesion: 0.20
Nodes (9): ExponentialBank, base_index, buckets, count, effective_scale, maximum, minimum, sum (+1 more)

### Community 707 - "cpp-project-analizer/analyzer.py"
Cohesion: 0.26
Nodes (5): apply_cloc(), bucket_for(), new_metrics(), run_analysis(), run_cloc()

### Community 708 - "Requirement: Runtime level control"
Cohesion: 0.29
Nodes (6): MODIFIED Requirements, Requirement: Runtime level control, Scenario: Level lowered mid-run admits more, Scenario: Level raised mid-run filters more, Scenario: Reads and writes need no synchronization primitives, Spec Delta

### Community 709 - "TerminatedBy"
Cohesion: 0.13
Nodes (14): Entry, name, value, EnumTraits<nexigen::exec::TerminatedBy>, entries, from_string(), TerminatedBy, ExecutionManager (+6 more)

### Community 710 - "Design"
Cohesion: 0.29
Nodes (6): Context, Decisions, Design, Goals / Non-Goals, Open Questions, Risks / Trade-offs

### Community 711 - ".bump"
Cohesion: 0.11
Nodes (14): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why, 1. Logging: flush barrier crosses no ring ownership (P0-1) (+6 more)

### Community 712 - "Tasks"
Cohesion: 0.18
Nodes (10): 1. Build scaffolding and the `RateLimited` error code, 2. `log_level.hpp` + level control + the concept, 3. `log_record.hpp` + the shared hot-path pipeline, 4. `null_logger.hpp` and `console_logger.hpp`, 5. FileLogger cold units (`src/logging/`), 6. `file_logger.hpp` assembly, 7. `composite_logger.hpp` + subsystem documentation, 8. Cross-mode integration verification (+2 more)

### Community 713 - "spam_protection_config.cpp"
Cohesion: 0.36
Nodes (6): from_json(), read_bool_field(), read_log_level_field(), read_u32_field(), SpamProtectionConfig::to_string(), to_json()

### Community 714 - "Requirement: Manifest launch settings reach each child"
Cohesion: 0.29
Nodes (7): Requirement: Manifest launch settings reach each child, Scenario: Arguments are passed exactly, Scenario: CPU affinity is restricted, Scenario: Environment additions are visible to the child, Scenario: Identity variables take precedence, Scenario: Idle timeout terminates the application, Scenario: Rejected affinity is a launch failure

### Community 715 - "Requirement: Suite status reports aggregate and application state"
Cohesion: 0.29
Nodes (7): Requirement: Suite status reports aggregate and application state, Scenario: Degraded and failed suites are distinguishable, Scenario: JSON output matches text output, Scenario: Running suite reports every application, Scenario: Status is independent of the query route, Scenario: Stopped suite reports stopped, Scenario: Unknown suite has no record

### Community 716 - "Requirements"
Cohesion: 0.29
Nodes (6): execution_manager/logging Specification, Purpose, Requirement: ExecutionManager logging is cold path only, Requirements, Scenario: Application hot paths are untouched, Scenario: Only execution_manager processes write the execution_manager log

### Community 717 - "Requirement: Live updates use Server-Sent Events"
Cohesion: 0.29
Nodes (7): Requirement: Live updates use Server-Sent Events, Scenario: All streams are available, Scenario: Heartbeat keeps the connection alive, Scenario: Replay then live, Scenario: Slow client is closed, Scenario: SSE headers are correct, Scenario: Tree snapshots are streamed

### Community 718 - "Design"
Cohesion: 0.20
Nodes (9): D2 — Render client-side in the existing viewer, D3 — Dispatch on the metric `kind`, D4 — Keep the raw JSON fallback, Decisions, Design, Goals / Non-Goals, Migration Plan, Open Questions (+1 more)

### Community 719 - "Decisions"
Cohesion: 0.18
Nodes (10): 1. Validator decomposition, 2. Keep validators in the anonymous namespace, 3. Test through the public orchestrator, 4. Error handling stays uniform, 5. CPU online count is fetched once, Decisions, Design, Goals / Non-Goals (+2 more)

### Community 720 - "validate.sh"
Cohesion: 0.57
Nodes (6): ccache_report(), cpp_changed_files(), gate_filtered(), validate.sh script, timed(), usage()

### Community 721 - "producer_test.cpp"
Cohesion: 0.16
Nodes (16): 4. Test decomposition (P4), PerThreadOrder, count, last, order_ok, tid, rendered_value(), suite_producer() (+8 more)

### Community 722 - "Requirement: Metric batches carry the application name"
Cohesion: 0.29
Nodes (6): ADDED Requirements, MODIFIED Requirements, Requirement: Metric batches carry the application name, Scenario: Batch line carries the app field, Scenario: Empty app name is omitted or empty, Spec Delta

### Community 723 - "Requirement: Provider kind setting"
Cohesion: 0.18
Nodes (10): ADDED Requirements, MODIFIED Requirements, Requirement: Manual kind records ignored worker fields, Requirement: Provider kind setting, Scenario: Default kind is daemon, Scenario: Environment overrides do not touch the kind, Scenario: Invalid kind keeps the previous value, Scenario: Local file overrides global kind (+2 more)

### Community 725 - "BoundedSink"
Cohesion: 0.33
Nodes (4): BoundedSink, left, p, spill

### Community 727 - "resources"
Cohesion: 0.33
Nodes (6): resources, background_cpu_affinity, max_memory_mb, priority, scheduling_policy, worker_cpu_affinity

### Community 728 - "resources"
Cohesion: 0.33
Nodes (6): max_write_bytes_per_sec, resources, bg_cpu_affinity, io_throttle, priority_nice, scheduling_policy

### Community 729 - "Tasks"
Cohesion: 0.08
Nodes (21): TracerProvider<ShardCapacity, MaxThreads>::flush(), TracerProvider<ShardCapacity, MaxThreads>::initialize(), 1. `safe_id` — extract the character predicate, 2. `FileLogger::initialize` — validation predicates, 3. `Infrastructure::initialize` — construction phases, 4. `TracerProvider::consume_loop` — worker phases, Context, Decisions (+13 more)

### Community 730 - "ThreadSpec"
Cohesion: 0.13
Nodes (32): apply_affinity(), apply_name(), apply_scheduler(), from_json(), from_string(), native_policy(), parse_cpu_set_json(), parse_cpus() (+24 more)

### Community 731 - "ExponentialMerge"
Cohesion: 0.20
Nodes (10): ExponentialMerge, base, buckets, empty, maximum, minimum, scale, sum (+2 more)

### Community 732 - "Requirement: Civil time diagnostic string"
Cohesion: 0.33
Nodes (5): ADDED Requirements, Requirement: Civil time diagnostic string, Scenario: Decomposition renders fully, Scenario: Free helper matches member, Spec Delta

### Community 733 - "Requirement: Timestamp diagnostic string"
Cohesion: 0.33
Nodes (5): ADDED Requirements, Requirement: Timestamp diagnostic string, Scenario: Member and free forms agree, Scenario: Value appears in the text, Spec Delta

### Community 734 - "Why"
Cohesion: 0.08
Nodes (22): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why, 1. Baseline (+14 more)

### Community 735 - "Requirement: Tracing settings map every provider field"
Cohesion: 0.20
Nodes (9): ADDED Requirements, MODIFIED Requirements, Requirement: Tracing settings map every provider field, Requirement: Worker thread settings preserve legacy affinity compatibility, Scenario: Canonical worker fields merge across sources, Scenario: Legacy environment override remains effective, Scenario: Rate budget defaults to unlimited, Scenario: Tracing fields load from the global file (+1 more)

### Community 736 - "Requirement: Monotonic nanosecond stamping"
Cohesion: 0.33
Nodes (5): MODIFIED Requirements, Requirement: Monotonic nanosecond stamping, Scenario: Stamps are non-decreasing per producer, Scenario: Stamps reflect real elapsed time, Spec Delta

### Community 737 - "SuiteMetadata"
Cohesion: 0.03
Nodes (52): SuiteAppMetadata, app, instance, instance_dir, stop_grace_s, to_string(), SuiteAppStatus, app (+44 more)

### Community 738 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 739 - "Decisions"
Cohesion: 0.18
Nodes (10): Context, D1 — Bulk reformat with the pinned version instead of aligning tooling to the tree, D2 — Accept the expanded `NX_CHECK` macro in `log_daemon_test.cpp`, D3 — Accept the formatter's string-literal splitting, D4 — `.git-blame-ignore-revs` lands in a second commit, D5 — Verification order, Decisions, Design (+2 more)

### Community 740 - ".prepare"
Cohesion: 0.39
Nodes (8): Requirement: Calibration gates issuance, Scenario: Calibration failure is surfaced and retrievable, Scenario: Issuance before prepare is rejected, Scenario: Prepared provider issues calibrated sources, Requirement: Calibration gates issuance, Scenario: Calibration failure is surfaced and retrievable, Scenario: Issuance before prepare is rejected, Scenario: Prepared provider issues calibrated sources

### Community 741 - "Tasks"
Cohesion: 0.33
Nodes (5): 1. Drop visibility implementation, 2. Tests, 3. Validation, Tasks, Workflow follow-up

### Community 742 - "Tasks"
Cohesion: 0.33
Nodes (5): 1. Pin nlohmann::json to v3.12.0, 2. Actualize documentation figures, 3. Verification, Tasks, Workflow follow-up

### Community 743 - ".collect_now"
Cohesion: 0.04
Nodes (52): Context, D11 — Enabler 5: registration-vs-harvest publication fix (scope addition, user-approved), D1 — Four-example decomposition (supersedes the planned two), D3 — Enabler 2: composite construction from configured members, D4 — TSan-safe harvesting strategy per example, D5 — Self-verification and exit codes, D6 — CMake and CTest wiring, D7 — Robust output path and CI-safe affinity (+44 more)

### Community 744 - "Entry"
Cohesion: 0.22
Nodes (7): Entry, name, value, EnumTraits<nexigen::core::LogLevel>, entries, from_string(), to_string()

### Community 745 - "TelemetryPaths"
Cohesion: 0.24
Nodes (6): resolve_telemetry_paths(), TelemetryPaths, chrono, metrics, traces, to_string()

### Community 746 - "StateStorage<ConcurrencyPolicy::MultiThreaded, Reset>"
Cohesion: 0.36
Nodes (3): StateStorage<ConcurrencyPolicy::MultiThreaded, Reset>, shard_, write_lock_

### Community 747 - "Tasks"
Cohesion: 0.33
Nodes (5): 1. Failure-demo mode, 2. Trace-shape self-verification, 3. Help smoke test and integration, Tasks, Workflow follow-up

### Community 748 - "Requirement: Effective log level applies to each application"
Cohesion: 0.33
Nodes (6): Requirement: Effective log level applies to each application, Scenario: Absent level preserves settings, Scenario: Application level overrides the suite, Scenario: Effective level beats settings and environment, Scenario: Generated logging settings carry the level, Scenario: Suite level applies by default

### Community 749 - "Requirement: Failure impact restarts the whole suite"
Cohesion: 0.33
Nodes (6): Requirement: Failure impact restarts the whole suite, Scenario: Exhausted suite budget fails the suite, Scenario: Failure application crash restarts every application, Scenario: Failure application ignores its own retry budget, Scenario: Suite restart budget is shared, Scenario: Suite restart creates new attempt files for every application

### Community 750 - "Requirement: Restarts follow abnormal termination only"
Cohesion: 0.33
Nodes (6): Requirement: Restarts follow abnormal termination only, Scenario: Abnormal exit triggers a delayed retry, Scenario: Exhausted per-application retries mark the application failed, Scenario: Normal exit does not restart, Scenario: Operator stop does not restart, Scenario: Retries create new artifact files

### Community 751 - "Requirement: Manifest configures runner thread affinity"
Cohesion: 0.33
Nodes (6): Requirement: Manifest configures runner thread affinity, Scenario: Applications keep their own affinity, Scenario: Control thread is pinned, Scenario: Empty lists mean no pinning, Scenario: Logger falls back to global affinity, Scenario: Logger worker is pinned

### Community 752 - "Requirement: Runner level follows manifest, then global settings"
Cohesion: 0.33
Nodes (6): Requirement: Runner level follows manifest, then global settings, Scenario: Application level does not affect the runner, Scenario: Explicit suite level wins, Scenario: Global level applies when the manifest omits it, Scenario: Missing settings fall through to info, Scenario: Suite directory uses recorded global settings

### Community 753 - "Proposal"
Cohesion: 0.33
Nodes (5): Capabilities, Impact, Proposal, What Changes, Why

### Community 754 - "Requirement: Meter publication is serialized with harvesting"
Cohesion: 0.33
Nodes (5): ADDED Requirements, metrics/metric-provider delta spec, Requirement: Meter publication is serialized with harvesting, Scenario: Concurrent identical get_meter hands out one meter, Scenario: get_meter races the harvester

### Community 755 - "View"
Cohesion: 0.22
Nodes (8): View, base_index, buckets, count, effective_scale, maximum, minimum, sum

### Community 756 - "Proposal"
Cohesion: 0.33
Nodes (5): Capabilities, Impact, Proposal, What Changes, Why

### Community 757 - "Proposal"
Cohesion: 0.33
Nodes (5): Capabilities, Impact, Proposal, What Changes, Why

### Community 758 - "Proposal"
Cohesion: 0.33
Nodes (5): Capabilities, Impact, Proposal, What Changes, Why

### Community 759 - "Proposal"
Cohesion: 0.33
Nodes (5): Capabilities, Impact, Proposal, What Changes, Why

### Community 760 - "Entry"
Cohesion: 0.22
Nodes (7): Entry, name, value, EnumTraits<nexigen::com::Error>, entries, from_string(), to_string()

### Community 761 - "Requirement: SafeState priority"
Cohesion: 0.33
Nodes (5): ADDED Requirements, Requirement: SafeState priority, Scenario: Ordinary transitions proceed afterwards, Scenario: SafeState overtakes queued transitions, Spec Delta

### Community 762 - "Design"
Cohesion: 0.22
Nodes (7): Context, Decisions, Design, Goals / Non-Goals, Migration Plan, Open Questions, Risks / Trade-offs

### Community 763 - "composite_logger_test.cpp"
Cohesion: 0.13
Nodes (17): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why, CompConsole (+9 more)

### Community 764 - "Requirement: Restarts follow abnormal termination only"
Cohesion: 0.33
Nodes (6): Requirement: Restarts follow abnormal termination only, Scenario: Abnormal exit triggers a delayed retry, Scenario: Exhausted per-application retries mark the application failed, Scenario: Normal exit does not restart, Scenario: Operator stop does not restart, Scenario: Retries create new artifact files

### Community 765 - "Requirement: ExecutionManager level follows manifest, then global settings"
Cohesion: 0.33
Nodes (6): Requirement: ExecutionManager level follows manifest, then global settings, Scenario: Application level does not affect the execution_manager, Scenario: Explicit suite level wins, Scenario: Global level applies when the manifest omits it, Scenario: Missing settings fall through to info, Scenario: Suite directory uses recorded global settings

### Community 766 - "Requirement: Global logging settings supply operational fields"
Cohesion: 0.33
Nodes (6): Requirement: Global logging settings supply operational fields, Scenario: ExecutionManager identity is not overridden by the file, Scenario: Invalid global file is non-fatal, Scenario: Legacy global affinity remains readable, Scenario: Missing global file keeps execution_manager defaults, Scenario: Operational fields are applied

### Community 767 - "Requirement: Manifest configures execution_manager thread affinity"
Cohesion: 0.33
Nodes (6): Requirement: Manifest configures execution_manager thread affinity, Scenario: Applications keep their own affinity, Scenario: Control thread is pinned, Scenario: Empty lists mean no pinning, Scenario: Logger falls back to global affinity, Scenario: Logger worker is pinned

### Community 768 - "Requirement: Read-only JSON endpoints"
Cohesion: 0.33
Nodes (6): Requirement: Read-only JSON endpoints, Scenario: Health reports readiness, Scenario: Recent records are limited, Scenario: Stream list is returned, Scenario: Tree is returned, Scenario: Unknown stream is not found

### Community 769 - "Requirement: Request headers are matched case-insensitively"
Cohesion: 0.33
Nodes (6): Requirement: Request headers are matched case-insensitively, Scenario: A similarly named header is not used, Scenario: Header whitespace is tolerated, Scenario: Invalid cursor is ignored, Scenario: Lowercase header resumes the stream, Scenario: Mixed-case header resumes the stream

### Community 770 - "Tasks"
Cohesion: 0.22
Nodes (8): 2. App settings loader — +85 lines, 3. JSON writers — +38 lines, 4. Metric and trace sinks — +56 lines, 5. App lifecycle branches — +10 `app.hpp` lines, 6. Remaining module gaps — +~95 lines, 7. Coverage gate, docs and matrix, Tasks, Workflow follow-up

### Community 771 - "ADDED Requirements"
Cohesion: 0.25
Nodes (7): ADDED Requirements, Purpose, Requirement: OverflowPolicy enum controls full-ring behavior, Requirement: Runtime reconfiguration is wait-free, Scenario: Overflow policy values are serializable, Scenario: Runtime reconfiguration never blocks producers, Spec Delta

### Community 772 - "Requirement: Manifest and config deserialization uses a shared field vocabulary"
Cohesion: 0.20
Nodes (9): ADDED Requirements, Requirement: Field-extraction helpers are non-throwing and cold-path only, Requirement: Manifest and config deserialization uses a shared field vocabulary, Scenario: Missing field leaves the destination untouched, Scenario: Object and array composition keep the current semantics, Scenario: Out-of-range unsigned value is rejected without terminating, Scenario: Vocabulary is cold-path only, Scenario: Wrong-typed field is ignored (+1 more)

### Community 773 - "execution_manager_logging.cpp"
Cohesion: 0.07
Nodes (46): 1. Surface the apply report, 2. Report-based diagnostics at call sites, 3. Tests and validation, Tasks, 1. Directory relocation, 2. Namespace and string sweep, 3. Build system, 4. Spec capability relocation (+38 more)

### Community 774 - "Tasks"
Cohesion: 0.29
Nodes (6): 1. Build-system accelerators, 2. Test-cycle unlock, 3. Validation entrypoint, 4. Measurement and acceptance, 5. Wrap-up, Tasks

### Community 775 - "Tasks"
Cohesion: 0.25
Nodes (7): 1. Module scaffold and build target, 2. StopwatchState enum and serialization contract, 4. ScopedStopwatch RAII guard, 5. Runnable example and README, 6. Full verification matrix, Tasks, Workflow follow-up

### Community 777 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 780 - "security"
Cohesion: 0.40
Nodes (5): security, capabilities, group, sha256_checksum, user

### Community 781 - "context_overrides"
Cohesion: 0.50
Nodes (5): app_id, context_id, min_level, context_overrides, context_overrides

### Community 782 - "Decisions"
Cohesion: 0.20
Nodes (9): Context, D1 — Exclusion via one `--not-match-d` regex, not a per-directory list, D2 — `lans=["cpp"]` with a compatibility fallback, D3 — Filter `alignas` by identifier, mirroring the gate, D4 — Verify by cross-check, not by trusting the change, Decisions, Design, Goals / Non-Goals (+1 more)

### Community 783 - "Infrastructure"
Cohesion: 0.07
Nodes (27): Infrastructure, chrono_logger_bridge_, chrono_sink_, initialized_, metrics_sink_, previous_mask_, provider_, shutdown_started_ (+19 more)

### Community 784 - ".user_header"
Cohesion: 0.06
Nodes (34): current_trace_context(), Context, Decisions, Design, Goals / Non-Goals, Risks / Trade-offs, Capabilities, Impact (+26 more)

### Community 785 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 786 - "StateStorage<ConcurrencyPolicy::SingleThreaded, Reset>"
Cohesion: 0.38
Nodes (4): StateCell, value, StateStorage<ConcurrencyPolicy::SingleThreaded, Reset>, cell_

### Community 787 - "Design"
Cohesion: 0.40
Nodes (4): Context, Design, Goals / Non-Goals, Risks / Trade-offs

### Community 788 - "Requirement: Degrade and not-impact levels bound the damage"
Cohesion: 0.40
Nodes (5): Requirement: Degrade and not-impact levels bound the damage, Scenario: Degrade exhaustion degrades the suite, Scenario: Neither level restarts the suite, Scenario: Not-impact exhaustion leaves the suite running, Scenario: Not-impact retries never change suite state

### Community 789 - "Requirement: Affinity failures are non-fatal warnings"
Cohesion: 0.40
Nodes (5): Requirement: Affinity failures are non-fatal warnings, Scenario: Applications are not affected by a failed pin, Scenario: Invalid control CPU is ignored, Scenario: Invalid logger CPU is ignored, Scenario: Kernel rejection is ignored

### Community 790 - "Requirement: CLI commands record their outcome"
Cohesion: 0.40
Nodes (5): Requirement: CLI commands record their outcome, Scenario: Command failure is recorded, Scenario: Stop and status are recorded, Scenario: Successful single-instance start is recorded, Scenario: Successful suite start is recorded

### Community 791 - "Requirement: Global logging settings supply operational fields"
Cohesion: 0.40
Nodes (5): Requirement: Global logging settings supply operational fields, Scenario: Invalid global file is non-fatal, Scenario: Missing global file keeps runner defaults, Scenario: Operational fields are applied, Scenario: Runner identity is not overridden by the file

### Community 792 - "Requirement: Log location follows the manifest and suite metadata"
Cohesion: 0.40
Nodes (5): Requirement: Log location follows the manifest and suite metadata, Scenario: Missing manifest uses the working-directory fallback, Scenario: Suite directory selects the recorded artifact path, Scenario: Unreadable suite metadata uses the fallback, Scenario: Valid manifest selects the suite artifact directory

### Community 793 - "Requirement: Suite supervisor records lifecycle and policy events"
Cohesion: 0.40
Nodes (5): Requirement: Suite supervisor records lifecycle and policy events, Scenario: Application exit is recorded, Scenario: Retry and failure decisions are recorded, Scenario: Stop and forced stop are recorded, Scenario: Whole-suite restart is recorded

### Community 794 - "Decisions"
Cohesion: 0.20
Nodes (9): Context, D1 — Move only `to_json`/`from_json`, keep free `to_string` inline, D2 — One `.cpp` per entity, named after its header, D3 — Declarations in the header carry the attributes; definitions are plain, D4 — Symbol check expects weak-to-strong promotion, not new API, Decisions, Design, Goals / Non-Goals (+1 more)

### Community 795 - "Tasks"
Cohesion: 0.40
Nodes (4): 1. Frame the SSE body as chunked, 2. Pin the contract and verify, Tasks, Workflow follow-up

### Community 796 - "Tasks"
Cohesion: 0.40
Nodes (4): 1. Extract the shared parser, 2. Verify, Tasks, Workflow follow-up

### Community 797 - "Tasks"
Cohesion: 0.40
Nodes (4): 1. Shared machinery, 2. Sweep deletions, 3. Verification, Tasks

### Community 798 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 799 - "MODIFIED Requirements"
Cohesion: 0.40
Nodes (4): MODIFIED Requirements, Requirement: Complexity of decomposed functions stays within the project target, Scenario: Post-refactor lizard report, Spec Delta

### Community 800 - "SupervisedEntity"
Cohesion: 0.07
Nodes (20): std::formatter<nexigen::phm::SupervisedEntity>, SupervisedEntity, checkpoint_, group_listener_, group_name_, health_, last_checkpoint_, last_health_ (+12 more)

### Community 801 - "MODIFIED Requirements"
Cohesion: 0.40
Nodes (4): MODIFIED Requirements, Requirement: SafeState escalation, Scenario: Critical failure escalates to SafeState, Spec Delta

### Community 802 - "Design"
Cohesion: 0.40
Nodes (4): Context, Decisions, Design, Goals / Non-Goals

### Community 803 - "Design"
Cohesion: 0.25
Nodes (7): D1 — Fix the off-by-one and make the destination size explicit, Decisions, Design, Goals / Non-Goals, Migration Plan, Open Questions, Risks / Trade-offs

### Community 804 - "Approach"
Cohesion: 0.18
Nodes (9): Approach, Context, design: refactor-high-complexity-functions, hub/http-api, Key Decisions, Open Questions, Risks and Mitigations, runner/app-suite (+1 more)

### Community 805 - "Requirement: CLI commands record their outcome"
Cohesion: 0.40
Nodes (5): Requirement: CLI commands record their outcome, Scenario: Command failure is recorded, Scenario: Stop and status are recorded, Scenario: Successful single-instance start is recorded, Scenario: Successful suite start is recorded

### Community 806 - "Requirement: Log location follows the manifest and suite metadata"
Cohesion: 0.40
Nodes (5): Requirement: Log location follows the manifest and suite metadata, Scenario: Missing manifest uses the working-directory fallback, Scenario: Suite directory selects the recorded artifact path, Scenario: Unreadable suite metadata uses the fallback, Scenario: Valid manifest selects the suite artifact directory

### Community 807 - "Requirement: State write failures are reported"
Cohesion: 0.40
Nodes (5): Requirement: State write failures are reported, Scenario: Failed instance exit write is recorded, Scenario: Failed suite exit write is recorded, Scenario: Failed suite state write is recorded, Scenario: Successful writes stay quiet

### Community 808 - "Requirement: Suite supervisor records lifecycle and policy events"
Cohesion: 0.40
Nodes (5): Requirement: Suite supervisor records lifecycle and policy events, Scenario: Application exit is recorded, Scenario: Retry and failure decisions are recorded, Scenario: Stop and forced stop are recorded, Scenario: Whole-suite restart is recorded

### Community 809 - "Modified Capabilities"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 810 - "Requirement: HTTP parsing and errors are bounded"
Cohesion: 0.40
Nodes (5): Requirement: HTTP parsing and errors are bounded, Scenario: Keep-alive is supported, Scenario: Oversized request is rejected, Scenario: Unknown path is not found, Scenario: Unsupported method is rejected

### Community 811 - "OtherPayload"
Cohesion: 0.40
Nodes (4): OtherPayload, a, b, c

### Community 812 - "FanoutConfiguredSink"
Cohesion: 0.40
Nodes (3): FanoutConfiguredSink, configured, moved_from_marker

### Community 813 - "Tasks"
Cohesion: 0.20
Nodes (9): 1. Baseline and scaffolding, 2. Decompose `sanitize_document`, 3. Decompose `apply_environment`, 4. Decompose `logger_options_format.cpp` `from_json` overloads, 5. Extend unit tests, 6. Verification and quality gates, Tasks, Workflow follow-up (+1 more)

### Community 814 - "operator new"
Cohesion: 0.60
Nodes (3): arena_exhausted(), operator delete(), operator new()

### Community 815 - "operator new"
Cohesion: 0.60
Nodes (3): arena_exhausted(), operator delete(), operator new()

### Community 816 - "operator new"
Cohesion: 0.60
Nodes (3): arena_exhausted(), operator delete(), operator new()

### Community 817 - "run_suite_demo.sh"
Cohesion: 0.83
Nodes (3): find_app(), find_runner(), run_suite_demo.sh script

### Community 818 - "Design"
Cohesion: 0.22
Nodes (8): Context, D1 — Rename to PascalCase, do not add `// NX_ALLOW_NAMING` opt-outs, D2 — Fix the result-value finding by renaming the test local, not by opting out, D3 — Rename in place, one file per commit-sized step, Decisions, Design, Goals / Non-Goals, Risks / Trade-offs

### Community 819 - "Design"
Cohesion: 0.25
Nodes (7): 4. `nexigen-hub` `main` — options struct + setup helpers, Context, Decisions, Design, Goals / Non-Goals, Migration Plan, Open Questions

### Community 820 - "Requirement: Failure impact restarts the whole suite"
Cohesion: 0.33
Nodes (6): Requirement: Failure impact restarts the whole suite, Scenario: Exhausted suite budget fails the suite, Scenario: Failure application crash restarts every application, Scenario: Failure application ignores its own retry budget, Scenario: Suite restart budget is shared, Scenario: Suite restart creates new attempt files for every application

### Community 821 - "Decisions"
Cohesion: 0.20
Nodes (9): Context, D1 — Extract steps as free functions in the file's anonymous namespace, D2 — Preserve lifetime boundaries exactly, D3 — `main` stays a numbered narrative, D4 — Verification is output equality, not code review, Decisions, Design, Goals / Non-Goals (+1 more)

### Community 822 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 823 - "lifecycle"
Cohesion: 0.50
Nodes (4): restart_attempts, shutdown_timeout_ms, startup_timeout_ms, lifecycle

### Community 824 - "ipc_iceoryx2"
Cohesion: 0.50
Nodes (4): ipc_iceoryx2, batch_timeout_ms, event_channel, service_name

### Community 825 - "rotation"
Cohesion: 0.50
Nodes (4): compression, max_archive_files, max_file_size_mb, rotation

### Community 826 - "StopArgs"
Cohesion: 0.25
Nodes (7): StopArgs, config, grace_s, grace_set, have_dir, instance_dir, suite_dir

### Community 827 - "Design"
Cohesion: 0.22
Nodes (8): Context, D1 — Separator rule: apostrophe between two hex digits, D2 — Self-test inside each script, one CTest entry, D3 — Rename the two revealed declarations in this change, Decisions, Design, Goals / Non-Goals, Risks / Trade-offs

### Community 828 - "Design"
Cohesion: 0.22
Nodes (8): Context, D1 — Policy change plus targeted rejoin, not a mechanical "join everything", D2 — Rejoin by merging raw literal bodies, then reformat, D3 — Prove the option actually does the work, Decisions, Design, Goals / Non-Goals, Risks / Trade-offs

### Community 829 - "Design"
Cohesion: 0.22
Nodes (8): Context, D1 — Commit the graphify plugin, ignore the session analyzer, D2 — Commit `initial_spec/` as provenance, D3 — Ignore the leftovers with anchored, root-scoped patterns, Decisions, Design, Goals / Non-Goals, Risks / Trade-offs

### Community 830 - "Requirement: Each attempt writes to a new artifact directory"
Cohesion: 0.50
Nodes (4): Requirement: Each attempt writes to a new artifact directory, Scenario: Control files stay in the instance directory, Scenario: First attempt uses attempt-0001, Scenario: Restart creates the next attempt directory

### Community 831 - "Requirement: Generated settings point at the attempt artifacts"
Cohesion: 0.50
Nodes (4): Requirement: Generated settings point at the attempt artifacts, Scenario: External sink paths are overridden, Scenario: Four settings files are written, Scenario: Sink paths point into the attempt directory

### Community 832 - "Requirement: Suite launch is detached"
Cohesion: 0.50
Nodes (4): Requirement: Suite launch is detached, Scenario: A per-application launch failure does not fail start, Scenario: Manifest or supervisor failure fails start, Scenario: Start returns while applications keep running

### Community 833 - "Requirement: Suite manifest has a fixed schema"
Cohesion: 0.50
Nodes (4): Requirement: Suite manifest has a fixed schema, Scenario: Complete manifest is accepted, Scenario: Optional fields default deterministically, Scenario: Unknown fields are ignored

### Community 834 - "Requirement: Suite metadata lives in a deterministic suite directory"
Cohesion: 0.50
Nodes (4): Requirement: Suite metadata lives in a deterministic suite directory, Scenario: Another process can operate from the suite directory, Scenario: Metadata writes are atomic, Scenario: Suite directory is created on start

### Community 835 - "Requirement: Suite types follow the serialization contract"
Cohesion: 0.50
Nodes (4): Requirement: Suite types follow the serialization contract, Scenario: Hostile suite JSON is ignored, Scenario: Impact and state enums round-trip through text and JSON, Scenario: Manifest and status structures round-trip

### Community 836 - "Requirement: Logger lifecycle is fork-safe"
Cohesion: 0.50
Nodes (4): Requirement: Logger lifecycle is fork-safe, Scenario: Final records survive process exit, Scenario: Pre-restart records are durable, Scenario: Restart does not inherit a logger thread

### Community 837 - "Requirement: Logging is best-effort"
Cohesion: 0.50
Nodes (4): Requirement: Logging is best-effort, Scenario: Both locations fail without changing outcome, Scenario: Runtime write failure does not stop supervision, Scenario: Unwritable primary path falls back

### Community 838 - "Requirement: Runner diagnostics use the project file logger"
Cohesion: 0.50
Nodes (4): Requirement: Runner diagnostics use the project file logger, Scenario: Application logs stay separate, Scenario: Records are append-only, Scenario: Suite run writes runner.jsonl

### Community 839 - "logging/spsc-shard-ring delta spec"
Cohesion: 0.50
Nodes (3): logging/spsc-shard-ring delta spec, REMOVED Requirements, Requirement: SpscShardRing overflow policies

### Community 840 - "Tasks"
Cohesion: 0.50
Nodes (3): 2. Migrate loaders (wire/diagnostic-identical), 3. Gates and verification, Tasks

### Community 841 - "ConsoleLogger"
Cohesion: 0.07
Nodes (6): ConsoleLogger, producer_, worker_, to_string(), from_json(), to_json()

### Community 843 - "Boxed"
Cohesion: 0.33
Nodes (7): Boxed, counter, diagnostics, path, read_counter(), read_path(), write_counter()

### Community 844 - "Requirement: Long and short option forms are supported"
Cohesion: 0.33
Nodes (6): Requirement: Long and short option forms are supported, Scenario: Long options accept both value forms, Scenario: Multi-character short names are recognized, Scenario: Program name is not parsed, Scenario: Short options accept all value forms, Scenario: Single-letter aliases are rejected

### Community 845 - "Requirement: LoggerOptions defines the engine configuration"
Cohesion: 0.29
Nodes (6): MODIFIED Requirements, Requirement: LoggerOptions defines the engine configuration, Scenario: Default-constructed options carry documented defaults, Scenario: `spsc_ring_buffer_capacity` rounds up to power of two, Scenario: `to_string` renders every option field, Spec Delta

### Community 846 - "run_daemon"
Cohesion: 0.29
Nodes (6): load_config(), main(), Options, config_path, parse_args(), run_daemon()

### Community 847 - "Design"
Cohesion: 0.20
Nodes (9): Context, D1 — Drop summaries are synthesized by the worker, never pushed into foreign rings, D2 — Runner handshake: EOF means exec succeeded, Decisions, Design, Goals / Non-Goals, Migration Plan, Open Questions (+1 more)

### Community 848 - "Requirement: Degrade and not-impact levels bound the damage"
Cohesion: 0.40
Nodes (5): Requirement: Degrade and not-impact levels bound the damage, Scenario: Degrade exhaustion degrades the suite, Scenario: Neither level restarts the suite, Scenario: Not-impact exhaustion leaves the suite running, Scenario: Not-impact retries never change suite state

### Community 849 - "Requirement: ExecutionManager diagnostics use the project file logger"
Cohesion: 0.50
Nodes (4): Requirement: ExecutionManager diagnostics use the project file logger, Scenario: Application logs stay separate, Scenario: Records are append-only, Scenario: Suite run writes execution_manager.jsonl

### Community 850 - "Requirement: Logger lifecycle is fork-safe"
Cohesion: 0.50
Nodes (4): Requirement: Logger lifecycle is fork-safe, Scenario: Final records survive process exit, Scenario: Pre-restart records are durable, Scenario: Restart does not inherit a logger thread

### Community 851 - "Requirement: Logging is best-effort"
Cohesion: 0.50
Nodes (4): Requirement: Logging is best-effort, Scenario: Both locations fail without changing outcome, Scenario: Runtime write failure does not stop supervision, Scenario: Unwritable primary path falls back

### Community 852 - "Requirement: Suite metadata preserves execution_manager affinity"
Cohesion: 0.50
Nodes (4): Requirement: Suite metadata preserves execution_manager affinity, Scenario: Legacy suite metadata remains readable, Scenario: Missing execution_manager lists mean no pinning, Scenario: Suite directory reproduces affinity

### Community 853 - "Requirement: Control-plane refresh failures are reported"
Cohesion: 0.50
Nodes (4): Requirement: Control-plane refresh failures are reported, Scenario: A failing refresh does not stop serving, Scenario: Rescan failure is reported, Scenario: Stream sync failure is reported

### Community 854 - "Requirement: Web assets are served by the binary"
Cohesion: 0.50
Nodes (4): Requirement: Web assets are served by the binary, Scenario: Assets are served, Scenario: Assets work offline, Scenario: Index is served

### Community 855 - "Spec Delta"
Cohesion: 0.29
Nodes (6): MODIFIED Requirements, Purpose, Requirement: File logger vocabulary diagnostic strings, Scenario: Config dump is complete, Scenario: Drop snapshot renders, Spec Delta

### Community 856 - "Tasks"
Cohesion: 0.25
Nodes (7): 1. File relocation, 2. Identifier sweep (design D2 map), 3. Spec text update, 4. Grep gate, 5. Full gate, Tasks, Workflow follow-up

### Community 857 - "PodPayload"
Cohesion: 0.50
Nodes (3): PodPayload, x, y

### Community 858 - "Why"
Cohesion: 0.29
Nodes (6): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, Why

### Community 860 - "Tasks"
Cohesion: 0.25
Nodes (7): 1. Metrics and tracing sink settings, 2. Chrono sink settings, 4. Example and documentation, 5. Full verification matrix, Follow-up (not part of this change), Tasks, Workflow follow-up

### Community 861 - "Entry"
Cohesion: 0.33
Nodes (5): Entry, name, value, EnumTraits<nexigen::com::CallbackProgression>, entries

### Community 863 - "Requirement: At least one settings directory is required"
Cohesion: 0.40
Nodes (5): Requirement: At least one settings directory is required, Scenario: Documented flow terminates on the missing-directory error, Scenario: Neither directory is supplied, Scenario: Only the global directory is supplied, Scenario: Only the local directory is supplied

### Community 864 - "Requirement: Command-line types are serializable"
Cohesion: 0.40
Nodes (5): Requirement: Command-line types are serializable, Scenario: Diagnostics and errors round-trip through JSON, Scenario: Every type renders through to_string and formatter, Scenario: Non-object JSON is ignored, Scenario: Options round-trip through JSON

### Community 865 - "Requirement: Global and local directory shapes are validated"
Cohesion: 0.40
Nodes (5): Requirement: Global and local directory shapes are validated, Scenario: Absolute local directory is rejected, Scenario: Empty directory value is rejected, Scenario: Relative global directory is rejected, Scenario: Valid directory pair is accepted

### Community 866 - "Requirement: Client read logic is layered"
Cohesion: 0.67
Nodes (3): Requirement: Client read logic is layered, Scenario: Large request line is detected before routing, Scenario: Multiple pipelined requests are processed in order

### Community 867 - "Requirement: `HttpServer::Impl::start` is split into setup phases"
Cohesion: 0.67
Nodes (3): Requirement: `HttpServer::Impl::start` is split into setup phases, Scenario: Epoll setup failure is reported cleanly, Scenario: IPv6 dual-stack binding is isolated

### Community 868 - "Requirement: The API is observation-only"
Cohesion: 0.67
Nodes (3): Requirement: The API is observation-only, Scenario: No control endpoint exists, Scenario: State files are not modified

### Community 904 - "Requirement: Unknown, missing, repeated, and positional arguments are handled"
Cohesion: 0.40
Nodes (5): Requirement: Unknown, missing, repeated, and positional arguments are handled, Scenario: Missing value is rejected, Scenario: Positional argument is rejected, Scenario: Repeated option keeps the last value, Scenario: Unknown option is rejected

### Community 906 - "Proposal"
Cohesion: 0.29
Nodes (6): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, Why

### Community 907 - "SuiteLaunchPlan"
Cohesion: 0.05
Nodes (32): SuiteLaunchPlan, artifact_dir, command, identity, options, Concurrency / memory layout, D1 — Scenario resolution, D2 — Process orchestration (+24 more)

### Community 908 - "Proposal"
Cohesion: 0.25
Nodes (7): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes, Why

### Community 909 - "Design"
Cohesion: 0.15
Nodes (12): EnumTraits, 2. Concepts and configuration, Context, Decisions, Design, Goals / Non-Goals, Migration Plan, Open Questions (+4 more)

### Community 910 - "Requirement: Fixed file names produce the four settings pairs"
Cohesion: 0.50
Nodes (4): Requirement: Fixed file names produce the four settings pairs, Scenario: Absent directory disables that source, Scenario: Both directories produce eight paths, Scenario: Trailing slash is normalized

### Community 911 - "Requirement: Help short-circuits with success"
Cohesion: 0.50
Nodes (4): Requirement: Help short-circuits with success, Scenario: Help alone succeeds, Scenario: Help wins over invalid arguments, Scenario: Usage text lists the contract

### Community 912 - "Requirement: Log level accepts canonical enumerator names"
Cohesion: 0.50
Nodes (4): Requirement: Log level accepts canonical enumerator names, Scenario: Every canonical level is accepted, Scenario: Omitted level records no override, Scenario: Unknown level is rejected

### Community 913 - "proposal: refactor-high-complexity-functions"
Cohesion: 0.40
Nodes (4): Capabilities, proposal: refactor-high-complexity-functions, Success Criteria, Why

### Community 914 - "jobs"
Cohesion: 0.14
Nodes (14): name, runs-on, timeout-minutes, container, name, runs-on, timeout-minutes, name (+6 more)

### Community 916 - "Requirement: CPU-pinned background consumer"
Cohesion: 0.25
Nodes (8): Requirement: CPU-pinned background consumer, Scenario: CPU above 63 is supported, Scenario: Idle consumer wake-ups are bounded by the ceiling, Scenario: Per-shard FIFO and fair drain hold, Scenario: Pin failures are visible without stopping the pipeline, Scenario: Producers never wait on the worker, Scenario: Required failure rolls back initialization, Scenario: Worker affinity matches the configured mask

### Community 917 - "Design"
Cohesion: 0.29
Nodes (6): Context, Design, Goals / Non-Goals, Migration Plan, Open Questions, Risks / Trade-offs

### Community 918 - "Requirement: Settings sources merge per application"
Cohesion: 0.25
Nodes (8): Requirement: Settings sources merge per application, Scenario: Application global path replaces the suite path, Scenario: Invalid settings values fail the launch, Scenario: Local path is resolved from the manifest directory, Scenario: Local settings override global settings, Scenario: Missing settings files keep defaults, Scenario: Suite global settings apply by default, Scenario: Unreadable settings file fails the launch

### Community 919 - "Requirement: Child environment and CPU affinity are configurable"
Cohesion: 0.25
Nodes (8): Requirement: Child environment and CPU affinity are configurable, Scenario: Application name is passed via argv, Scenario: CPU affinity restricts the child, Scenario: CPUs above 63 reach the child unchanged, Scenario: Empty CPU set retains existing reset behavior, Scenario: Environment entries are visible to the child, Scenario: ExecutionManager identity takes precedence, Scenario: Invalid CPU index is rejected before fork

### Community 920 - "TracerSlot"
Cohesion: 0.67
Nodes (3): TracerSlot, name, occupied

### Community 921 - "Proposal"
Cohesion: 0.33
Nodes (5): Capabilities, Modified Capabilities, New Capabilities, Proposal, Why

### Community 922 - "Tasks"
Cohesion: 0.29
Nodes (6): 3. Client library, 4. Supervision engine, 6. Daemon, 7. Behavior tests, 8. Gates, Tasks

### Community 923 - "Requirement: Option parsing honors the terminator"
Cohesion: 0.67
Nodes (3): Requirement: Option parsing honors the terminator, Scenario: A bare terminator is accepted, Scenario: Arguments after the terminator are positional

### Community 924 - "apply_thread_spec_self"
Cohesion: 0.10
Nodes (22): apply_thread_spec_self(), Context, D1 — `serialization_mandate.hpp` kept intact, D2 — `thread_management.hpp` becomes a free-function utility, Decisions, Design, Risks / Trade-offs, Target file tree (~20 headers) (+14 more)

### Community 926 - "Tasks"
Cohesion: 0.29
Nodes (6): 1. Baseline and shared JSON vocabulary, 3. Daemon entry points (P3), 5. Gate widening and wiring, 6. Integration verification, Tasks, Workflow follow-up

### Community 934 - "Proposal"
Cohesion: 0.29
Nodes (6): Capabilities, Impact, Modified Capabilities, New Capabilities, Proposal, What Changes

### Community 935 - "Requirement: Size-based file rotation"
Cohesion: 0.29
Nodes (7): Requirement: Size-based file rotation, Scenario: Flush covers the active segment, Scenario: Limit reached starts the next segment, Scenario: Pre-existing content counts toward the limit, Scenario: Producers observe no size-rotation hitch, Scenario: Segment naming preserves the extension, Scenario: Zero disables size-based rotation

### Community 937 - "Requirement: JSON-lines file sink"
Cohesion: 0.29
Nodes (7): Requirement: JSON-lines file sink, Scenario: Batch becomes one JSON line, Scenario: Flush persists pending data, Scenario: JSON strings are escaped, Scenario: Multiple batches append, Scenario: Open and write failures surface, Scenario: State records serialize their value

### Community 938 - "Requirement: Log level accepts canonical enumerator names"
Cohesion: 0.29
Nodes (6): MODIFIED Requirements, Requirement: Log level accepts canonical enumerator names, Scenario: Every canonical level is accepted, Scenario: Omitted level records no override, Scenario: Unknown level is rejected, Spec Delta

### Community 939 - "ArtifactCounts"
Cohesion: 0.33
Nodes (6): ArtifactCounts, chrono_snapshots, metric_batches, parent_edges, span_kinds, trace_ids

### Community 940 - "FormatWithLocation"
Cohesion: 0.33
Nodes (3): FormatWithLocation, fmt, loc

### Community 941 - "Entry"
Cohesion: 0.33
Nodes (5): Entry, name, value, EnumTraits<ErrorCode>, entries

### Community 944 - "JsonFileTraceSink::write_record_line"
Cohesion: 0.19
Nodes (8): append_padded(), ChromeTraceSink::to_string(), ChromeTraceSinkConfig::to_string(), JsonFileTraceSink::to_string(), JsonFileTraceSink::window_filename(), JsonFileTraceSink::write_record_line(), JsonFileTraceSinkConfig::to_string(), to_string()

### Community 945 - "StatusArgs"
Cohesion: 0.17
Nodes (11): Capabilities, Impact, Proposal, What Changes, Why, StatusArgs, config, have_dir (+3 more)

### Community 947 - "Capabilities"
Cohesion: 0.67
Nodes (3): Capabilities, Modified Capabilities, New Capabilities

### Community 948 - "clang"
Cohesion: 0.40
Nodes (5): container, name, runs-on, timeout-minutes, clang

### Community 949 - "Tasks"
Cohesion: 0.29
Nodes (6): 1. Apply and prove the reformat, 2. Verify no behavioral change, 3. Blame hygiene and documentation, 4. Close out the change, Tasks, Workflow follow-up

### Community 950 - "Tasks"
Cohesion: 0.29
Nodes (6): 1. Metrics examples, 2. Core and suite examples, 3. Application CLI example, 4. Verification, Tasks, Workflow follow-up

### Community 952 - "Tasks"
Cohesion: 0.29
Nodes (6): 1. Fix the stripper in both guards, 2. Clear the revealed violations, 3. Pin the regression, 4. Verification, Tasks, Workflow follow-up

### Community 956 - "Proposal"
Cohesion: 0.33
Nodes (5): Capabilities, Impact, Ordering note, Proposal, What Changes

### Community 958 - "StateShard"
Cohesion: 0.40
Nodes (3): StateShard, seq, value

### Community 960 - "Requirement: Stdout and stderr are captured to files"
Cohesion: 0.40
Nodes (5): Requirement: Stdout and stderr are captured to files, Scenario: Child stderr goes to the instance log, Scenario: Child stdout goes to the instance log, Scenario: Log files are truncated on each start, Scenario: Restarted attempts use a new log file

### Community 961 - "Design"
Cohesion: 0.33
Nodes (5): Design, Goals / Non-Goals, Migration Plan, Open Questions, Risks / Trade-offs

### Community 962 - "Requirement: Nanosecond UTC timestamps via calibrated clocks"
Cohesion: 0.33
Nodes (5): MODIFIED Requirements, Requirement: Nanosecond UTC timestamps via calibrated clocks, Scenario: File timestamps track wall time, Scenario: Order within file matches monotonic order, Spec Delta

### Community 963 - "Requirement: Long and short option forms are supported"
Cohesion: 0.33
Nodes (6): Requirement: Long and short option forms are supported, Scenario: Long options accept both value forms, Scenario: Multi-character short names are recognized, Scenario: Program name is not parsed, Scenario: Short options accept all value forms, Scenario: Single-letter aliases are rejected

### Community 964 - "Proposal"
Cohesion: 0.33
Nodes (5): Capabilities, Impact, Proposal, What Changes, Why

### Community 965 - "openspec"
Cohesion: 0.50
Nodes (4): openspec, name, runs-on, timeout-minutes

### Community 966 - "Proposal"
Cohesion: 0.33
Nodes (5): Capabilities, Impact, Proposal, What Changes, Why

### Community 967 - "Proposal"
Cohesion: 0.33
Nodes (5): Capabilities, Impact, Proposal, What Changes, Why

### Community 968 - "Tasks"
Cohesion: 0.33
Nodes (5): 1. Scope the line counter, 2. Scope the complexity analysis, 3. Verification, Tasks, Workflow follow-up

### Community 969 - "Proposal"
Cohesion: 0.33
Nodes (5): Capabilities, Impact, Proposal, What Changes, Why

### Community 970 - "Proposal"
Cohesion: 0.33
Nodes (5): Capabilities, Impact, Proposal, What Changes, Why

### Community 971 - "Tasks"
Cohesion: 0.33
Nodes (5): 1. Policy, 2. Rejoin the formatter artefacts, 3. Verification, Tasks, Workflow follow-up

### Community 972 - "Proposal"
Cohesion: 0.33
Nodes (5): Capabilities, Impact, Proposal, What Changes, Why

### Community 973 - "Proposal"
Cohesion: 0.33
Nodes (5): Capabilities, Impact, Proposal, What Changes, Why

### Community 974 - "Tasks"
Cohesion: 0.33
Nodes (5): 1. Track the workflow inputs, 2. Ignore the local leftovers, 3. Verification, Tasks, Workflow follow-up

### Community 975 - "on"
Cohesion: 0.50
Nodes (4): on, pull_request, push, branches

### Community 976 - "Requirement: Effective log level applies to each application"
Cohesion: 0.33
Nodes (6): Requirement: Effective log level applies to each application, Scenario: Absent level preserves settings, Scenario: Application level overrides the suite, Scenario: Effective level beats settings and environment, Scenario: Generated logging settings carry the level, Scenario: Suite level applies by default

### Community 977 - "RequestLineError"
Cohesion: 0.40
Nodes (5): RequestLineError, BadRequest, BadVersion, MethodNotAllowed, None

### Community 979 - "Requirement: Composite metric sink fan-out"
Cohesion: 0.33
Nodes (6): Requirement: Composite metric sink fan-out, Scenario: Composite diagnostics contract, Scenario: Composite flush aggregates, Scenario: Every sink receives every batch, Scenario: One failing sink does not starve the rest, Scenario: Typed and indexed access

### Community 980 - "heap_in_use"
Cohesion: 0.33
Nodes (4): suite_counter(), test_counter(), heap_in_use(), test_harness()

### Community 981 - "ex_stopwatch.cpp"
Cohesion: 0.60
Nodes (3): expect(), main(), work_units()

### Community 986 - "Requirement: Suite metadata lives in a deterministic suite directory"
Cohesion: 0.50
Nodes (4): Requirement: Suite metadata lives in a deterministic suite directory, Scenario: Another process can operate from the suite directory, Scenario: Metadata writes are atomic, Scenario: Suite directory is created on start

### Community 987 - "Requirement: Suite types follow the serialization contract"
Cohesion: 0.50
Nodes (4): Requirement: Suite types follow the serialization contract, Scenario: Hostile suite JSON is ignored, Scenario: Impact and state enums round-trip through text and JSON, Scenario: Manifest and status structures round-trip

### Community 988 - "Requirement: At least one settings directory is required"
Cohesion: 0.40
Nodes (5): Requirement: At least one settings directory is required, Scenario: Documented flow terminates on the missing-directory error, Scenario: Neither directory is supplied, Scenario: Only the global directory is supplied, Scenario: Only the local directory is supplied

### Community 989 - "RequestLine"
Cohesion: 0.50
Nodes (4): RequestLine, method, path, version

### Community 993 - "Requirement: Command-line types are serializable"
Cohesion: 0.40
Nodes (5): Requirement: Command-line types are serializable, Scenario: Diagnostics and errors round-trip through JSON, Scenario: Every type renders through to_string and formatter, Scenario: Non-object JSON is ignored, Scenario: Options round-trip through JSON

### Community 994 - "Requirement: Global and local directory shapes are validated"
Cohesion: 0.40
Nodes (5): Requirement: Global and local directory shapes are validated, Scenario: Absolute local directory is rejected, Scenario: Empty directory value is rejected, Scenario: Relative global directory is rejected, Scenario: Valid directory pair is accepted

### Community 995 - "Requirement: Unknown, missing, repeated, and positional arguments are handled"
Cohesion: 0.40
Nodes (5): Requirement: Unknown, missing, repeated, and positional arguments are handled, Scenario: Missing value is rejected, Scenario: Positional argument is rejected, Scenario: Repeated option keeps the last value, Scenario: Unknown option is rejected

### Community 996 - "log_record_test.cpp"
Cohesion: 0.50
Nodes (3): rendered(), suite_record(), test_record()

### Community 997 - "Requirement: App name option"
Cohesion: 0.50
Nodes (4): Requirement: App name option, Scenario: App name accepts all value forms, Scenario: App name is parsed, Scenario: Omitted app name records nothing

### Community 998 - "Requirement: Fixed file names produce the four settings pairs"
Cohesion: 0.50
Nodes (4): Requirement: Fixed file names produce the four settings pairs, Scenario: Absent directory disables that source, Scenario: Both directories produce eight paths, Scenario: Trailing slash is normalized

### Community 999 - "Requirement: Help short-circuits with success"
Cohesion: 0.50
Nodes (4): Requirement: Help short-circuits with success, Scenario: Help alone succeeds, Scenario: Help wins over invalid arguments, Scenario: Usage text lists the contract

### Community 1000 - "Requirement: Each attempt writes to a new artifact directory"
Cohesion: 0.50
Nodes (4): Requirement: Each attempt writes to a new artifact directory, Scenario: Control files stay in the instance directory, Scenario: First attempt uses attempt-0001, Scenario: Restart creates the next attempt directory

### Community 1004 - "Requirement: Suite stop is final and idempotent"
Cohesion: 0.50
Nodes (4): Requirement: Suite stop is final and idempotent, Scenario: Stop is idempotent, Scenario: Stop succeeds when the suite supervisor is gone, Scenario: Stop terminates every application

### Community 1030 - "Requirement: Logger concept core surface"
Cohesion: 0.50
Nodes (4): Requirement: Logger concept core surface, Scenario: Conforming type satisfies the concept, Scenario: Missing member rejects the concept, Scenario: Wrong signature rejects the concept

### Community 1034 - "trace_daemon_test.cpp"
Cohesion: 0.12
Nodes (18): count_dumps(), E2eContext, config, dir, make_dir(), make_e2e_context(), make_frame(), make_record() (+10 more)

### Community 1039 - "Requirement: File sink configuration serialization"
Cohesion: 0.50
Nodes (4): Requirement: File sink configuration serialization, Scenario: File sink configuration diagnostics, Scenario: File sink configuration round-trip, Scenario: Rebinding is construction, not deserialization

### Community 1040 - "Requirement: Settings sources are explicit"
Cohesion: 0.50
Nodes (4): Requirement: Settings sources are explicit, Scenario: Caller supplies both paths, Scenario: Empty paths yield defaults, Scenario: Missing source is not a failure

### Community 1041 - "null_logger_test.cpp"
Cohesion: 0.67
Nodes (3): null_loop(), suite_null_logger(), test_null_logger()

### Community 1042 - "run_suite"
Cohesion: 0.50
Nodes (4): suite_daemon_components(), main(), run_suite(), suite_prometheus_render()

### Community 1044 - "Requirement: CLI log level overrides loaded settings"
Cohesion: 0.67
Nodes (3): Requirement: CLI log level overrides loaded settings, Scenario: Absent CLI level preserves the loaded value, Scenario: CLI level wins over files and environment

### Community 1045 - "Requirement: Option parsing honors the terminator"
Cohesion: 0.67
Nodes (3): Requirement: Option parsing honors the terminator, Scenario: A bare terminator is accepted, Scenario: Arguments after the terminator are positional

## Knowledge Gaps
- **1741 isolated node(s):** `config_path`, `entries`, `inner_`, `app_name`, `instance_dir` (+1736 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 9108 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **113 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `LogLevel` connect `LogLevel` to `string`, `ADDED Requirements`, `SuiteManifest`, `ConsoleLoggerConfig`, `format_param_t`, `ResetPolicy`, `Requirement: Logger concept core surface`, `execution_manager_logging.cpp`, `SuiteManifest`, `SpamProtection`, `Iceoryx2LoggerConfig`, `LogFrame`, `Requirement: Log level accepts canonical enumerator names`, `LoggerOptions`, `LogRecord`, `StartOptions`, `ChronoWorker`, `sample_utc_offset`, `ErrorCode`, `Decisions`, `SettingsSource`, `SpanAttribute`, `app_suite.cpp`, `suite_supervisor.cpp`, `Requirement: Log level accepts canonical enumerator names`, `Proposal`, `FileLogger`, `FormatWithLocation`, `ProviderConfig`, `TelemetryPacket`, `SpscRingBuffer`, `Decisions`, `nexigen-exec/main.cpp`, `Decisions`, `enum_name`, `ADDED Requirements`, `Requirements`, `file_logger_config.cpp`, `CompositeLogger`, `ConsoleLogger`, `ADDED Requirements`, `Requirement: Logger concept core surface`, `spam_protection_config.cpp`, `logger_options.cpp`, `FileLoggerConfig`, `Iceoryx2Logger`, `RotationSchedule`, `ADDED Requirements`, `SpamProtectionConfig`, `RecordingSink`, `Requirement: Runnable console logger example`, `Entry`, `.emit`, `instance_lifecycle.cpp`, `LogDaemonConfig`, `Requirements`, `Stopwatch`, `Producer`, `CommandLineOptions`, `Decisions`, `REMOVED Requirements`?**
  _High betweenness centrality (0.068) - this node is a cross-community bridge._
- **Are the 77 inferred relationships involving `LogLevel` (e.g. with `2. Уровни логирования и ранняя фильтрация (Early Filtering)` and `D2. `LogLevel` and the shared level-control cell`) actually correct?**
  _`LogLevel` has 77 INFERRED edges - model-reasoned connections that need verification._
- **What connects `config_path`, `entries`, `inner_` to the rest of the system?**
  _1741 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `string` be split into smaller, more focused modules?**
  _Cohesion score 0.04280961647103132 - nodes in this community are weakly interconnected._
- **Why does `MetricRecord` connect `MetricRecord` to `string`, `ResetPolicy`, `MetricIdentity`, `HistogramSnapshot`, `ExponentialHistogram`, `ExplicitBucketHistogram`, `StatisticsSnapshot`, `Approach`, `MetricKind`, `ADDED Requirements`, `MetricProvider`, `ConsoleSink`, `DescriptiveStatistics`, `Gauge`, `.on_batch`, `ADDED Requirements`, `Requirements`, `Requirement: Readable metric concept`, `Requirements`, `self_affinity`, `MetricFrame`, `Subscriber`, `ThreadSpec`, `Counter`, `.inc`, `StateGauge`, `ADDED Requirements`, `RecordingSink`, `snapshot_format.cpp`?**
  _High betweenness centrality (0.044) - this node is a cross-community bridge._
- **Should `format` be split into smaller, more focused modules?**
  _Cohesion score 0.038980603854120895 - nodes in this community are weakly interconnected._
- **Why does `LoggerOptions` connect `LoggerOptions` to `string`, `ADDED Requirements`, `WorkerTuning`, `string_view`, `ResourceMetadata`, `Infrastructure`, `OverflowPolicy`, `SettingsSource`, `Requirement: LoggerOptions defines the engine configuration`, `Decisions`, `Modified Capabilities`, `.shutdown`, `ShardState`, `Tasks`, `.chrono`, `Requirement: LoggerOptions defines the engine configuration`, `LogLevel`, `Requirement: LoggerOptions defines the engine configuration`, `Requirement: LoggerOptions defines the engine configuration`, `logger_options.cpp`, `JsonFileTraceSinkConfig`, `check_capacity`, `ThreadSpec`, `Why`, `Requirements`, `SpamProtectionConfig`, `Decisions`, `Proposal`, `Proposal`, `load_logging_settings`, `Settings`, `AsyncWorker`, `instance_lifecycle.cpp`, `ADDED Requirements`?**
  _High betweenness centrality (0.032) - this node is a cross-community bridge._