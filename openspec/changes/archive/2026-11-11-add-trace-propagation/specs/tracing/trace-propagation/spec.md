# Spec Delta

## Purpose

Defines the cross-process trace propagation layer: a `TraceHeader` that rides as a custom header on every iceoryx2 publish-subscribe message, a thread-local `TraceContext` that automatically links incoming spans to outgoing ones through the header, and a `NEW_ROOT` primitive for data sources that start a fresh trace.

## ADDED Requirements

### Requirement: TraceHeader is a fixed zero-copy custom header

`nexigen::tracing::TraceHeader` SHALL be a fixed-size (32 bytes), pointer-free, trivially-copyable struct carrying `TraceId trace_id`, `SpanId source_span_id`, `uint64_t publish_timestamp_ns`, and `uint32_t sequence_number`. It SHALL satisfy the `com::concepts::Message` constraint (trivially copyable) so the iceoryx2 transport treats it as a safe custom header.

#### Scenario: Header layout is fixed

- **WHEN** `TraceHeader` is instantiated
- **THEN** `sizeof(TraceHeader) == 32`, `alignof(TraceHeader) == 8`, and `std::is_trivially_copyable_v<TraceHeader>` holds

#### Scenario: Header carries the propagation fields

- **WHEN** a publisher fills a `TraceHeader` with a known `trace_id`, `source_span_id`, timestamp, and sequence number
- **THEN** those exact values are recovered from the subscriber's `user_header()` after transmission

### Requirement: Thread-local trace context links processes

`nexigen::tracing` SHALL provide a thread-local `TraceContext` that holds the `trace_id` and `source_span_id` extracted from the last received message. The context SHALL be writable once per received message and readable by every subsequent span start and publisher send on that thread, until the next `receive()` overwrites it.

#### Scenario: Incoming message sets the context

- **WHEN** a subscriber receives a message with `TraceHeader { trace_id=A, source_span_id=B }` and writes it into the current thread's `TraceContext`
- **THEN** the next root span opened on this thread inherits `trace_id=A` and treats `B` as its parent span

#### Scenario: Outgoing message reads the context

- **WHEN** a publisher is about to send a message and reads the thread's `TraceContext`
- **THEN** the outgoing `TraceHeader` carries the current `trace_id` and the active span's `span_id` as `source_span_id`

### Requirement: NEW_ROOT starts a fresh trace

`NEXIGEN_TRACE_NEW_ROOT` SHALL generate a fresh `trace_id` (via `TraceId::generate()`) and store it in the thread's `TraceContext`, replacing any previously inherited context. The `source_span_id` SHALL be cleared so the next outgoing message carries a zero parent.

#### Scenario: NEW_ROOT on a data source

- **WHEN** a camera driver calls `NEXIGEN_TRACE_NEW_ROOT("CameraCapture")` at the start of a new frame
- **THEN** the thread's `TraceContext.trace_id` is a freshly generated non-zero id and the next published message carries that id with a zero `source_span_id`

### Requirement: start_span inherits cross-process context

`Tracer::start_span` SHALL check the thread's `TraceContext` as a fallback: when no explicit parent is given and the active span stack is empty, the new span SHALL inherit the context's `trace_id` and treat the context's `span_id` as its parent, instead of generating a fresh root. The context SHALL persist (NOT be consumed) so subsequent same-thread spans keep the same trace.

#### Scenario: Incoming trace links the first processing span

- **WHEN** a subscriber extracts `trace_id=A, source_span_id=B` from a received message and opens a root span `TRACE_SCOPE("process")` without an explicit parent
- **THEN** that span carries `trace_id=A` and its parent is `B`

#### Scenario: Subsequent spans inherit the same trace

- **WHEN** the first processing span ends and the thread opens another root span
- **THEN** the new span still carries `trace_id=A` (context is persistent, not consumed)

### Requirement: TraceHeader and TraceContext diagnostics

`TraceHeader` and `TraceContext` SHALL provide cold-path `to_string()` members plus free helpers and `std::formatter` specializations naming their fields for diagnostics.

#### Scenario: Header and context render their state

- **WHEN** `to_string()` and `std::format("{}", ...)` are evaluated
- **THEN** the text includes the `trace_id`, `span_id`, and sequence number for the header, or the current `trace_id` and source for the context, and formatter output equals member output