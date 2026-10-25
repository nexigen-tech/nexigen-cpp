# Spec Delta

## Purpose

Provides the reusable monotonic stamping services: a producer-owned, calibrated nanosecond stamp source whose per-object samples never regress, and cold-path UTC clock-offset calibration with an arithmetic monotonic→UTC conversion, so any subsystem can stamp and date events without per-record syscalls or libc time state.

## ADDED Requirements

### Requirement: Calibrated monotonic stamp source

The system SHALL provide a stamp source in `nexigen::chrono` that yields `Timestamp` values in the monotonic nanosecond clock domain. Successive samples from one stamp-source object SHALL never decrease. A calibrated cycle-counter fast path is permitted where the target supports it, provided it is provably equivalent to the raw monotonic clock; where it is unavailable or calibration has not succeeded, every sample SHALL come from a direct clock read.

#### Scenario: Stamps are non-decreasing per source object

- **WHEN** a single stamp-source object is sampled many times in a loop
- **THEN** every sample is greater than or equal to its predecessor

#### Scenario: Stamps reflect real elapsed time

- **WHEN** a known sleep interval separates two samples from one calibrated source
- **THEN** their difference is within a small tolerance of the interval's true duration

#### Scenario: Independent sources keep independent state

- **WHEN** two stamp-source objects are sampled interleaved
- **THEN** each maintains its own non-decreasing sequence within the same clock domain

### Requirement: Stamp source calibration contract

Calibration of a stamp source SHALL be an explicit cold-path operation performed before real-time use, SHALL report failure through the error model rather than terminating, and SHALL leave the source usable through the direct-clock-read path when no fast path can be armed. Sampling SHALL perform no heap allocation, acquire no lock, and never terminate the process on a clock read failure; on such a failure the sample SHALL return the previous stamp rather than a stale zero.

#### Scenario: Calibration failure is reported, not fatal

- **WHEN** calibration cannot sample the required clocks
- **THEN** it returns a failure status and subsequent samples still yield non-decreasing stamps from the clock-read path

#### Scenario: Clock read failure during sampling

- **WHEN** a direct clock read fails while sampling
- **THEN** the call returns without throwing and yields the previous stamp value

#### Scenario: Steady-state sampling allocates nothing

- **WHEN** a loop of samples runs after calibration
- **THEN** no allocation, deallocation, or blocking call occurs during the loop

### Requirement: UTC offset snapshot calibration

The system SHALL provide a cold-path operation that samples the signed offset between `CLOCK_REALTIME` and the raw monotonic clock at one instant into a caller-provided result, using no heap allocation, and SHALL report clock read failure through the error model.

#### Scenario: Offset sampled successfully

- **WHEN** the UTC offset is sampled on a system with working clocks
- **THEN** it returns a success status and an offset that, applied to the monotonic reading of the same instant, reproduces wall-clock time within the two-read latency

#### Scenario: Offset sampling on a failed clock

- **WHEN** either underlying clock read fails
- **THEN** the operation returns a failure status without terminating the process

### Requirement: Monotonic-to-UTC conversion

The system SHALL provide a pure conversion from a monotonic nanosecond count and a previously sampled offset snapshot to the corresponding UTC nanosecond count, performable on any thread with no clock access, no allocation, and no shared state.

#### Scenario: Deterministic conversion

- **WHEN** the same monotonic count and offset snapshot are converted twice
- **THEN** both results are identical, and converting a later monotonic count yields a later UTC count
