# Spec Delta

## ADDED Requirements

### Requirement: Stamp source diagnostic string

`nexigen::chrono::Stamp_source` SHALL provide a cold-path `std::string to_string() const` (member, plus a free `to_string(const Stamp_source&)` returning the same text) reporting the source's calibration state for post-mortem debugging of a mis-calibrated clock. It reads producer-owned state and SHALL only be called from the owning thread or during quiescent diagnostics, never from the sampling hot path.

#### Scenario: Uncalibrated source is identifiable

- **WHEN** `to_string()` is called on a default-constructed `Stamp_source` before `calibrate()`
- **THEN** the returned string states that the fast path is not calibrated

### Requirement: Stamp source dump content

The `Stamp_source` diagnostic string SHALL name the calibration flag, the monotonic nanosecond anchor, and the last emitted timestamp whenever sampled.

#### Scenario: Calibrated source reports its anchors

- **WHEN** `to_string()` is called after a successful `calibrate()` and a few `sample()` calls
- **THEN** the returned string identifies the calibration state and the current monotonic nanosecond anchor
