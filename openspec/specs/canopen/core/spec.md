# canopen/core Specification

## Purpose
Defines the transport-independent CANopen primitives shared by the viewer and the future node stack: CiA 306 dictionaries, bit-exact PDO unpacking, SDO frame decoding and segmented assembly, COB-ID routing, and canonical vocabulary.

## Requirements

### Requirement: EDS and DCF files produce immutable dictionaries

The core SHALL parse CiA 306 `*.eds`/`*.dcf` content into an immutable dictionary of object entries, PDO mappings, and COB-ID overrides. It SHALL enforce bounded file, line, name, and object limits and SHALL report malformed input as a diagnostic without aborting the process.

#### Scenario: Valid dictionary is parsed

- **WHEN** a valid EDS with variables and PDO mapping objects is parsed
- **THEN** the dictionary exposes the object entries, PDO mappings, and COB-ID overrides, and records the source size and content hash

#### Scenario: Malformed line is diagnosed

- **WHEN** one EDS line cannot be parsed
- **THEN** the result carries the line number and a reason, and the parser does not crash

#### Scenario: Limits are enforced

- **WHEN** the input exceeds the configured file, line, name, or object limit
- **THEN** the load is rejected with a diagnostic and no partial dictionary is returned

#### Scenario: One dictionary is shared

- **WHEN** two consumers load the same EDS content
- **THEN** they can share one immutable dictionary instance without copying its objects

### Requirement: PDO payloads unpack bit-exactly

The core SHALL unpack PDO signals from a payload using mapping entries defined by object index, subindex, and bit length. It SHALL support byte-aligned and bit-packed entries, SHALL not read beyond the payload, and SHALL mark partial mappings.

#### Scenario: Byte-aligned signals

- **WHEN** a mapping contains two 16-bit objects
- **THEN** both values are extracted in mapping order with the correct widths

#### Scenario: Bit-packed signal

- **WHEN** a mapping entry starts at a non-byte-aligned bit offset
- **THEN** the signal is extracted at that exact bit offset and length

#### Scenario: Unknown mapped object

- **WHEN** a mapping references an object absent from the dictionary
- **THEN** the signal is reported as unknown without failing the remaining signals

#### Scenario: Payload shorter than mapping

- **WHEN** the mapping requires more bits than the payload contains
- **THEN** the result is marked partial and no bytes beyond the payload are read

### Requirement: SDO frames decode and segmented transfers assemble

The core SHALL decode SDO request and response frames, including expedited and segmented transfers, index, subindex, direction, abort codes, and completion state. It SHALL isolate assembly state per node and direction and SHALL enforce a bounded transfer size and timeout.

#### Scenario: Expedited transfer

- **WHEN** an expedited SDO response carries a 32-bit value
- **THEN** the decoded result exposes the index, subindex, value, and completion state

#### Scenario: Segmented transfer

- **WHEN** a segmented upload or download spans several frames
- **THEN** the assembler concatenates the segments in order and reports the completed payload

#### Scenario: Interleaved nodes

- **WHEN** two nodes perform segmented transfers in parallel
- **THEN** each transfer completes with its own data and neither mixes segments

#### Scenario: Abort and timeout

- **WHEN** a transfer receives an abort frame or exceeds the timeout
- **THEN** the transfer ends with the abort code or timeout reason and no completed write is produced

#### Scenario: Oversized transfer

- **WHEN** a segmented transfer exceeds the bounded assembly buffer
- **THEN** it is marked too large and other node transfers remain usable

### Requirement: COB-ID routes follow the predefined set and EDS overrides

The core SHALL resolve 11-bit COB-IDs through the CANopen Predefined Connection Set and EDS-provided overrides. It SHALL ignore extended identifiers for CANopen routing, SHALL keep the first route when two bindings conflict, and SHALL expose the conflict.

#### Scenario: Default route

- **WHEN** an identifier `0x182` is resolved for node 2 without an override
- **THEN** it maps to TPDO1 of node 2

#### Scenario: Custom route

- **WHEN** an EDS gives TPDO1 of node 2 the COB-ID `0x300`
- **THEN** `0x300` maps to that PDO and the default route is not used

#### Scenario: Extended identifier

- **WHEN** a 29-bit frame is resolved
- **THEN** it is not routed as CANopen

#### Scenario: Conflict

- **WHEN** two bindings claim the same 11-bit identifier
- **THEN** the first route is kept and the conflict names both bindings

### Requirement: CANopen vocabulary is canonical and serializable

Every public core enum and decoded value SHALL provide a canonical string representation, a `std::formatter`, and non-throwing JSON serialization and deserialization under `-fno-exceptions`.

#### Scenario: Enum round-trip

- **WHEN** an access, service, or NMT-state value is formatted and serialized
- **THEN** it renders its canonical name and round-trips through JSON without throwing

#### Scenario: Value round-trip

- **WHEN** a route or decoded PDO/SDO value is serialized and parsed back
- **THEN** its fields survive the round-trip

#### Scenario: Hostile JSON

- **WHEN** a JSON value has the wrong type or an unknown enum name
- **THEN** parsing leaves the target unchanged and does not throw

### Requirement: The core is transport- and UI-independent

The core SHALL build and operate without `nexigen_com`, `nexigen_app`, ImGui, GLFW, or viewer code. Decoding a fixture SHALL depend only on the supplied dictionary, payload, and timestamp inputs.

#### Scenario: Core links without transport

- **WHEN** the core library and its tests are built
- **THEN** no COM, application, GUI, or viewer target is required

#### Scenario: Deterministic decode

- **WHEN** the same dictionary and payload are decoded twice without external input
- **THEN** both results are identical

#### Scenario: Viewer compatibility

- **WHEN** the viewer decodes its existing CANopen fixtures through the extracted core
- **THEN** the displayed records and existing viewer tests are unchanged
