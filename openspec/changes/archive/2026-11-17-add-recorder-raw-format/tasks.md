# Tasks

## 1. Recorder module and public types

- [x] 1.1 Create `include/nexigen/recorder/`, `src/recorder/`, and `tests/recorder/`; add the `nexigen_recorder` CMake target with no COM, media, MCAP, or FFmpeg dependency; verify a default configure builds it and `ldd`/link inspection shows no transport or codec library.
- [x] 1.2 Define `RecorderFormat`, `TimeDomain`, `StorageSettings`, `SessionManifest`, `SegmentInfo`, `SchemaDeclare`, `ChannelDeclare`, `DatasetKind`, `DatasetInfo`, and `DatasetSegmentInfo` with `to_string`/`from_string`, `std::formatter`, and non-throwing `to_json`/`from_json`; verify round-trip tests cover every enum and struct under `JSON_NOEXCEPTION=1`.
- [x] 1.3 Define `RecordHeader` (48 B), `IndexEntry` (64 B), `VideoFrameRef` (64 B), `VideoStreamConfig` (1056 B), and `SegmentFooter` (64 B) with `static_assert` on size/alignment/trivial copy; verify the layout test fails on any field drift.
- [x] 1.4 Extend `core::ErrorCode` append-only with `UnsupportedFormat`, `NotFound`, and `CorruptData`; verify existing values/spellings and the core test suite still pass.

## 2. Record framing, CRC, and index

- [x] 2.1 Implement a recorder-local CRC-32C helper using SSE4.2/ARMv8 when available with the `core::integrity` fallback; verify both paths produce the published CRC-32C test vectors.
- [x] 2.2 Implement record framing and validation: header CRC over `[0..39]`, payload CRC, magic/type/payload-size checks; verify a valid record round-trips and a single flipped bit is rejected.
- [x] 2.3 Implement sparse index emission at 256 KiB/50 ms defaults and binary search over `[min_event_ns, max_event_ns]`; verify a slice with out-of-order event times returns the correct records.
- [x] 2.4 Implement torn-record detection: stop at the first incomplete header/payload and return the intact prefix; verify a file truncated at every byte offset never exposes a partial record.

## 3. Session writer, rotation, and recovery

- [x] 3.1 Implement session creation and atomic `session.json` updates (temp file, rename, directory `fsync`); verify a rotation leaves a parseable manifest and no partial JSON.
- [x] 3.2 Implement the single-writer preallocated path with per-thread SPSC shards (`alignas(64)` indices, release/acquire handoff), bounded records, and `GapMarker` on a full shard; verify the measured write loop has zero heap delta.
- [x] 3.3 Implement rotation on max bytes, duration, and records with no record spanning segments; verify each cap rotates and the manifest counts match the files.
- [x] 3.4 Implement `circular`/`stop` retention, `min_free_bytes`, and deletion of only closed triplets; verify the active triplet is never deleted and low disk blocks a new segment cleanly.
- [x] 3.5 Implement footer finalization, recovery scan, and in-memory index rebuild when `.nxidx` is missing or truncated; verify clean and torn-tail sessions both reopen with the correct record prefix.
- [x] 3.6 Validate session ids and layout tokens against `..`, absolute paths, and traversal; verify no directory is created outside the configured root.

## 4. Schema and channel registry

- [x] 4.1 Add built-in layout descriptors for `LogFrame`, `MetricFrame`, `SpanFrame`, `GapMarker`, `TimeSync`, `VideoStreamConfig`, and `VideoFrameRef`; verify `offsetof`/`sizeof` static assertions and a descriptor round-trip.
- [x] 4.2 Implement config-provided opaque schemas with path, SHA-256, size bound (256 KiB), encoding whitelist, and 64 schema/64 channel slots; embed the full descriptor bytes in the session and verify that decoding works after the source schema file is deleted.
- [x] 4.3 Write schema/channel declarations at each segment start and mirror them in `session.json`; verify a single segment is self-describing without the manifest and a schema change creates a new channel id.
- [x] 4.4 Reject records whose channel/schema was not declared; verify the reader reports the missing schema instead of decoding by guesswork.

## 5. Video storage

- [x] 5.1 Implement the `.nxvid` writer with 4 KiB-aligned frame offsets, a bounded frame buffer, and optional per-frame CRC (default off); verify every offset is aligned and the bytes match the input.
- [x] 5.2 Implement `VideoFrameRef` emission after the frame bytes, `VideoStreamConfig` before the first frame and on `config_epoch` changes; verify capture time, 64-bit source sequence, dimensions, codec, bitstream, and extradata survive.
- [x] 5.3 Implement video gap detection within an epoch and epoch reset behavior; verify a sequence jump emits one gap and a `config_epoch` change emits none.
- [x] 5.4 Verify the recorder links no media library with `NEXIGEN_ENABLE_MEDIA=OFF` and can store every configured codec tag without parsing the bitstream.
- [x] 5.5 Verify video decoding metadata is self-contained: copy a session, remove the encoder configuration, and extract the elementary stream plus timestamp sidecar using only `VideoStreamConfig` and the converter; verify the output matches the stored frames.

## 6. Reader and hostile-input safety

- [x] 6.1 Implement the reader API: open session/segment, iterate records, filter by time/channel, read `VideoFrameRef` bytes, and expose gap/metadata records; verify a multi-segment session yields the same records as the writer counted.
- [x] 6.2 Add bounds checks and a fuzz/negative-input harness for truncated, oversized, and hostile `.nxrec`/`.nxidx`/`.nxvid` inputs; verify no out-of-bounds read or unbounded allocation under ASan.

## 7. On-demand converter

- [x] 7.1 Add `nexigen-export` with `list`, `info`, `extract`, `inspect`, `jsonl`, `perfetto`, `video`, and `verify` subcommands, `--dataset`/`--start`/`--end`/`--channels`/`--output`/`--format`/`--json`, and stable exit codes; verify `--help` exits zero and invalid arguments exit non-zero.
- [x] 7.2 Implement `list` and `info`: enumerate every dataset with id/kind/schema/count/size/time range, and report one dataset's byte size, start/end, per-segment breakdown, schema fields/arrays, and video codec/extradata; verify neither command reads payload bytes and both support `--json`.
- [x] 7.3 Implement dataset/time/channel selection through `.nxidx` and JSONL/Perfetto output, including explicit gap objects; verify a selected dataset and time window return only matching records and an unknown dataset lists available ids.
- [x] 7.4 Implement MCAP output with JSON Schema + JSON for built-in frames and pass-through for registered opaque encodings; verify the output reopens with the pinned MCAP reader and reports the expected channels/messages.
- [x] 7.5 Implement video extraction from `VideoFrameRef` + `.nxvid` to codec-native elementary streams plus a timestamp sidecar; verify MP4/MKV requests fail with `UnsupportedFormat` naming the `nexigen_media` follow-up and no partial output is created.
- [x] 7.6 Implement `verify`: per-record CRC, optional frame CRC, footer, count comparison, and truncated-tail reporting; verify a corrupted record/frame produces a non-zero exit and a clean session passes.
- [x] 7.7 Ensure the converter never runs in the background and rejects follow/watch/live options; verify one invocation writes its output and exits with no child process.
- [x] 7.8 Verify self-contained decoding: copy a session to a clean directory, delete the original schema/config files, and run `list`, `info`, and `extract` with only the converter binary; verify all three succeed.
- [x] 7.9 Implement generic decoders for the supported embedded encodings (JSON Schema/JSON, Protobuf `FileDescriptorSet`, FlatBuffers `.bfbs`, pass-through) and a named error for any other encoding; verify a descriptor-only opaque channel decodes without the producer SDK.

## 8. Build, documentation, and integration

- [x] 8.1 Wire MCAP C++ into the converter only (pinned `42aea98d01dbbfe14c53f787378ee3d70ddb2e61`, compression disabled) and keep it out of `nexigen_recorder`; verify `ldd`/link inspection of the recorder shows no MCAP/FFmpeg and the converter builds with COM off.
- [x] 8.2 Update `docs/architecture.md` with the recorder storage and converter Mermaid diagrams, and document the raw format, `list`/`info` inventory, self-contained session package, and converter usage in `docs/` or `benchmarks/recorder/README.md`; verify the documented commands run as written.
- [x] 8.3 Add Doxygen comments (`/// @brief`, `@param`, `@return`, `@tparam`) to every new public header under `include/nexigen/recorder/`; verify the header audit or documentation build reports no missing public contracts.
- [x] 8.4 Add the recorder/converter examples and update the packaging/install rules so the storage library and tool are exported without MCAP/FFmpeg leakage; verify an installed consumer can link `nexigen_recorder` with both gates off.

## 9. Verification

- [x] 9.1 Build Debug and Release (`-O3 -march=native -flto` with latency gates) and run 100% of unit tests; verify no production or recorder test regressions.
- [x] 9.2 Build ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`) and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`) and run 100% of unit tests, including the hostile-input and shard tests; verify no sanitizer reports.
- [x] 9.3 Re-run the FullHD 16 bpp 50 Hz and telemetry storage benchmarks against the production writer; verify the recorded CPU, p99, and allocation-free results stay within the published benchmark envelope.
- [x] 9.4 Verify the converter is on-demand only: no daemon target, no `--follow`, no background thread after the command returns.
- [x] 9.5 Confirm the only existing public-header change is the append-only `core::ErrorCode` vocabulary; verify no other existing public header, source, or tool outside the new recorder/converter targets changed.
- [x] 9.6 Add `nexigen_export_cli` (`tools/nexigen-export/export_cli_test.cpp`): write a real session with the recorder library, fork/exec the built `nexigen-export`, and assert exit codes and payloads for every subcommand (usage errors, `list`/`inspect`/`info` text and `--json`, schema-driven and opaque JSONL, event-time windows, multi-channel selection, Perfetto, elementary video plus sidecar, MP4/MKV/unknown rejections, truncated-tail and corrupted-frame `verify` diagnostics, relocated sessions, MCAP on/off); verify it passes in Release, Debug, ASan, and TSan.

## Workflow follow-up

- Implement `nexigen_media` and MP4/MKV remux in a follow-up change; this revision extracts elementary streams only.
- Archive the change after review and verification.

