# hub/streaming Specification

## Purpose

Defines how `nexigen-hub` tails runtime stream files, frames complete lines, keeps bounded in-memory replay rings, identifies streams, and delivers replay plus live records to subscribers without unbounded memory growth.

## Requirements

### Requirement: Streams have stable identities

Each discovered stream SHALL be identified by run, suite, application, instance, attempt, and stream kind. Stream kinds SHALL be `app`, `metrics`, `traces`, `chrono`, `stdout`, `stderr`, and `execution_manager`. The identity SHALL remain stable while the source file is being followed.

#### Scenario: Every stream kind is represented

- **WHEN** an instance has all four JSONL files, stdout, stderr, and a suite execution_manager log
- **THEN** descriptors exist for all seven kinds with distinct identities

#### Scenario: Attempts are distinct streams

- **WHEN** an application writes attempt `0001` and then `0002`
- **THEN** each attempt has its own stream identities

#### Scenario: Identity survives appends

- **WHEN** the source file grows while being followed
- **THEN** the stream identity does not change

### Requirement: Tailing starts from existing content and follows appends

On discovery the hub SHALL read up to the ring capacity from the end of an existing file, align to the next complete line, and then follow new appends. A file created after startup SHALL be read from its beginning.

#### Scenario: Existing file seeds the ring

- **WHEN** the hub discovers a non-empty stream file
- **THEN** the ring contains recent complete lines and new appends follow them

#### Scenario: Large existing file is bounded

- **WHEN** an existing file is larger than the ring byte capacity
- **THEN** the hub reads only the bounded tail and does not load the whole file

#### Scenario: New file is read from the start

- **WHEN** a stream file is created after discovery
- **THEN** its first line is captured

### Requirement: Lines are framed safely

The hub SHALL emit only newline-terminated lines. A trailing partial line SHALL remain buffered until completed. A line larger than the configured maximum SHALL be dropped as one oversized record with a gap marker rather than growing memory without bound.

#### Scenario: Partial line waits for newline

- **WHEN** a write ends in the middle of a line
- **THEN** no record is emitted until the newline arrives

#### Scenario: Oversized line is bounded

- **WHEN** a line exceeds the maximum line size
- **THEN** it is discarded, counted, and surfaced as a gap

#### Scenario: Empty lines are preserved

- **WHEN** the file contains an empty line
- **THEN** it is emitted as an empty record

### Requirement: Rings are bounded per stream

The hub SHALL keep a bounded record-and-byte ring per stream, defaulting to 4,096 records and 8 MiB, with both limits configurable. On overflow it SHALL drop the oldest records and expose a gap with the dropped count and the sequence range.

#### Scenario: Record limit drops oldest

- **WHEN** more records than the configured capacity arrive
- **THEN** the oldest records are dropped and the newest remain

#### Scenario: Byte limit drops oldest

- **WHEN** accumulated record bytes exceed the configured byte capacity
- **THEN** the oldest records are dropped until the ring fits

#### Scenario: Gap reports dropped range

- **WHEN** a subscriber reads after an overflow
- **THEN** a gap record names the dropped count and the missing sequence range

### Requirement: Subscribers receive replay before live

A new subscriber SHALL receive a snapshot of the current ring in sequence order, a clear snapshot boundary, and then live records. Records accepted after the snapshot SHALL NOT be duplicated or skipped.

#### Scenario: Replay precedes live

- **WHEN** a client subscribes to a stream with existing records
- **THEN** it receives the ring snapshot before new live records

#### Scenario: Live records follow the boundary

- **WHEN** new lines arrive while a snapshot is being delivered
- **THEN** they are delivered after the snapshot boundary without duplication

#### Scenario: Empty ring still opens live

- **WHEN** a client subscribes to an empty stream
- **THEN** it receives an empty snapshot boundary and subsequent live records

### Requirement: Slow subscribers are disconnected

Each subscriber SHALL have a bounded outbound buffer. When a client cannot drain its buffer, the hub SHALL disconnect it and record the dropped delivery rather than blocking tailing or other clients.

#### Scenario: Slow client does not block others

- **WHEN** one subscriber stops reading while another remains active
- **THEN** the active subscriber keeps receiving records

#### Scenario: Overflowing client is disconnected

- **WHEN** a subscriber's outbound buffer exceeds its bound
- **THEN** the hub closes that connection and continues tailing

### Requirement: File replacement and new attempts are followed

The hub SHALL detect file replacement or truncation through inotify and reopen the new source without losing the stream identity. A new attempt directory SHALL create a new stream instead of being appended to the old attempt.

#### Scenario: Truncated file restarts at offset zero

- **WHEN** a followed file is truncated
- **THEN** the hub reopens it and reads new content from the beginning

#### Scenario: Replaced file continues the stream

- **WHEN** a followed file is replaced by rename
- **THEN** the stream identity is retained and the new file is followed

#### Scenario: New attempt is separate

- **WHEN** `attempt-0002` appears
- **THEN** its stream is separate from `attempt-0001`

### Requirement: Streaming types follow the serialization contract

The stream descriptor, record envelope, gap marker, and `Stream_kind` SHALL provide `to_string` member/free functions, `std::formatter`, and non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`. Hostile JSON SHALL leave targets unchanged.

#### Scenario: Record envelope round-trips

- **WHEN** a populated record envelope passes through `to_json` then `from_json`
- **THEN** stream id, sequence, kind, line, timestamp, and truncation flag are preserved

#### Scenario: Gap marker round-trips

- **WHEN** a gap marker passes through text and JSON
- **THEN** its dropped count and sequence range are preserved

#### Scenario: Hostile stream JSON is ignored

- **WHEN** `from_json` receives an array or wrong field types
- **THEN** the target keeps its previous values and no exception escapes

### Requirement: Streaming is cold path only

Tailing, ring updates, replay, and subscriber delivery SHALL execute in the hub process and MUST NOT be called from an application tick, producer, snapshot, or sink callback.

#### Scenario: Application hot paths are untouched

- **WHEN** a supervised application runs its tick loop
- **THEN** no hub file read, ring update, or subscriber write occurs in that loop

#### Scenario: Hub reads do not change source files

- **WHEN** the hub tails a stream
- **THEN** source file contents and modification times are unchanged by the reads
