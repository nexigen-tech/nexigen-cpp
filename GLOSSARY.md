# nexigen

Hard-real-time C++ platform for simulators and control systems. This language describes suite execution (how applications are grouped, launched, supervised and reported), the platform processes that supply time, health, state, telemetry and observability, and the vocabulary those processes share with applications.

## Language

One canonical name per concept; competing names are listed under `_Avoid_`. `_Contested_` marks a name that carries more than one meaning in this codebase or in the specs — read the note before relying on it.

### Suite execution

**Suite**:
A set of Applications and Function Groups described by one manifest, launched and supervised as a unit.
_Avoid_: run, job

**Supervisor**:
The detached process that launches a Suite's Applications and Function Groups and tracks them until the Suite stops.
_Avoid_: runner, orchestrator
_Contested_: `phm::Supervisor` is a different thing — a per-process health state machine inside the PHM daemon, not the suite process.

**Execution Manager (EM)**:
The front-end process of suite execution (`nexigen-exec` / `nexigen-em`) whose supervisor owns the Suite, its instances and its state.
_Contested_: "EM" names both that process and the child-process component inside it that launches and reaps Applications; the PHM "EM lifecycle" and "PHM→EM channel" refer to the process.

**Application**:
A declared executable together with the startup configurations it may be launched under.
_Avoid_: binary, program

**App ID**:
The manifest-driven 16-byte code stamped into every telemetry frame (`CN_LOOP_1`), unique within its suite, defaulting to the application name, and delivered to the child as `--app-id`. It is an attribution label, not an authentication credential.
_Avoid_: tag, app tag, app name

**App Runtime**:
The `app::App<Policy>` facade that owns settings bootstrap, time-base mode, Infrastructure lifecycle, metric handles, one event input, its optional output, and shutdown for a minimal application.
_Avoid_: app class, application framework

**Instance**:
The run-scoped identity of one Application, owning its directory, generated settings, and resource and exit metadata.
_Avoid_: process, copy

**Attempt**:
One launch attempt of an Instance, with its own artifact directory and stream identities; a restart starts the next attempt rather than appending to the previous one.
_Contested_: the hub exposes attempts as directories while the suite supervisor counts them against the restart budget; both refer to the same launch.

**Function Group**:
A named subset of Applications that share one state vocabulary; the group's state selects which startup configuration each bound Application runs under.
_Avoid_: group, mode

**RootFG**:
The reserved Function Group whose states describe the Suite as a whole rather than a subset of Applications.
_Avoid_: root group

**Resting state**:
The inactive state a Function Group returns to after a reset: always `Off`.
_Avoid_: initial state, idle state
_Contested_: the SM starts each group in its first declared state, which need not be `Off`; "resting" is the EM-side notion of the inactive state, not every group's initial state.

**Suite-start state**:
The state requested for a Function Group when the Suite starts: its declared initial state, otherwise the first non-`Off` state, and `Startup` for `RootFG`.
_Avoid_: initial state

**Off**:
The canonical inactive state of a Function Group, in which its bound Applications are not started. Every group can rest in `Off` whether or not it declares `Off`.
_Avoid_: stopped, disabled
_Contested_: the EM also treats `Stopped`, `Shutdown` and `Update` as off-like, so `Off` is the canonical inactive state, not the only one.

**SetState**:
A request to move a Function Group to a named state, identified by a request id and answered with an acknowledgement.
_Avoid_: command, transition (the transition is what happens, not the request)

**Restart budget**:
The number of restart attempts an Application is allowed before the Suite reports it as failed.
_Avoid_: retry limit

**Impact on Suite operability**:
The manifest declaration of what one Application's abnormal termination does to the Suite: `failure` relaunches the whole Suite, `degrade` retries only that Application and degrades the Suite, `not_impact` retries it and leaves the Suite state unchanged.
_Avoid_: criticality, priority

**`SuiteState`**:
The Suite's aggregate lifecycle as recorded in suite state: `Running`, `Degraded`, `Failed`, `Stopped`, `Unknown`.
_Avoid_: suite status (that names the projection, not the vocabulary)

**Unrecoverable state**:
The terminal condition in which the Execution Manager stops every managed process and runs its project hook; later entries are ignored.
_Avoid_: fatal error

**Trusted executable**:
An Application admitted to launch only after its declared SHA-256 digest matches the executable, when the platform policy requires digests.
_Avoid_: verified binary

**Resource group**:
The named cgroup whose limits and budget an Application joins at launch.
_Avoid_: pool, partition

### Application state

**`AppState`**:
What the Suite reports about an Application: `Running`, `Restarting`, `Exited`, `Stopped`, `Failed`, `Unknown`.
_Avoid_: process state, status

**`ProcessState`**:
The Execution Manager's own lifecycle bookkeeping for a process: `Idle`, `Starting`, `Running`, `Terminating`, `Terminated`.
_Avoid_: app state

**Process type**:
The declared termination contract of an Application: `daemon` runs for the whole bound phase and must report readiness, `oneshot` self-terminates and exit code 0 is success.
_Avoid_: process kind

**Readiness**:
An Application's report that it reached `Running` on its own terms; dependency-ordered startup gates on it instead of polling artifacts.
_Avoid_: health, heartbeat

### Application runtime

**Infrastructure**:
The application-side runtime facade that owns the logger, metric provider, tracer provider, time-base attachment and signal handling, and shuts them down in a fixed order.
_Avoid_: runtime, context

**Bootstrap**:
The pre-logger diagnostics buffer that collects initialization warnings until a logger exists, then hands them over.
_Avoid_: startup log

**Stop reason**:
The first recorded cause of an application shutdown — requested, SIGINT, SIGTERM, or time-base lost; later causes never replace it.
_Avoid_: exit code

**Run identity**:
The identity an application receives from its environment: run id, run directory, instance id and instance directory.
_Avoid_: instance identity
_Contested_: "run" here is the suite execution the application belongs to, not the hub's run root.

**Telemetry layout**:
The rule that resolves where each telemetry stream lands from explicit settings or from run identity, so an observatory can attribute every stream.
_Avoid_: paths, naming

**Deterministic client**:
The allocation-free cycle clock and seeded pseudo-random sequence a simulator application uses instead of wall time and system entropy.
_Avoid_: mock clock

### Configuration language

**Settings**:
The persisted, merged user configuration an application or daemon reads at startup (JSON under a settings path), including values injected by the suite.
_Avoid_: config, options
_Contested_: `Settings` and `Config` both appear for the same subsystem; the code consistently reserves `Settings` for the persisted document.

**Config**:
The resolved, construction-time parameters derived from Settings (timeouts, capacities, service names) that a component receives as a plain value.
_Avoid_: settings
_Contested_: some components call their resolved parameters `Options`, others `Config`; the distinction from Settings is consistent, the choice between the two names is not.

**Options**:
A set of tunable knobs passed to one constructor or command line (`PublishSubscribeOptions`, `LoggerOptions`, `CommandLineOptions`).
_Avoid_: settings, config
_Contested_: overlaps with Config; prefer `Options` for knobs of one component and `Config` for the resolved configuration of a subsystem.

**Manifest**:
A declarative JSON document read once at startup that configures a subsystem's topology.
_Contested_: four different documents are called a manifest — the Suite manifest (Applications, Function Groups, states), the Machine manifest (deployment machines and their processes), the SM manifest (state machines, transitions, priorities) and the PHM health manifest (supervised processes, checkpoints, recovery rules). Always qualify which one.

**Policy**:
A named rule that decides one behavioural choice, either as a compile-time template argument or as a startup setting.
_Contested_: metrics policies (`access`, `concurrency`, `reset`) are compile-time instrument properties; synchronization policies (`start`, `late event`, `past target`) are runtime strategies; and `Infrastructure`'s `Policy` is a stateless type list of the telemetry components. Three unrelated uses of the word.

### Time

**Time base**:
The platform's single published time authority: one canonical monotonic scale plus a table of domain maps, published by `nexigen-chronod` into shared memory and consumed read-only.
_Avoid_: clock, timer

**Published base**:
The shared-memory block a time-base daemon maintains; "attached" means a consumer is bound to it, "lost" means a required domain or its heartbeat could not be honoured.
_Avoid_: segment (that is the storage artifact, not the contract)

**Heartbeat**:
The canonical instant of the last successful publication; a consumer that sees it older than its staleness window treats the base as lost even though the memory is still readable.
_Avoid_: keepalive

**Canonical**:
The hub scale of the time base: monotonic-raw nanoseconds that every other domain maps into. It is an adjective, never a type.
_Avoid_: canonical domain, hub
_Contested_: the wire format calls the same concept "canonical", while ADR 0001 forbids reintroducing a `Canonical` class or a user-visible "canonical" domain name.

**Time domain**:
One clock source mapped into the canonical scale — for example monotonic raw, TAI, realtime, or a hardware counter.
_Avoid_: clock, source
_Contested_: "domain" also appears as a generic DDS-style partition and as `timestamp_domain` on transport messages; in chrono it is always a clock domain.

**Domain class**:
What kind of source a time domain is: `system_clock`, `cycle_counter`, or `device_clock`.
_Avoid_: domain type

**Declared domain**:
A time domain an application or daemon requires by name; a declared domain that is absent or unlocked degrades the base instead of silently disappearing.
_Avoid_: required clock

**Domain coefficient row**:
One row of the published table: the anchor pair, forward and reverse Q32 scales, uncertainty, lock state, and validity limits that map a domain to and from canonical time.
_Avoid_: coefficient, map entry

**Lock state**:
How well a domain is tracking the canonical scale: `locked`, `holdover`, `free_run`, or `unsupported`.
_Avoid_: sync state

**Holdover**:
The period during which a previously synchronized domain keeps extrapolating after it lost its reference, bounded by a holdover window.
_Avoid_: fallback, drift

**Uncertainty**:
The published bound, in nanoseconds, on how far a domain's map may be from the canonical scale.
_Avoid_: error, accuracy

**Generation**:
The seqlock word that advances on every publication of the base; consumers reread the domain table only when it moves.
_Avoid_: version
_Contested_: "generation" also numbers time-base history entries and recorder segments; only the seqlock word is read on the consumer path.

**Time-base history**:
The append-only stream of published changes the daemon writes next to the base, used after the fact to resolve stored instants to UTC.
_Avoid_: log, journal

**Boot token**:
The 64-bit prefix of the kernel boot identity that a published base carries, so a consumer can tell a daemon restart from a reboot.
_Avoid_: boot id, boot uuid

**Service flags**:
The set of observed platform capabilities published with a base (PHC present, TAI available, host counter synchronized, RT scheduling applied, memory locked, declared domains complete, and similar).
_Avoid_: capabilities

**Sync mode**:
The base's declared synchronization posture: `standalone`, `ptp_synced`, or `holdover`.
_Avoid_: clock state

**`TimeBaseStatus`**:
The readiness of the published base: `NotReady` (initializing or calibrating), `Ready` (every declared domain locked), `Degraded` (canonical scale usable but a declared domain is absent, free-running or in holdover), `Lost` (no usable base).
_Avoid_: base state, clock status

**Time-base client**:
The application-side read-only attachment to a published base; it caches required rows, refreshes on generation changes, and applies the holdover and fail-fast policy.
_Avoid_: clock reader

**Stamp source / Stamper**:
A per-producer object that converts the fastest available clock into canonical nanoseconds; the chrono provider issues a fixed bank of them.
_Avoid_: clock, sampler

**Provider**:
In chrono, the UTC authority of one process: it issues stampers, samples the wall-clock offset against canonical time, and delivers offset snapshots to a bound sink.
_Contested_: the name is reused for unrelated factories — `MetricProvider` (owns meters) and `TracerProvider` (owns tracers). Never assume "provider" means the same role across modules.

**Service snapshot**:
In chrono, the 1 Hz value the base publishes for consumers that need the current stamp and wall-clock offset in one read.
_Avoid_: snapshot
_Contested_: three other artifacts are called a snapshot; see Snapshot under telemetry delivery. The service snapshot is the only one that is part of the published base.

**Time-sync monitor**:
The cold-path detector of realtime-clock violations — steps, backwards jumps and excessive slew relative to the monotonic scale.
_Avoid_: clock watchdog

**PHC**:
A PTP hardware clock owned by a network interface; when present it is published as an ordinary device domain with its own measured map.
_Avoid_: hardware clock

### Transport

**Node**:
A process's membership in the transport; every port (publisher, subscriber, client, server, event and field endpoint) is created through one.
_Avoid_: context, participant

**Service**:
A named transport endpoint identified by a `ServiceName`; producers and consumers meet by name, not by address.
_Avoid_: topic, channel
_Contested_: "service" also names a daemon ("log service") and a recovery endpoint; on the transport it is always a `ServiceName`.

**Publisher / Subscriber**:
The two ends of publish-subscribe: a publisher writes a payload in place into shared memory, a subscriber reads the same bytes as a read-only view.
_Avoid_: writer, reader
_Contested_: "publisher" is also used for the time-base publisher, the claim publisher and the state Field writer.

**Sample**:
A subscriber-side read-only view of one published payload; the producer's counterpart, a mutable loan, is written before sending.
_Avoid_: message, packet
_Contested_: "sample" also names a measurement in chrono and metrics; on the transport it is a zero-copy view.

**Field**:
A one-entry shared-memory cell that always holds the latest value, so a late reader sees the current state without a request-response round trip.
_Avoid_: variable, blackboard
_Contested_: recorder datasets and metrics snapshots also talk about fields, meaning named members of a value.

**Event**:
A transport notification with an id, delivered 1:N, that wakes consumers without carrying the payload; hardware-originated events may carry extra metadata.
_Avoid_: signal, notification
_Contested_: `DiagnosticEvent`, `SecurityEvent` and process-lifecycle events are structured reports, not transport events.

**Event channel**:
A configured event service pairing a producer with its consumers, including notification options; it creates no transport service until a port is opened.
_Avoid_: topic

**Waitset**:
The mux that waits on several transport endpoints at once and reports which became ready.
_Avoid_: poller, selector

**Request-response**:
The correlated call pattern: a client sends a request and receives one response from a server, with at most one pending response per client.
_Avoid_: RPC, command

**Loss**:
A detected gap in a transport sequence, reported with a reason instead of silently skipped.
_Avoid_: drop

**Publish outcome**:
The finer result of a successful send: delivered with a wake-up versus accepted without one.
_Avoid_: status

**Message contract**:
A payload is fixed-size, trivially copyable and pointer-free, which is what makes zero-copy transport possible.
_Avoid_: serializable payload, POD

### Telemetry delivery

**Telemetry**:
The four streams an application emits — logs, metrics, traces and recorded transport events — as opposed to diagnostics, which report the state of the telemetry machinery itself.
_Avoid_: observability
_Contested_: "diagnostics" is not a synonym for telemetry in this platform.

**Record**:
An immutable, fixed-size value produced at the source and held in an in-process buffer.
_Avoid_: event, entry
_Contested_: also the recorder's stored row; the qualifier (`LogRecord`, `MetricRecord`, `SpanRecord`, recorder record) decides which.

**Frame**:
The cross-process mirror of a Record: encoded, pointer-free, and sent through the transport to a daemon or a recorder.
_Avoid_: packet, message

**Snapshot**:
An immutable copy of state taken at one instant.
_Contested_: the word names four unrelated artifacts — the chrono 1 Hz service snapshot inside the published base, a metric snapshot (one instrument at harvest), the trace daemon's persisted trace artifact (also called a dump), and the hub's tree read model. Always qualify which.

**Sink**:
A consumer of finished telemetry that lives in the emitting process (console, file, transport, or a composite of several).
_Avoid_: target, output
_Contested_: four unrelated contract families use the name — log sinks, metric sinks, trace sinks and the chrono offset sink; they share the lifecycle shape, not the payload.

**Binding**:
A non-owning, type-erased reference that connects a producer to a sink or logger and must not outlive it.
_Avoid_: hook, callback
_Contested_: the SM also binds Function Groups to applications, and the time base publishes binding coefficients; those are different relationships.

**Session**:
One bounded period of work that owns a resource for its duration.
_Contested_: a recorder session is an artifact (one file set holding a correlated timeline); a trace-daemon session is the daemon's process lifetime. Never assume which one.

**Worker**:
The background thread a component owns to drain or publish on a cadence.
_Contested_: the generic `WorkerThread`, the chrono `ChronoWorker`, the logging worker, the metrics harvester and the tracing consumer are all "workers" with different duties.

**Harvester**:
The metrics-side worker that periodically converts instrument state into a batch and hands it to the sinks.
_Avoid_: collector, aggregator

**Quiescence**:
The logging contract that a logger stops accepting records, drains what producers already enqueued, and only then reports shutdown complete.
_Avoid_: flush, drain (both are steps inside quiescence)

**Dropped stats**:
The counted, reported losses of a telemetry stream (full rings, discarded batches, suppressed lines), so a gap is never silent.
_Avoid_: drops, errors

**Overflow policy**:
What a bounded logging buffer sacrifices when it is full — the incoming record or the oldest unread one.
_Avoid_: drop policy
_Contested_: transport overflow is non-blocking by design and reports loss instead of choosing a victim; the named `OverflowPolicy` belongs to logging.

### Logging

**Logger**:
The application-facing sink of `LogRecord`s: console, file, transport (iceoryx2), composite, or a no-op.
_Avoid_: log, output

**`LogRecord`**:
One structured log event at the source: level, source location, format string and its typed arguments, stored without allocation.
_Avoid_: log line, message

**Log frame**:
The fixed-size wire envelope that carries a `LogRecord` to the log daemon; the daemon renders the final line.
_Avoid_: packet

**Log level**:
The severity vocabulary shared by records, filters and settings.
_Avoid_: verbosity

**Rotation schedule**:
The policy that closes a log file and opens the next one: by elapsed period, by size, or both.
_Avoid_: rollover

**Spam protection**:
The per-caller suppression of repeated identical records, with a token budget and a counted summary instead of a flood.
_Avoid_: rate limit, dedup

**Log daemon**:
`nexigen-logd`, the central process that subscribes to log frames, renders the final JSON line and owns rotation and disk I/O.
_Avoid_: log server

### Metrics

**Meter**:
The per-component handle that creates instruments and carries the producer identity used in every metric it reports.
_Avoid_: registry, factory

**Instrument**:
One measurable object owned by a meter — counter, gauge, state gauge, explicit or exponential histogram, descriptive statistics.
_Avoid_: metric
_Contested_: a metric is the reported identity plus value; the instrument is the thing being written to.

**Metric identity**:
The stable key of a reported metric: owning meter (application), name and kind.
_Avoid_: key, label set

**Metric snapshot**:
One instrument's value at harvest time, with kind-specific payload and the policies the instrument was created with.
_Avoid_: sample, reading

**Metric batch**:
The set of metric snapshots produced by one harvest, handed to the sinks as a single bounded view.
_Avoid_: buffer, chunk

**Access / concurrency / reset policy**:
The compile-time instrument properties: whether readers may observe it, whether several threads write it, and whether reading resets the accumulated window.
_Avoid_: mode, flags

**Metrics daemon**:
`nexigen-metricsd`, the central process that aggregates the latest value per metric and exports it.
_Avoid_: aggregator

### Tracing

**Trace**:
One causally related tree of spans, identified by a trace id that travels with outgoing messages.
_Avoid_: transaction

**Span**:
One timed operation inside a trace, with a kind, status, attributes and parent; it is recorded once at start and ended once.
_Avoid_: segment, operation

**Scoped span**:
The RAII guard that owns an active span and ends it exactly once on scope exit unless ended explicitly.
_Avoid_: span guard

**Trace header**:
The fixed-size value attached to an outgoing message that links it to the current trace and active span and carries the publisher sequence.
_Avoid_: context header

**Sampling decision**:
The per-span choice to drop it or to record and sample it, made once at span start.
_Avoid_: filter
_Contested_: "sampling" also names clock sampling in chrono and metric harvesting in metrics; only tracing decides a sampling decision.

**Span frame**:
The cross-process mirror of a span record: the same data with hashes instead of source pointers, plus publisher identity and format version.
_Avoid_: span record (that one is the in-process value)

**Trace daemon**:
`nexigen-traced`, the central flight recorder that drains span traffic into a bounded in-memory ring and persists a snapshot only when the health layer reports an incident.
_Avoid_: collector

**Dump**:
The persisted trace artifact written when the daemon is triggered.
_Avoid_: snapshot
_Contested_: the same artifact is called a "snapshot" in the trace-daemon spec and a "dump" in the daemon's configuration and failure counters; they are the same file.

### Health management (PHM)

**PHM**:
Platform Health Management — `nexigen-phmd`, the daemon that supervises process liveness and applies recovery.
_Avoid_: health monitor, watchdog

**Supervised entity**:
The application-side client through which an application emits checkpoints, health faults and its identity claim.
_Avoid_: client, agent

**Checkpoint**:
A signal an application emits to prove a specific progress or ordering property; it is an activation, not a saved state.
_Avoid_: marker, save point

**Alive / logical / deadline supervision**:
The three liveness contracts: enough checkpoints per window, a legal order of checkpoints including fork/join branches, and completion within a deadline.
_Avoid_: watchdog, heartbeat

**Supervision status**:
The aggregated verdict of one supervised process after a cycle: `Ok`, `Failed` (within tolerance), `Expired` (beyond tolerance) or `Exhausted` (recovery attempts used up).
_Avoid_: health

**Health state**:
The level an application publishes about itself: `Ok`, `Warning`, `Error`, `Critical`.
_Avoid_: supervision status

**Health channel**:
The event endpoint on which an application publishes a health-state transition.
_Avoid_: alert channel

**Claim**:
The identity an application publishes about itself (name, process id, user id) so the daemon can verify it against the manifest without a handshake.
_Avoid_: registration, handshake

**Fault kind**:
The label on a recorded fault that names which check failed, so an operator can separate an aliveness gap from a determinism violation.
_Avoid_: error type

**Security event**:
The daemon's report that a claim was rejected, carrying what was observed and what was claimed.
_Avoid_: alarm

**Watchdog**:
The hardware timer the daemon feeds while every supervised process is healthy; feeding stops on expiry, or on demand, so the device reset is the last-resort safe action.
_Avoid_: health check

**Babbling idiot**:
A process that keeps communicating after it has lost the right to, which supervision must detect rather than interpret as health.
_Avoid_: spammer

**Recovery**:
The escalation applied after a supervision failure: restart the process, switch a Function Group state, or trigger the watchdog reset.
_Avoid_: remediation
_Contested_: the SM also has error recovery inside its state machines, and the EM exposes a recovery handler to PHM; those are the receiving hops of the same escalation, not separate concepts.

**Recovery action / reason / outcome**:
The requested escalation (`restart process`, `switch state`, `trigger watchdog reset`), why it was requested, and whether the addressee accepted, is still working, or rejected it.
_Avoid_: result, reply

**SafeState**:
The reserved emergency state a Function Group is switched to when local restart cannot restore health.
_Avoid_: emergency mode, fallback

**EM lifecycle**:
The PHM's view of whether the Execution Manager is running, so supervision can be suspended during a deliberate stop instead of reporting a fault.
_Avoid_: EM state

### State management (SM)

**State Manager (SM)**:
`nexigen-sm`, the process that owns the Function Group state graphs, arbitrates SetState requests and drives transitions through the EM.
_Contested_: the acronym collides with "state machine" — SM is the process, an LSM is a state machine inside it.

**Arbitration**:
The rule that resolves conflicting SetState requests by requester priority; a request that loses is rejected, not queued.
_Avoid_: conflict resolution

**Requester priority**:
The configured rank of a requester that decides arbitration; PHM fault events outrank hardware power signals, and diagnostics outrank user applications.
_Avoid_: authority level

**Transition**:
The execution of one allowed move between two Function Group states, including its ordered actions.
_Avoid_: switch, change

**`TransitionResult`**:
The outcome of a transition attempt: success, rejected by priority, rejected by an update in progress, invalid graph edge, timeout, or a failed action.
_Avoid_: status

**Transition watchdog**:
The timeout that aborts a transition which does not complete within its budget and reports the timeout instead of hanging.
_Avoid_: timer

**Trigger (TriggerIn)**:
An external request to start a transition with a requester identity and priority, distinct from a SetState command path.
_Avoid_: command
_Contested_: tracing also has a recovery trigger that dumps spans; only the SM one is a TriggerIn request.

**LSM (Lightweight State Machine)**:
A declarative, single-threaded interpreter over a transition table with ordered actions and an error-recovery table, one per Function Group.
_Avoid_: state machine, SM

**LSM action**:
One side effect in a transition: request a Function Group state through the EM, or publish a state on the state channel.
_Avoid_: step

**Error recovery (LSM)**:
The per-state rule that names a fallback state and the actions to run when an action fails; it is local to one group.
_Avoid_: recovery (reserved for the PHM escalation)

**State client**:
The EM-side endpoint that answers SetState requests, runs the requested transition, and reports the result back to the SM.
_Avoid_: state server

**UCM**:
The single-update coordination path: it holds one active update at a time, captures the previous group state, and drives `start`, `complete` and `cancel` through the EM.
_Avoid_: update manager

**Update**:
One coordinated change of a Function Group's software or state, identified by an update id and either active or absent.
_Avoid_: upgrade, deployment

**State publication**:
The per-group Field on which the SM publishes the current Function Group state, so a late reader sees the latest state without a round trip.
_Avoid_: state broadcast

**Group state payload**:
The fixed-size value stored in the group's state Field: the group's current state name.
_Avoid_: state message

**Root lifecycle**:
The SM's own startup and shutdown sequence against RootFG, including its readiness report and the initial-transition query.
_Avoid_: bootstrap

### Recording and observability

**Recorder**:
`nexigen-recd`, the process that records the suite's log, metric, trace and event streams into one session.
_Avoid_: logger, collector

**Recorder session**:
One recording artifact: a set of segments, a manifest and embedded channel/schema declarations, closable cleanly or not.
_Avoid_: file, capture

**Record shard**:
The per-producer ring of framed records that a producer hands to the session writer.
_Avoid_: buffer

**Channel declaration / schema declaration**:
The session metadata that names a recorded stream and describes its payload schema, so a reader can decode the session without the producer's SDK.
_Avoid_: stream, schema
_Contested_: transport services and events are also called channels; a recorder channel is a stored stream, not a live endpoint.

**Time-base resolver**:
The export-side lookup that turns a stored hub instant into UTC from the recorded time-base history.
_Avoid_: converter

**Export**:
`nexigen-export`, the on-demand conversion of a recorded session into requested outputs; it has no daemon and no follow mode.
_Avoid_: dump, replay

**Hub**:
`nexigen-hub`, the local read-only observatory that discovers run roots and serves their tree, metadata and live streams over HTTP.
_Avoid_: dashboard, server

**Discovery**:
The hub's filesystem scan that rebuilds the tree of run roots, suites, instances, attempts and streams, and keeps it live.
_Avoid_: indexer, scanner

**Run root**:
The directory a suite execution was given and that the hub watches; it contains suite metadata and instance directories.
_Avoid_: run, job, workspace
_Contested_: "run" in the hub means this artifact root, while the suite glossary avoids "run" for a Suite and application identity uses "run id"; three different things.

**Hub suite / hub instance**:
The hub's read-only view of a Suite or Instance, derived from the same metadata the Execution Manager writes.
_Avoid_: mirror, copy

**Stream**:
One append-only file the hub tails (application stdout/stderr, metrics, traces, chrono, execution-manager log), exposed with a stable id and sequence numbers.
_Avoid_: log, feed

**Stream kind**:
The classification of a stream: `app`, `metrics`, `traces`, `chrono`, `stdout`, `stderr`, or `execution_manager`.
_Avoid_: type

**Gap marker**:
The hub's explicit report that records were lost (an oversized line was discarded or the reader fell off the bounded ring), with the missing count.
_Avoid_: error, skip

**Hub tree**:
The current read model of everything Discovery sees, rebuilt on every reconciliation and served as one API response.
_Avoid_: index

### Observability embedding

**Observability embedding**:
The pattern by which a runtime/service layer owns the logger and meter while data-plane modules stay telemetry-free and return structured results.
_Avoid_: instrumentation, telemetry layer

**Observability scenario (`OBS-*`)**:
A named, reusable telemetry embedding pattern defined in `docs/observability-embedding.md`; reviews cite the name instead of restating the pattern.
_Avoid_: recipe, profile

**Error episode**:
The open interval for one `(domain, node, code)` error key: from the first occurrence until `recovery_hold_ms` of quiet, with one first-error record, bounded repeats, and one recovery record reporting the suppressed count.
_Avoid_: error storm, error window

**Windowed summary**:
The periodic per-domain cold-path record that aggregates errors, suppressed occurrences, and recoveries since the previous summary; it is emitted only after activity and then resets the window.
_Avoid_: error rollup, periodic log

**`OBS-BOOT`**:
Pre-logger bootstrap diagnostics: bounded `stderr` lines plus a structured error before a logger exists.
_Avoid_: early logging

**`OBS-COLD`**:
Detailed lifecycle, configuration, and EDS/DCF loading journal, with parser diagnostics capped by `max_cold_diagnostics` and an omitted-count trailer.
_Avoid_: verbose logging

**`OBS-EDGE`**:
One record per meaningful edge: a state transition, the first error of an episode, an error-code change, or a recovery. Never one record per observation.
_Avoid_: event logging

**`OBS-HOT-ERR`**:
Bounded hot-path error event: the metric is recorded first, then the logger threshold and the fixed-size error gate decide whether a record is emitted.
_Avoid_: error log, hot logging

**`OBS-HOT-MET`**:
Metrics-first hot path: cached `MultiThreaded` instrument pointers updated per frame with no name lookup, allocation, formatting, or locking.
_Avoid_: counters, stats

**`OBS-SUM`**:
Windowed cold-path summary: one periodic record per active domain/scope that aggregates what the hot path suppressed.
_Avoid_: rollup, report

**`OBS-SNAP`**:
Pull-based diagnostic snapshot: an on-demand cold-path value or harvest call that captures current state for a diagnostic endpoint or test.
_Avoid_: dump, probe

**`OBS-SPAN`**:
Low-frequency transaction span: one `ScopedSpan` around a transaction (EDS load, SDO transaction, sampled PDO cycle), never one per frame.
_Avoid_: per-frame trace

### Diagnostics

**Stopwatch**:
A cold-path measurement handle that records named laps and derives count, min/max/mean and the current run state.
_Avoid_: timer, profiler

**Lap**:
One named interval recorded by a stopwatch, with its start, duration and description.
_Avoid_: split, mark

**Scoped stopwatch**:
The RAII form of a stopwatch that measures exactly one scope.
_Avoid_: timer guard

**Diagnostic event**:
A structured, pointer-free report from a telemetry subsystem about its own lifecycle or runtime state (initialized, degraded, conflict), delivered through a diagnostic binding.
_Avoid_: log record, telemetry event

**Diagnostic binding**:
The type-erased logger reference a provider binds once in configuration mode to receive diagnostic events.
_Avoid_: callback, hook

**Diagnostic source / kind**:
Which subsystem produced a diagnostic event and which class of event it is.
_Avoid_: origin, category

### Synchronization (synchro)

**Sync generator / consumer**:
The two endpoints of phase-aligned periodic activation: the generator publishes one event per grid cycle without cumulative drift, the consumer validates and dispatches it.
_Avoid_: publisher, subscriber
_Contested_: "sync" here means schedule synchronization, not a mutex or an atomic.

**Schedule (grid)**:
The deterministic cycle layout — period, phase and alignment — that defines when each activation is due.
_Avoid_: timer, clock

**Start mode**:
How the first cycle is chosen: `NextCycle` waits for the next grid boundary, `AtTarget` honours an explicit target subject to the past-target policy.
_Avoid_: start policy

**Past-target policy**:
What to do when an explicit start target lies in the past: reject the start or silently realign to the next cycle.
_Avoid_: catch-up

**Late-event policy**:
What a consumer does with an event that arrived after its deadline: drop and count it, or deliver it flagged as late.
_Avoid_: drop policy

**`SyncState`**:
The endpoint lifecycle: `Stopped`, `WaitingForStart`, `Running`, `Faulted`.
_Avoid_: status

### Error and status vocabulary

**Result**:
The platform's fallible return value: either a value or an `ErrorCode`, always checked by branching, never by calling `value()`.
_Avoid_: exception, error

**Status**:
A `Result` that carries no value — success or an `ErrorCode`.
_Avoid_: state
_Contested_: "status" is also a reported state (`TimeBaseStatus`, `SupervisionStatus`, suite status, application status); the unqualified `core::Status` is always an error result.

**Error code**:
The fixed vocabulary of failure reasons shared by every module; values are append-only for ABI compatibility.
_Avoid_: error kind, error type
