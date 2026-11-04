# logging/logger-options delta spec

## MODIFIED Requirements

### Requirement: SpamProtectionConfig defines anti-spam quotas

The system SHALL provide `SpamProtectionConfig` as the nested anti-spam
configuration inside `LoggerOptions`, carrying the quotas the hot-path engine
consumes: repeat-suppression enable flag and window, the per-shard token
bucket, and the bypass level. Byte-budget quotas that no engine path reads
SHALL NOT be part of the configuration surface.

#### Scenario: Default-constructed spam protection carries documented defaults

- **WHEN** a `SpamProtectionConfig` is default-constructed
- **THEN** `enable_repeat_suppression` is `true`, `repeat_suppression_window_ms`
  is `1000`, `max_messages_per_sec_per_shard` is `100000` (0 = no limit), and
  `bypass_min_level` is `LogLevel::Critical`

#### Scenario: `to_string` renders every spam-protection field

- **WHEN** `to_string()` is called on a `SpamProtectionConfig`
- **THEN** the result is non-empty and contains each field name and its value
