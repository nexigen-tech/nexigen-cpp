# Spec Delta

## Purpose

Defines the platform's zero-copy Publish-Subscribe transport: `Publisher<T>` writes messages in place into shared memory and `Subscriber<T>` reads them as read-only views of the same pages, with Event notification, a compile-time trivially-copyable payload contract, non-blocking overflow handling, and subscriber-lag telemetry.

## ADDED Requirements

### Requirement: Zero-copy publication

`nexigen::com::Publisher<T>` SHALL let the producer borrow a shared-memory slot through `loan_uninit()` returning `SampleMut<T>`, construct the payload in place, and hand it over with `send()`. The payload SHALL never be serialized, copied between buffers, or heap-allocated: the subscriber observes the very bytes the producer wrote.

#### Scenario: Loan and send delivers the message

- **WHEN** a publisher loans a sample, writes fields into it, and sends it to a connected subscriber
- **THEN** the subscriber receives a sample whose bytes equal the values the producer wrote

#### Scenario: Loan without send returns the slot

- **WHEN** a producer loans a sample and drops it without calling `send()`
- **THEN** the slot returns to the shared pool and the next `loan_uninit()` reuses it without pool exhaustion

### Requirement: Subscriber receive

`nexigen::com::Subscriber<T>` SHALL expose `receive()` returning a `Sample<T>` that is a read-only view of the shared-memory slot (the same physical page the producer wrote), and the view SHALL be released for reuse when the caller drops it.

#### Scenario: Received sample is the published value

- **WHEN** a subscriber calls `receive()` after a publisher sent a sample
- **THEN** the returned `Sample<T>` reports exactly the values that were sent, and no copy was made to deliver them

#### Scenario: Released sample returns to the pool

- **WHEN** a subscriber drops its `Sample<T>` after reading it
- **THEN** the shared slot becomes available to the producer again and the queue fill level drops accordingly

### Requirement: Event notification on publish

Publishing a sample SHALL notify waiting listeners through the iceoryx2 Event mechanism: `nexigen::com::EventNotifier` pairs with a publisher and `nexigen::com::EventListener` wakes without busy polling.

#### Scenario: Listener wakes on publish

- **WHEN** a listener is blocked in `wait()` and a publisher sends a sample
- **THEN** the listener wakes promptly with the notification, without any polling loop

### Requirement: Trivially-copyable message contract

Every `com` port template SHALL require `com::concepts::Message<T>`, which is satisfied exactly when `std::is_trivially_copyable_v<T>` is true, so payloads are plain bytes with no pointers, no virtual tables, and no owning members; a violating type SHALL be rejected at compile time.

#### Scenario: POD payload is accepted

- **WHEN** a plain struct of arithmetic members is used as a publisher payload type
- **THEN** the `Message<T>` constraint holds and the publisher compiles

#### Scenario: Non-POD payload is a compile error

- **WHEN** a negative-compile probe instantiates a publisher over a type containing `std::string`, `std::vector`, a smart pointer, or a virtual function
- **THEN** compilation fails at the constraint, not at runtime

### Requirement: Compile-time sizing and pool allocation

Port creation SHALL derive the payload size and alignment from `T` itself (`sizeof(T)`/`alignof(T)`) and request a shared-memory pool sized from the configured `max_subscribers` and `max_samples_per_subscriber`; the developer never writes byte counts.

#### Scenario: Pool follows the payload type

- **WHEN** a publisher for `T` is created with `max_subscribers` and `max_samples_per_subscriber`
- **THEN** the shared-memory segment is sized to hold exactly that many samples of `sizeof(T)` with `alignof(T)` alignment

### Requirement: Type-layout mismatch detection

A subscriber connecting to an existing service SHALL verify the publisher's payload type name, size, and alignment; on mismatch the connection SHALL fail with `com::Error::IncompatibleTypeLayout` instead of delivering corrupt bytes.

#### Scenario: Mismatched layouts are rejected

- **WHEN** a subscriber compiled against a different version of `T` connects to a live publisher
- **THEN** the connect returns `IncompatibleTypeLayout` and no sample is exchanged

### Requirement: Non-blocking overflow

Publishing SHALL be non-blocking: when a subscriber's queue is full the delivery policy SHALL discard the oldest unread sample and the publisher SHALL keep running in `O(1)` without waiting on any subscriber.

#### Scenario: Slow subscriber cannot block the publisher

- **WHEN** a subscriber stops reading and its queue fills up while the publisher keeps sending
- **THEN** the oldest unread sample is evicted per send, every `send()` completes immediately, and the publisher never blocks

### Requirement: Subscriber lag telemetry

The send path SHALL maintain per-subscriber diagnostics: a queue-fill counter, a lost-sample counter, and a fill threshold; when the threshold is crossed or a sample is evicted, the module SHALL increment the counters and report the condition through the diagnostics surface.

#### Scenario: Counters track fill and loss

- **WHEN** a subscriber reads slowly enough that its queue crosses the configured threshold and later overflows
- **THEN** the fill counter reflects the queue level, the lost counter reflects every evicted sample, and `to_string()` on the port names both values

### Requirement: Port diagnostics

`Publisher`, `Subscriber`, and `EventNotifier`/`EventListener` SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization naming the service and the port state.

#### Scenario: Ports render their state

- **WHEN** `to_string()` and `std::format("{}", port)` are evaluated on a created publisher and subscriber
- **THEN** both render the service name and the current fill level, and the formatter output equals the member output
