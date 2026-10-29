# Spec Delta

## MODIFIED Requirements

### Requirement: Fixed-size trivially-copyable record

The system SHALL define `LogRecord` as a trivially copyable, cache-line-aligned (`alignas(64)`) fixed 256-byte value type carrying: a monotonic nanosecond `Timestamp`, a `LogLevel`, the producing thread's cached Linux thread id, the source location's file/line/function, the format-string pointer, the type-erased formatting callback pointer, and an inline fixed-size `arg_payload_` blob with its used length. `std::string_view`, `const char*`, and `std::span<const char>` arguments are `memcpy`-copied into `arg_payload_` at pack time (safe truncation with a flag) so the consumer never reads a destroyed producer stack. The record's size is a compile-time constant with no indirection to heap memory.

#### Scenario: Record type properties hold at compile time

- **WHEN** `LogRecord` is inspected in a static assertion
- **THEN** `std::is_trivially_copyable_v`, `alignof(...) == 64`, `sizeof(...) == 256`, and a fixed `sizeof` all hold

#### Scenario: String-like arguments are copied into record-owned storage

- **WHEN** a `std::string_view` or `const char*` argument references a stack buffer that is destroyed before the record is drained
- **THEN** the consumer observes the copied bytes intact (truncated to `arg_payload_` capacity with the truncation flag set when too long), never a dangling pointer, and ASan reports no use-after-free

#### Scenario: Long messages truncate in place

- **WHEN** a packed string-like argument exceeds the inline `arg_payload_` allowance
- **THEN** the stored copy is truncated to capacity with the truncation flag set, the call completes without blocking or allocating, and the consumer renders the truncated text

#### Scenario: Short-circuit fields survive the handoff

- **WHEN** a record is enqueued by the producer and dequeued by the consumer
- **THEN** every field — timestamp, level, thread id, location triplet, format pointer, callback pointer, payload bytes, length — is observed by the consumer exactly as the producer stored it
