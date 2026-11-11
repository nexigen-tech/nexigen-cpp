# Spec Delta

## MODIFIED Requirements

### Requirement: Zero-copy publication

`nexigen::com::Publisher<T, H = void>` SHALL let the producer borrow a shared-memory slot through `loan_uninit()` returning `SampleMut<T, H>`, construct the payload in place, and hand it over with `send()`. The payload SHALL never be serialized, copied between buffers, or heap-allocated: the subscriber observes the very bytes the producer wrote. When the service was opened with a non-void custom header type `H`, `SampleMut<T, H>::user_header_mut()` SHALL give mutable access to the header before `send()`.

#### Scenario: Loan and send delivers the message

- **WHEN** a publisher loans a sample, writes fields into it, and sends it to a connected subscriber
- **THEN** the subscriber receives a sample whose bytes equal the values the producer wrote

#### Scenario: Loan without send returns the slot

- **WHEN** a producer loans a sample and drops it without calling `send()`
- **THEN** the borrowed slot returns to the pool and a subsequent loan succeeds

#### Scenario: Custom header is written before send and read after receive

- **WHEN** a publisher and subscriber are created with a non-void custom header type `H`, the publisher writes into `user_header_mut()` before `send()`, and the subscriber reads `user_header()` after `receive()`
- **THEN** the subscriber observes the exact same header bytes the publisher wrote

### Requirement: Subscriber receive

`nexigen::com::Subscriber<T, H = void>` SHALL expose `receive()` returning a `Sample<T, H>` that is a read-only view of the shared-memory slot (the same physical page the producer wrote), and the view SHALL be released for reuse when the caller drops it. When `H` is not `void`, `Sample<T, H>::user_header()` SHALL give const read access to the custom header that the publisher attached to that sample.

#### Scenario: Received sample is the published value

- **WHEN** a subscriber calls `receive()` after a publisher sent a sample
- **THEN** the returned `Sample<T, H>` reports exactly the values that were sent, and no copy was made to deliver them

#### Scenario: Released sample returns to the pool

- **WHEN** a subscriber drops its `Sample<T, H>` after reading it
- **THEN** the shared slot becomes available to the producer again and the queue fill level drops accordingly

#### Scenario: Subscriber reads custom header

- **WHEN** a publisher sends a sample with a custom header set to known values
- **THEN** the subscriber's `receive()` returns a sample whose `user_header()` carries the same values