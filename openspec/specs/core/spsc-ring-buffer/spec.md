# core/spsc-ring-buffer Specification

## Purpose

Provides a bounded, wait-free first-in/first-out queue transferring elements between exactly one producer thread and one consumer thread, with no locks, no blocking, and no heap allocation.

## Requirements

### Requirement: Fixed-capacity queue declared at compile time

The system SHALL provide a queue type whose element type and capacity are fixed when it is declared, with capacity rounded to a power of two, and all storage reserved with the object so that capacity never changes during its lifetime. A declared capacity that is not a positive power of two SHALL be rejected at compile time.

#### Scenario: Capacity is fixed at construction

- **WHEN** a queue of a given capacity is created and used
- **THEN** it holds that capacity's worth of elements at most and performs no resizing or allocation at any later point

#### Scenario: Rejecting a non-power-of-two capacity

- **WHEN** a queue is declared with a capacity that is not a power of two
- **THEN** the program fails to compile with a diagnosable error rather than silently rounding the capacity

### Requirement: Non-blocking push and pop with classified outcomes

The system SHALL provide a push operation that inserts at the back and a pop operation that removes from the front, each returning immediately with either success or a specific error code: a full queue SHALL report `BufferFull` and an empty queue SHALL report `BufferEmpty`, and neither outcome SHALL ever block the calling thread.

#### Scenario: Push into a queue with room

- **WHEN** an element is pushed while the number of stored elements is below capacity
- **THEN** the push reports success and the element is enqueued

#### Scenario: Push into a full queue

- **WHEN** an element is pushed after the queue has accepted as many elements as it can hold
- **THEN** the push reports `BufferFull` and the pushed element is not lost into the queue or dropped from it

#### Scenario: Pop from an empty queue

- **WHEN** an element is popped from a queue into which nothing has been pushed
- **THEN** the pop reports `BufferEmpty` and yields no element

#### Scenario: Pop returns the element

- **WHEN** an element is popped from a queue holding at least one element
- **THEN** the pop reports success and provides the element that was at the front

### Requirement: Full capacity is usable and empty is distinguishable from full

The system SHALL allow all declared slots to hold elements at once, and SHALL keep the empty state and the full state distinct so a full queue accepts no push and reports full, while an empty queue yields no pop and reports empty.

#### Scenario: Fill then drain

- **WHEN** a producer pushes exactly capacity elements and then a consumer pops until empty
- **THEN** all pushes succeed, the next push reports `BufferFull`, all pops succeed in the same order, and the next pop reports `BufferEmpty`

### Requirement: First-in/first-out ordering and element integrity

The queue SHALL deliver elements to the single consumer in the exact order the single producer inserted them, with each pushed element delivered once and unmodified.

#### Scenario: Order preserved under wrap-around

- **WHEN** more elements are pushed and popped than the queue capacity, so the insertion point wraps past the end of storage
- **THEN** every element is received in insertion order with its original value

### Requirement: Wait-free progress on both ends

Each queue operation SHALL complete in a bounded number of steps determined by the operation alone, independent of the other thread's state or scheduling, so neither the producer nor the consumer can be stalled, spin-waited, or blocked by the other.

#### Scenario: One end idle while the other works

- **WHEN** the consumer makes no call while the producer repeatedly pushes and pops are attempted by the producer alone
- **THEN** each producer call returns after a bounded number of steps without waiting for the consumer thread

#### Scenario: Sustained two-thread exchange

- **WHEN** one producer thread and one consumer thread exchange one million elements with capacity smaller than the total
- **THEN** all one million elements are received exactly once in order, no operation blocks, and the run terminates without a race or data-race report from a thread sanitizer

### Requirement: Cross-thread visibility of transferred elements

An element made available by a successful push SHALL be fully visible to the consumer thread that subsequently pops it successfully, such that the pop establishes the ordering needed for the consumer to read the element's contents without observing a partially written value.

#### Scenario: Written contents are visible after pop

- **WHEN** the producer fills a non-trivial element and pushes it, and the consumer pops it successfully on another thread
- **THEN** every field the producer wrote before the successful push is readable by the consumer after the successful pop

### Requirement: Single-producer single-consumer contract

The queue SHALL define correct behavior for exactly one producer thread and exactly one consumer thread operating concurrently, both calling through the same queue object; other thread topologies SHALL be outside the contract rather than a supported mode.

#### Scenario: Two concurrent endpoints on the same queue

- **WHEN** one thread only pushes and one thread only pops the same queue object concurrently
- **THEN** the ordering, capacity, and visibility requirements above all hold

### Requirement: Allocation-free and lock-free operation

Queue construction, push, and pop SHALL use no mutual-exclusion lock, no blocking synchronization primitive, and no heap allocation.

#### Scenario: Hot-path use

- **WHEN** push and pop are called repeatedly on a real-time hot path
- **THEN** no lock acquisition, blocking wait, or dynamic allocation occurs

### Requirement: Ring buffer diagnostic string

`SpscRingBuffer` SHALL provide a cold-path `std::string to_string() const` (member, plus a free `to_string(const SpscRingBuffer&)` returning the same text) rendering a snapshot diagnostic — capacity, current element count, and the empty/full state — readable from any thread as a non-linearizable observation, mirroring the existing `size()`/`is_empty()`/`is_full()` snapshot caveat. It SHALL never be called from a producer or consumer hot path.

#### Scenario: Snapshot names capacity and occupancy

- **WHEN** `to_string()` is called on a ring of capacity 8 holding 3 elements
- **THEN** the returned string contains the capacity (8), the occupancy (3), and an empty/full indicator

#### Scenario: Third-thread call is safe

- **WHEN** a thread other than the producer or consumer calls `to_string() const` while both endpoints are active
- **THEN** the call completes without synchronization, allocates only for its returned string, and reports some consistent instant of the snapshot counters
