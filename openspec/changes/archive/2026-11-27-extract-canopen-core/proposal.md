# Proposal

## Why

CANopen decoding today lives inside `tools/nexigen-canview/canopen` in the `nexigen::canview` namespace, although the CiA 306 parser, PDO bit unpacking, SDO frame assembly, and COB-ID routing are protocol primitives that the future `nexigen::canopen` node stack needs too. Keeping them private to the viewer would force a second implementation and make the viewer and the stack diverge on wire-level behaviour. Extract them once, before the node stack starts, while the viewer's regression tests can prove the behaviour is unchanged.

## What Changes

- Add a transport-independent `nexigen_canopen` library (`include/nexigen/canopen/`, `src/canopen/`, namespace `nexigen::canopen`).
- Move the CiA 306 EDS/DCF parser, immutable object dictionary, PDO mapping/unpacking, SDO frame decoding plus segmented assembly, and COB-ID routing from the viewer into the new library.
- Move the shared CANopen vocabulary (`EdsAccess`, `CoService`, `NmtState`, and the decoded PDO/SDO value types) into public, file-per-entity headers.
- Split the PDO and SDO decoders from the viewer's presentation types: the core returns structured protocol results; `nexigen-canview` keeps `CoDecoded` and converts those results for display.
- Replace the router's dependency on the viewer registry with a neutral `CobIdBinding` input; the viewer builds bindings from its `CanOpenRegistry`.
- Keep `EdsLibrary` (file cache, budgets, UI diagnostics), `CanOpenRegistry`, `CanOpenDecoder` orchestration, reindexing, and NMT/EMCY presentation in `nexigen-canview`.
- Split the CANopen tests into a new `nexigen_canopen_tests` target for the moved primitives and keep viewer-specific CANopen tests in `nexigen_canview_tests`.
- No behaviour, wire format, service name, configuration format, or user-visible output changes. This is a code-ownership extraction, not a protocol change.

**BREAKING**: none for external users; there are no consumers of these types outside `nexigen-canview` yet.

## Capabilities

### New Capabilities

- `canopen/core`: transport-independent CANopen data-plane primitives — EDS/DCF dictionaries, bit-exact PDO unpacking, SDO frame decoding and assembly, COB-ID routing, and canonical vocabulary — reusable by the viewer and the future node stack.

### Modified Capabilities

None. The `hw/can/canopen-viewer` requirements describe viewer behaviour and stay unchanged; the extraction must preserve them exactly.

## Impact

- **Code**: new `include/nexigen/canopen/`, `src/canopen/`, `tests/canopen/`, and the `nexigen_canopen` CMake target. Moved sources leave `tools/nexigen-canview/canopen/`; `nexigen_canview_model` links the new target. `CanOpenDecoder`, `CoDecoded`, `CanOpenRegistry`, `EdsLibrary`, and reindexing stay in the viewer.
- **APIs**: new public `nexigen::canopen` headers. Internal `nexigen::canview` CANopen type names may change to aliases of the core types; no installed package or external application uses them today.
- **Dependencies**: `nexigen_canopen` depends only on `nexigen_core`, `nexigen_chrono`, and `nexigen_hw_can`; it must not depend on `nexigen_com`, `nexigen_app`, ImGui, or any viewer code.
- **Performance / latency**: algorithms are moved, not rewritten. EDS parsing stays cold path with the same size/line/object limits. PDO unpacking stays allocation-free and bit-exact. SDO frame handling keeps its fixed assembly buffers; the existing timeout logic is unchanged. No new hot-path indirection is introduced beyond one library boundary; the viewer's per-frame decode cost is expected to stay within noise.
- **Memory / allocation**: no new allocations. The extractor keeps the existing memory footprint: the parsed dictionary vectors (cold, bounded by parser limits), the SDO decoder's existing per-node assembly and pending-write buffers (128 nodes, 4 KiB per direction — about 1 MiB total), and the COB-ID router's fixed 32 × 2 048 route table. Only ownership moves; the core owns them instead of the viewer.
- **Systems**: no COM/service/transport changes, no `canvbus` changes, no behaviour changes in `nexigen-canview`. The new library is the base for the future multi-node CANopen runtime.
- **Testing**: moved parser/PDO/SDO/router tests get a dedicated CTest target; the viewer's CANopen decode, capture, and session tests remain and must stay green in Debug, Release, ASan, and TSan builds.
