# com/waitset Specification

## Purpose

Provides the multiplexed kernel-level wait set over `nexigen::com` ports: attach event listeners with deadline and interval timers, block until one fires, and dispatch the fired source to a `com`-typed callback without exposing `iox2::`.

## Requirements

### Requirement: Multiplexed blocking wait

`nexigen::com::WaitSet` SHALL block the calling thread in the kernel until any one of its attached sources fires, and SHALL process all fired sources in a single pass without busy-polling; when no source is attached it SHALL not spin.

#### Scenario: Wait wakes on any source

- **WHEN** two event listeners are attached and only one notifier fires
- **THEN** the wait returns promptly and the callback observes exactly the firing source

#### Scenario: Empty wait set does not spin

- **WHEN** `wait_and_process_once_with_timeout` runs on a WaitSet with no attachments
- **THEN** the call returns promptly with an error rather than consuming CPU or blocking for the full timeout

### Requirement: Event notification attachment

`WaitSet` SHALL accept an `EventListener` via `attach_notification`, returning a `WaitSetGuard`; a notifier event on that listener SHALL wake the wait set.

#### Scenario: Listener wakes the wait set

- **WHEN** a listener is attached and its paired notifier emits an event
- **THEN** the wait set wakes and the attachment id reports the event via `has_event_from`

### Requirement: Deadline attachment

`WaitSet` SHALL accept an `EventListener` plus a timeout via `attach_deadline`, returning a `WaitSetGuard`; the wait set SHALL wake when either the notification arrives or the deadline elapses, and the attachment id SHALL distinguish the two via `has_event_from` and `has_missed_deadline`.

#### Scenario: Deadline fires when no notification arrives

- **WHEN** a listener is attached with a short deadline and no event is emitted
- **THEN** the wait set wakes at the deadline and the attachment id reports `has_missed_deadline` true

### Requirement: Interval attachment

`WaitSet` SHALL accept a duration via `attach_interval`, returning a `WaitSetGuard`; the wait set SHALL wake periodically at that interval even with no notification.

#### Scenario: Interval wakes repeatedly

- **WHEN** a one-second interval is attached and the wait set is processed in a loop
- **THEN** the wait set wakes at the configured interval without any notifier event

### Requirement: Callback dispatch and source identity

The wait entry points SHALL invoke a caller-supplied callback with a `nexigen::com::WaitSetAttachmentId` and a `nexigen::com::CallbackProgression` return; the callback returning `Stop` SHALL end the processing loop, `Continue` SHALL keep it going, and the callback SHALL never receive an `iox2::` type.

#### Scenario: Callback controls the loop

- **WHEN** a callback returns `Stop` on the first fired source
- **THEN** `wait_and_process` returns after that source and remaining sources are left unprocessed

#### Scenario: Source identity is com-typed

- **WHEN** a callback inspects the received attachment id
- **THEN** the id is a `nexigen::com` type naming the service, with no `iox2::` symbol in the callback signature

### Requirement: Attachment lifecycle

A `WaitSetGuard` SHALL detach its attachment when destroyed, so a moved-from or out-of-scope guard SHALL leave the wait set with fewer attachments and SHALL not leak a source.

#### Scenario: Dropped guard detaches

- **WHEN** a guard for an attached listener goes out of scope
- **THEN** the wait set's attachment count drops by one and the listener no longer wakes it

### Requirement: WaitSet diagnostics

`WaitSet`, `WaitSetGuard`, and `WaitSetAttachmentId` SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization; `CallbackProgression` SHALL provide the full project conversion set (`to_string`/`from_string`, ADL `to_json`/`from_json`, formatter).

#### Scenario: WaitSet renders its state

- **WHEN** `to_string()` and `std::format("{}", waitset)` are evaluated after attaching two sources
- **THEN** the text reports the attachment count and the formatter output equals the member output

#### Scenario: CallbackProgression round-trips

- **WHEN** each `CallbackProgression` value passes through `to_string`, `from_string`, `to_json`, and `from_json`
- **THEN** every canonical name round-trips and the formatter renders the name
