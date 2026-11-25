# hw/can/canopen-viewer Specification

## Purpose

Defines passive CANopen analysis in `nexigen-canview`: EDS/DCF loading and node binding, COB-ID routing, NMT/EMCY/SDO/PDO decoding, bounded dynamic PDO remapping, node presentation, and offline fallback.

## Requirements

### Requirement: EDS and DCF files are loaded into shared dictionaries

The viewer SHALL parse CiA 306 `*.eds`/`*.dcf` files into immutable dictionaries shared by all bindings of that file, and SHALL reject files that exceed the configured limits with a diagnostic.

#### Scenario: One dictionary, several bindings
- **WHEN** one EDS file is bound to node 2 and node 3
- **THEN** both bindings share one parsed dictionary and its object names

#### Scenario: File changes on disk
- **WHEN** an EDS file's size, mtime, or hash changes and it is reloaded
- **THEN** a new dictionary is parsed and its existing bindings use the new version after a visible warning

#### Scenario: Oversized file
- **WHEN** an EDS file exceeds the configured size or object limit
- **THEN** the load is rejected with a diagnostic and existing bindings remain usable

#### Scenario: Malformed line
- **WHEN** an EDS line cannot be parsed
- **THEN** the load reports the line and reason and does not crash

### Requirement: EDS bindings are keyed by channel and node

The viewer SHALL allow at most 128 `(channel_id, node_id)` bindings, each with a node id in 1..127, an optional alias, and a colour; duplicate pairs SHALL replace or reject deterministically.

#### Scenario: Bind node
- **WHEN** an EDS is bound to channel 1, node 2
- **THEN** the registry exposes one binding with alias and colour and the node can be filtered and viewed

#### Scenario: Duplicate pair
- **WHEN** a second binding targets the same `(channel_id, node_id)`
- **THEN** the operator chooses replace or cancel; no ambiguous duplicate is created

#### Scenario: Binding limit
- **WHEN** 128 bindings exist and another is requested
- **THEN** the request is rejected with a visible message

### Requirement: COB-ID routing is per channel

The viewer SHALL route 11-bit identifiers per channel through the Predefined Connection Set and EDS-provided custom COB-IDs, SHALL ignore extended identifiers for CANopen, and SHALL report COB-ID conflicts.

#### Scenario: Default TPDO
- **WHEN** a frame with identifier `0x182` arrives and node 2 has no custom override
- **THEN** it is decoded as TPDO1 of node 2

#### Scenario: Custom COB-ID
- **WHEN** an EDS gives TPDO1 of node 2 the COB-ID `0x300`
- **THEN** a frame with identifier `0x300` is decoded as that node's TPDO1

#### Scenario: Conflict
- **WHEN** two bindings claim the same COB-ID
- **THEN** the first route is kept, the conflict is displayed, and the second binding is marked

#### Scenario: Extended frame
- **WHEN** a 29-bit frame arrives
- **THEN** it is not decoded as CANopen and remains available as a raw frame

### Requirement: NMT state is tracked

The viewer SHALL track each bound node's NMT state from boot-up, heartbeat, and NMT command frames, and SHALL mark the state stale when heartbeats stop.

#### Scenario: Boot-up
- **WHEN** a node sends boot-up on `0x700 + node`
- **THEN** its state becomes Boot-up/Pre-operational and the binding shows the new state

#### Scenario: Heartbeat
- **WHEN** a node sends heartbeat state Operational
- **THEN** its state becomes Operational and the heartbeat age resets

#### Scenario: NMT command
- **WHEN** a broadcast NMT Start command is observed
- **THEN** every bound node that receives it is shown as Operational

#### Scenario: Stale heartbeat
- **WHEN** no heartbeat arrives for the configured timeout
- **THEN** the node is marked stale without being removed

### Requirement: EMCY frames are decoded

The viewer SHALL decode EMCY frames on `0x080 + node` into the emergency error code, error register, and manufacturer data.

#### Scenario: EMCY
- **WHEN** node 3 sends EMCY with error code `0x1000` and error register `0x01`
- **THEN** the row names the node, the code, and the register

#### Scenario: SYNC is not EMCY
- **WHEN** a frame with identifier `0x080` arrives
- **THEN** it is decoded as SYNC, not as EMCY of a node

### Requirement: SDO transfers are decoded

The viewer SHALL decode expedited and segmented SDO requests and responses on `0x600 + node` and `0x580 + node`, including index, subindex, direction, abort codes, and values known from the EDS.

#### Scenario: Expedited read response
- **WHEN** a response contains index `0x1000`, subindex 0, and a 32-bit value
- **THEN** the row shows the object name and the value

#### Scenario: Segmented transfer
- **WHEN** a segmented transfer spans several frames
- **THEN** the viewer reassembles the value within its bounded buffer and shows the completed value

#### Scenario: Abort
- **WHEN** an SDO response carries an abort code
- **THEN** the row shows the abort name or code and the transfer is not applied

#### Scenario: Oversized transfer
- **WHEN** a segmented transfer exceeds the assembly buffer
- **THEN** the viewer marks it too large and does not apply it

### Requirement: PDO payloads are unpacked from mapping

The viewer SHALL unpack mapped PDO signals bit-exactly and SHALL show each signal's name, value, and type from the EDS when available.

#### Scenario: Byte-aligned PDO
- **WHEN** a TPDO maps two 16-bit unsigned objects
- **THEN** both names and values are shown in the correct order

#### Scenario: Bit-packed PDO
- **WHEN** a mapping entry is not byte-aligned
- **THEN** the signal is extracted at its bit offset and length

#### Scenario: Unknown object
- **WHEN** a mapping references an object absent from the EDS
- **THEN** the signal is shown as `index:sub (unknown)` without failing the other signals

#### Scenario: Length mismatch
- **WHEN** the mapping requires more bits than the frame payload contains
- **THEN** the decoder marks the PDO partial and does not read beyond the payload

### Requirement: Dynamic PDO remapping is passive and bounded

The viewer SHALL observe successful SDO writes and reads that change COB-IDs or PDO mappings, SHALL apply them to future records only, and SHALL keep bounded mapping snapshots selected by a per-record mapping epoch.

#### Scenario: Mapping write
- **WHEN** a successful SDO write changes a TPDO mapping
- **THEN** subsequent TPDOs of that node decode with the new mapping and earlier records keep their epoch's mapping

#### Scenario: Failed write
- **WHEN** an SDO write is rejected or aborted
- **THEN** the active mapping does not change

#### Scenario: Evicted snapshot
- **WHEN** a record's mapping epoch is no longer retained
- **THEN** the record is marked mapping-stale and decoded with the nearest snapshot plus a warning

#### Scenario: COB-ID change
- **WHEN** a successful SDO write changes a PDO COB-ID
- **THEN** future frames use the new route and earlier history is not re-routed

### Requirement: Binding changes reindex history

Adding or removing a binding SHALL rebuild the channel's COB-ID routes and reindex retained records in bounded chunks so node colours, node filters, and Overwrite rows reflect the binding.

#### Scenario: Binding added after capture
- **WHEN** an EDS binding is added while history already contains frames for that node
- **THEN** reindex progresses in chunks and previously unassociated frames become associated

#### Scenario: Binding removed
- **WHEN** a binding is removed
- **THEN** its routes and node associations are cleared, and open node views become orphaned

#### Scenario: Dynamic mapping does not reindex
- **WHEN** a mapping changes dynamically
- **THEN** retained records keep their original mapping epoch and are not reindexed

### Requirement: CANopen presentation is explicit

The viewer SHALL colour messages by node, show node and service in the table, and expose NMT state, heartbeat age, warnings, and decoded values in the Inspector.

#### Scenario: Node colour
- **WHEN** a message is decoded to a bound node
- **THEN** its row uses the node's assigned colour

#### Scenario: Criticality mode
- **WHEN** the colour mode is Criticality and a message is an error or EMCY
- **THEN** the error styling overrides the node colour

#### Scenario: Inspector details
- **WHEN** a CANopen record is focused
- **THEN** the Inspector shows node, service, object names, SDO fields, and PDO signals

### Requirement: CANopen state is bounded

The viewer SHALL bound EDS memory, SDO assembly buffers, route tables, mapping snapshots, and registry entries, and SHALL reject operations that exceed a limit without affecting existing bindings.

#### Scenario: EDS budget
- **WHEN** loading a new EDS would exceed the EDS budget
- **THEN** the load is rejected and existing dictionaries remain usable

#### Scenario: SDO buffer
- **WHEN** an SDO transfer exceeds the per-direction buffer
- **THEN** it is marked too large and no other node's transfer is affected

### Requirement: Offline CANopen decoding is reproducible

The viewer SHALL use exported CANopen text when EDS files are unavailable and SHALL re-decode from EDS when the binding and file hash are available.

#### Scenario: Missing EDS offline
- **WHEN** an offline session contains `co_decoded` but the EDS file is absent
- **THEN** the exported decoded text is shown and marked as exported

#### Scenario: Matching EDS offline
- **WHEN** an offline session references an EDS file with a matching hash
- **THEN** the viewer re-decodes raw frames and marks the source as rebuilt

#### Scenario: Hash mismatch
- **WHEN** the EDS hash differs from the session header
- **THEN** the viewer warns and uses the available EDS with a visible marker

### Requirement: CANopen vocabulary is diagnosable and serializable

Every public CANopen enum and decoded struct SHALL provide a canonical string representation, a `std::formatter`, and non-throwing JSON serialization and deserialization.

#### Scenario: NMT and service enums
- **WHEN** `NmtState` and `CoService` values are formatted or serialized
- **THEN** they produce their canonical names and round-trip through JSON without throwing

#### Scenario: Binding struct
- **WHEN** an `EdsBinding` is serialized and parsed back
- **THEN** channel, node, alias, path, hash, and colour survive the round-trip

#### Scenario: Decoded record
- **WHEN** a `CoDecoded` value is serialized and parsed back
- **THEN** its node, service, object, and signal values survive the round-trip

#### Scenario: PDO signal
- **WHEN** a `PdoSignal` is serialized and parsed back
- **THEN** its name, value, type, and bit length survive the round-trip
