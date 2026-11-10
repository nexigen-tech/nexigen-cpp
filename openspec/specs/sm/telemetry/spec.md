# sm/telemetry Specification

## Purpose

Defines the SM's non-blocking audit logging and performance telemetry: a strict chronological transition journal in a lock-free ring buffer flushed by a background thread, and atomic metrics published periodically.

## Requirements

### Requirement: Transition audit log

The SM SHALL journal every state-change request, arbitration result, rejection, and final transition into a pre-allocated lock-free ring buffer in `O(1)` without I/O or mutexes; each record SHALL carry a monotonic timestamp, the Function Group name, old state, new state, requester id, and the result (`Success`, `RejectedByPriority`, `InvalidGraph`, `Timeout`); a low-priority background thread SHALL format and flush the records.

#### Scenario: Transitions are journaled without blocking

- **WHEN** transitions and rejections occur
- **THEN** each is appended to the ring buffer in constant time and the flush thread writes them to the log

### Requirement: Performance metrics

The SM SHALL maintain atomic counters updated by the arbitration thread and periodically published by a background thread: `last_transition_duration_us`, `arbitration_latency_us`, `rejected_requests_total`, `emergency_transitions_total`, and `transition_timeouts_total`.

#### Scenario: Metrics reflect arbitration behavior

- **WHEN** transitions, rejections, emergencies, and timeouts occur
- **THEN** the published metric counters reflect each event

### Requirement: Telemetry diagnostics

The audit log and metrics SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization reporting the counters.

#### Scenario: Telemetry renders its counters

- **WHEN** `to_string()` and `std::format("{}", telemetry)` are evaluated
- **THEN** the text reports the counters and the formatter output equals the member output
