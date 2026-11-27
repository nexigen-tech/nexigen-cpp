# Tasks

## 1. Module scaffold

- [x] 1.1 Add `include/nexigen/canopen/`, `src/canopen/`, and the `nexigen_canopen` static target (alias `nexigen::canopen`) in the root CMake; depend only on `nexigen_core`, `nexigen_chrono`, and `nexigen_hw_can`.
- [x] 1.2 Add a `nexigen_canopen_tests` target under `tests/canopen/` and register it in CTest without linking `nexigen_com`, `nexigen_app`, GUI, or `nexigen_canview_model`.
- [x] 1.3 Link `nexigen_canview_model` to `nexigen_canopen` and remove the moved CANopen sources from `tools/nexigen-canview/CMakeLists.txt`.

## 2. Vocabulary

- [x] 2.1 Move `EdsAccess`, `CoService`, `NmtState`, `EdsDataKind`, `EdsDataTypeInfo`, and `eds_data_type` into one-header-per-entity files under `include/nexigen/canopen/`; preserve `to_string`, `from_string`, `std::formatter`, and ADL `to_json`/`from_json`.
- [x] 2.2 Update `nexigen-canview` includes/usages to the `nexigen::canopen` types; keep `nexigen::canview` presentation enums (`CoDecodeStatus`, `NodeColourMode`) where they are.
- [x] 2.3 Move vocabulary JSON/formatter tests to the core test target.

## 3. EDS/DCF dictionary and parser

- [x] 3.1 Move `EdsNumeric`, `EdsObjectType`, `EdsObject`, `EdsMappingEntry`, `EdsPdoMapping`, `EdsCobIdEntry`, `EdsDictionary`, `EdsLimits`, `EdsDiagnostic`, `EdsParseResult`, and `EdsParser` into public per-entity headers plus `src/canopen/`.
- [x] 3.2 Keep `EdsLibrary` in the viewer as a cache/UI wrapper and make it consume the core parser and dictionary; keep the viewer's size/mtime/hash/budget behaviour unchanged.
- [x] 3.3 Move parser and dictionary cases from `tools/nexigen-canview/tests/canopen_test.cpp` into `tests/canopen/`, including malformed-line, limit, CompactSubObj, mapping, COB-ID, hash and shared-dictionary coverage.

## 4. PDO unpacking

- [x] 4.1 Add core `PdoSignal`, `PdoDecodeResult`, and `decode_pdo`; move `read_bits`, sign extension, REAL32/REAL64 and OCTET_STRING extraction without any presentation text.
- [x] 4.2 Add the viewer adapter that converts `PdoDecodeResult` into `CoSignal`/`CoDecoded`, resolving EDS names and rendering exactly the existing text.
- [x] 4.3 Preserve `UnknownObject`, `Partial`, zero-length mapping, payload-too-short and no-overread behaviour; add a fixture that compares viewer output before and after extraction.
- [x] 4.4 Move PDO bit-packed, byte-aligned, unknown-object and short-payload tests into the core test target.

## 5. SDO codec and assembler

- [x] 5.1 Add core `SdoEvent`, `SdoCompletedWrite`, and `SdoAssembler` with the existing 4 096-byte bound, 128 per-node/direction assemblies, toggle handling and 1 s timeout.
- [x] 5.2 Make the viewer `SdoDecoder` delegate protocol parsing/assembly to the core and keep `CoDecoded` text, inspector data, and dynamic PDO remapping unchanged.
- [x] 5.3 Move expedited, segmented upload/download, abort, interleaved-node, oversized and timeout cases into the core test target; keep viewer SDO presentation tests.

## 6. COB-ID routing

- [x] 6.1 Add core `CobIdRoute`, `CobIdConflict`, `CobIdBinding`, and `CobIdRouter::rebuild(std::span<const CobIdBinding>)`; keep the fixed 32 × 2 048 table and first-route-wins conflict semantics.
- [x] 6.2 Add the viewer adapter that builds `CobIdBinding` entries from `CanOpenRegistry` (slot, channel, node, dictionary) and use it from `CanOpenDecoder` and `CanOpenReindex`.
- [x] 6.3 Move default/custom/extended/conflict routing tests into the core target and add a viewer registry-adapter test.

## 7. Viewer compatibility

- [x] 7.1 Confirm `nexigen-canview` produces byte-identical CANopen decoded text for the existing fixtures before and after extraction.
- [x] 7.2 Verify the core target's link/compile closure contains no `nexigen_com`, `nexigen_app`, ImGui, GLFW, or viewer headers.
- [x] 7.3 Keep `nexigen_canview_tests` green without moving viewer-only CANopen coverage out of it.

## 8. Documentation

- [x] 8.1 Add `/// @brief`, `@param`, `@return`, and `@tparam` Doxygen comments to every new public `include/nexigen/canopen/` header.
- [x] 8.2 Update `docs/architecture.md` with a Mermaid diagram showing the new `nexigen_canopen` boundary between the viewer and the future node stack.

## 9. Verification

- [x] 9.1 Debug build and full CTest pass, including the new `nexigen_canopen_tests` and the existing `nexigen_canview_tests`.
- [x] 9.2 Release build (`-O3 -march=native -flto`) and the same suites pass.
- [x] 9.3 AddressSanitizer build (`-DNEXIGEN_ENABLE_ASAN=ON`) and the same suites pass.
- [x] 9.4 ThreadSanitizer build (`-DNEXIGEN_ENABLE_TSAN=ON`) and the same suites pass; the core remains single-threaded.
- [x] 9.5 Run `clang-format`, the complexity gate, and `graphify update .` after the final code edit.

## 10. Workflow follow-up

- [x] 10.1 Run `openspec validate extract-canopen-core` (valid).
- [x] 10.2 Archive this change after review.
