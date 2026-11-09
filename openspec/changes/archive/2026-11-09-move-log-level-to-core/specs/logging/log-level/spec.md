# Spec Delta

## REMOVED Requirements

### Requirement: Severity catalogue
**Reason**: The severity vocabulary is re-homed to `nexigen::core` as the program-wide catalogue (`core/log-level`, this change): logging no longer owns the level type, and runner, app, tools, and the upcoming chrono worker all need it without a `nexigen::logging` dependency.
**Migration**: Spell `nexigen::core::LogLevel`; the seven enumerators `Verbose`..`Off`, the strict severity order, and the `Off`-is-highest semantics are unchanged.

### Requirement: Compile-time level naming
**Reason**: The `constexpr` lowercase naming moves with the type to `nexigen::core`; the mapping continues to be evaluated over the same static-storage core names.
**Migration**: Use `nexigen::core::to_string(level)` over `nexigen::core::LogLevel`; the same canonical lowercase spellings and compile-time evaluation guarantee apply.

### Requirement: LogLevel name parsing
**Reason**: `from_string` moves with the type into `namespace nexigen::core`.
**Migration**: Call `nexigen::core::from_string(name, level)` on a `nexigen::core::LogLevel`; identical exact-spelling round-trip and unchanged-out-parameter contract.

### Requirement: LogLevel JSON serialization
**Reason**: The ADL `to_json`/`from_json` pair moves with the type into `namespace nexigen::core`, still backed by the core mandate helpers.
**Migration**: The functions are found by ADL on `nexigen::core::LogLevel`; the lowercase-name-string encoding and the non-throwing `JSON_NOEXCEPTION=1` contract are unchanged.

### Requirement: std::formatter for LogLevel
**Reason**: Format-ability of the level is a property of the core vocabulary now; the rendered result (`to_string` through the string-view formatter) is unchanged and realized by the same core delegate machinery.
**Migration**: Nothing to change for callers: `std::format("{}", core::LogLevel::Warning)` keeps yielding `"warning"`, including inside logging variadic calls.

## MODIFIED Requirements

### Requirement: Runtime level control

Every logger SHALL expose `set_level(nexigen::core::LogLevel) noexcept` and `level() const noexcept -> nexigen::core::LogLevel`, and SHALL honor a level change made at any point during execution: a relaxed atomic load of the active threshold on each log call determines filtering, and a concurrent `set_level` is visible to subsequent calls without locks or ordering handshakes.

#### Scenario: Level raised mid-run filters more

- **WHEN** a logger running at `Debug` is set to `Error` and then called with `Info`
- **THEN** the `Info` call is filtered and does not reach the output sink

#### Scenario: Level lowered mid-run admits more

- **WHEN** a logger at `Off` is set to `Verbose` and then called with `Verbose`
- **THEN** the `Verbose` call is accepted and reaches the output sink

#### Scenario: Reads and writes need no synchronization primitives

- **WHEN** one thread repeatedly reads `level()` while another repeatedly calls `set_level`
- **THEN** no mutex, condition variable, or CAS loop is used, and every observed value is one the other thread actually stored
