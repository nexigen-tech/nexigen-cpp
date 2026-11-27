# Design

## Context

`tools/nexigen-canview/canopen` already contains the CiA 306 parser, an immutable `EdsDictionary`, bit-exact PDO unpacking, SDO expedited/segmented assembly, a 32 × 2 048 COB-ID router, and the CANopen vocabulary. All of it is compiled into `nexigen_canview_model`, uses the `nexigen::canview` namespace, and mixes protocol logic with presentation types (`CoDecoded`, `CoSignal`).

The future `nexigen::canopen` node stack needs the same wire-level primitives. Duplicating them would create two interpretations of EDS, PDO bit order, SDO toggles, and route conflicts. The extraction must therefore separate protocol state from presentation without changing viewer output.

Constraints from the repository:

- C++23, `-fno-exceptions`, `-fno-rtti`, no `.value()`, nlohmann/json 3.12.0 pinned.
- Public types live under `include/nexigen/<module>/`, one top-level type per header.
- `nexigen::canopen` must not depend on `nexigen_com`, `nexigen_app`, ImGui, GLFW, or viewer code.
- No allocation or blocking on decode hot paths; the moved algorithms already satisfy this.

## Goals / Non-Goals

**Goals:**

- One implementation of EDS/DCF parsing, PDO unpacking, SDO frame assembly, and COB-ID routing, owned by `nexigen::canopen`.
- `nexigen-canview` consumes the shared core and produces byte-identical decoded presentation for existing fixtures.
- A public core API that the future node stack can use without linking COM or GUI.
- A dedicated core test target; viewer tests remain and stay green.

**Non-Goals:**

- Implementing active NMT, SYNC, heartbeat, EMCY producers/consumers, SDO server/client, OD runtime, or signal mapping — those belong to later CANopen changes.
- Moving `EdsLibrary`, `CanOpenRegistry`, `CanOpenDecoder`, `CanOpenReindex`, `co_runtime`, or the NMT/EMCY presentation decoders.
- Changing wire formats, EDS semantics, viewer output, CLI, session JSONL, or configuration.
- Making the core multi-threaded. It remains a single-threaded value/logic library.

## Decisions

### Target and dependencies

Add `nexigen_canopen` as a static library near `nexigen_hw_can` in the root CMake:

```text
nexigen_canopen
  PUBLIC nexigen_core nexigen_chrono nexigen_hw_can
  PRIVATE Threads::Threads only if a moved primitive needs it (none expected)
```

No `nexigen_com`, `nexigen_app`, GUI, or viewer include directories. `nexigen_canview_model` links `nexigen_canopen` and removes the moved sources. The core aliases the public namespace `nexigen::canopen`.

### File-per-entity public layout

The current `canopen_types.hpp`, `eds_dictionary.hpp`, `pdo_decoder.hpp`, and `sdo_decoder.hpp` group several types. Under `include/nexigen/canopen/` they are split:

```text
eds_access.hpp, co_service.hpp, nmt_state.hpp
eds_numeric.hpp, eds_object_type.hpp, eds_object.hpp, eds_data_kind.hpp,
eds_data_type_info.hpp, eds_mapping_entry.hpp, eds_pdo_mapping.hpp,
eds_cob_id_entry.hpp, eds_dictionary.hpp
eds_limits.hpp, eds_diagnostic.hpp, eds_parse_result.hpp, eds_parser.hpp
pdo_signal.hpp, pdo_decode_result.hpp, pdo_decode.hpp
sdo_event_kind.hpp, sdo_event.hpp, sdo_completed_write.hpp, sdo_assembler.hpp
cob_id_route.hpp, cob_id_conflict.hpp, cob_id_binding.hpp, cob_id_router.hpp
```

Each header carries the project-mandated Doxygen comments. Every public type carries `to_string`; enums also carry `from_string`, `std::formatter`, and non-throwing JSON. Decoded PDO/SDO values and routing results carry JSON as well; cold EDS descriptors keep `to_string` only. No umbrella header is required; consumers include the entity they use.

### PDO: protocol result vs presentation

The core owns extraction only:

```cpp
struct PdoSignal {            // core
    std::uint16_t index;
    std::uint8_t  subindex;
    std::uint8_t  bits;
    EdsDataKind   kind;
    std::uint64_t raw;
    std::int64_t  signed_value;
    double        real;
    bool          valid;
    bool          unknown_object;
    bool          string_value;
    std::array<std::uint8_t, 64> bytes;
    std::uint8_t  byte_count;
};

struct PdoDecodeResult {
    PdoDecodeStatus status;   // Ok, Partial, UnknownObject, Invalid
    std::array<PdoSignal, 16> signals;
    std::uint8_t count;
};

PdoDecodeResult decode_pdo(const hw::can::CanMessage&, const EdsPdoMapping&,
                           const EdsDictionary*);
```

The viewer's `decode_pdo(..., CoDecoded&)` becomes a thin adapter: it calls the core, copies values into `CoSignal`, resolves the EDS name, and renders text exactly as today. `CoDecoded`, `CoSignal`, names and text stay in `nexigen::canview`.

### SDO: codec/assembler vs presentation

The core exposes:

- `SdoEvent`: structured decision with kind, direction, segmentation flags, index/subindex, abort code, expedited value and size.
- `SdoCompletedWrite`: completed payload, index/subindex and byte count.
- `SdoAssembler`: per-node/direction fixed buffers, fed by parsed frames; `expire(now_ns)`; emits events and completed writes.

The viewer's `SdoDecoder` keeps its public shape and `CoDecoded` output, but delegates parsing/assembly to the core and only renders presentation text. `SdoCompletedWrite` moves to the core because dynamic PDO remapping consumes it; the viewer still applies it to its mapping snapshots.

### COB-ID routing with a neutral binding

The core router must not know `CanOpenRegistry`. Add:

```cpp
struct CobIdBinding {
    std::uint16_t slot;                 // opaque owner slot
    std::uint16_t channel_id;
    std::uint8_t  node_id;
    const EdsDictionary* dictionary;    // custom COB-IDs and disabled PDOs
};
```

`CobIdRouter::rebuild(std::span<const CobIdBinding>)` builds the same default/custom routes. `CobIdRoute` and `CobIdConflict` move to the core unchanged. The viewer builds a 128-entry binding array from `CanOpenRegistry` before each rebuild; `CanOpenReindex` continues to use the core router.

### Test and CMake split

- New `tests/canopen/` with `nexigen_canopen_tests`: EDS parser/dictionary, PDO bit unpack, SDO codec/assembly/timeouts, COB-ID routing, vocabulary JSON/formatter.
- Move the relevant cases out of `tools/nexigen-canview/tests/canopen_test.cpp` and `canopen_decode_test.cpp`; keep viewer integration, registry, library, reindex, NMT/EMCY, presentation, and compatibility coverage in `nexigen_canview_tests`.
- Add both targets to CTest. The core target must not link `nexigen_canview_model`.

### Memory and concurrency

The extraction preserves existing storage; it does not redesign it:

- `EdsDictionary` uses cold-path vectors bounded by parser limits.
- `SdoAssembler` keeps 128 per-node × 4 KiB assembly and 128 × 4 KiB pending-write buffers (~1 MiB), fixed at construction.
- `CobIdRouter` keeps a fixed 32 × 2 048 slot table.
- No atomics, mutexes, or thread-local state are introduced. Core is single-threaded; callers own any synchronization.

## Risks / Trade-offs

- **Mechanical split risk**: moving many headers can change include order or accidentally drop a `to_json` overload. Mitigation: move tests first, build with `-Werror` as configured, and keep viewer fixture outputs identical.
- **Presentation coupling in PDO/SDO**: the current decoders write `CoDecoded` directly. Splitting into protocol result plus adapter touches hot paths. Mitigation: keep the extraction functions allocation-free and benchmark the viewer decode path if any difference appears.
- **Router conflict semantics**: replacing `NodeBinding` with `CobIdBinding` can subtly change first-wins behaviour. Mitigation: move the router test cases unchanged and add a viewer-registry adapter test.
- **Public API surface**: the new headers become a compatibility commitment. Keep them limited to the types the future node stack needs; do not expose viewer presentation.
- **Test duplication/loss**: splitting test files can leave cases in only one target. Mitigation: enumerate moved cases in tasks and keep the full CTest suite as the acceptance gate.
- **File-per-entity churn**: the split increases the file count. This follows the project mandate and avoids a later public-header redesign.
