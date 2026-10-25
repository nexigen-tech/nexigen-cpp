# Spec Delta

## ADDED Requirements

### Requirement: Ring buffer diagnostic string

`SpscRingBuffer` SHALL provide a cold-path `std::string to_string() const` (member, plus a free `to_string(const SpscRingBuffer&)` returning the same text) rendering a snapshot diagnostic — capacity, current element count, and the empty/full state — readable from any thread as a non-linearizable observation, mirroring the existing `size()`/`is_empty()`/`is_full()` snapshot caveat. It SHALL never be called from a producer or consumer hot path.

#### Scenario: Snapshot names capacity and occupancy

- **WHEN** `to_string()` is called on a ring of capacity 8 holding 3 elements
- **THEN** the returned string contains the capacity (8), the occupancy (3), and an empty/full indicator

#### Scenario: Third-thread call is safe

- **WHEN** a thread other than the producer or consumer calls `to_string() const` while both endpoints are active
- **THEN** the call completes without synchronization, allocates only for its returned string, and reports some consistent instant of the snapshot counters
