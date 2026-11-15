# Proposal

## Why

Three integrity gaps remain independent of the dropped E2E and PER subsystems:

1. `core` has no checksum at all. Storage, logs, and the future E2E layer all
   need one, and today nothing exists below `exec`.
2. The only SHA-256 implementation lives in `nexigen::exec`
   (`exec/executable_digest.hpp`), so any other module that wants to verify a
   file must either depend on the process supervisor or copy the code — a
   layering violation the project has already refused elsewhere (affinity,
   `sink_io`, `settings_io` were promoted to `core` for the same reason).
3. Configuration is parsed without any integrity check, and the fault log — the
   evidence trail every later safety claim points at — is plain text that cannot
   distinguish a corrupted record from a valid one.

`RS_SAF_10002`, `RS_SAF_10005`, `RS_SAF_10037`, and `RS_SAF_10039` ask for
verification and corruption detection; this change supplies the primitives and
the two places that need them now, without waiting for E2E.

## What Changes

- **New header-only `nexigen::core::integrity`**: CRC-32C (Castagnoli) and
  CRC-64/XZ one-shot and streaming, FIPS 180-4 SHA-256 (promoted from `exec`),
  lowercase hex helpers, and a constant-time digest comparison. Everything is
  `noexcept`, table-driven from `constexpr` data, and allocation-free on the
  hashing path.
- **SHA-256 moves to `core`**; `nexigen::exec::Sha256` and the free helpers stay
  as thin aliases so existing call sites and includes keep working.
- **Verified file load**: a helper that reads a file once, hashes exactly the
  bytes it will return, and refuses to return content when the expected digest
  differs — no read-twice TOCTOU window.
- **Manifest digest checks**: the suite, PHM, and SM loaders accept an optional
  expected SHA-256 and refuse to start on a mismatch; the tools expose the flag.
- **Fault log record integrity**: each appended record gains a monotonic
  sequence number and a CRC-32C; a verification scan reports corrupted and
  truncated records, treats pre-change four-field lines as legacy/unverified,
  and never aborts on malformed input.
- **AUTOSAR E2E CRCs are deliberately not added here.** E2E profiles use
  CRC-8/16/32 with AUTOSAR polynomials, which belong to the protocol layer of
  the next change; keeping them out of `core` avoids a second, unrelated CRC
  family in the base library.

## Capabilities

### New Capabilities

- `core/integrity`: allocation-free checksums and digests, a constant-time
  comparison, and a verified file-load contract used by configuration loaders.

### Modified Capabilities

- `phm/recovery`: the fault log carries per-record sequence and checksum data and
  can be verified after the fact.

## Impact

- **Code**: new `include/nexigen/core/integrity/` headers; `src/phm/fault_storage.cpp`;
  `src/exec/executable_digest.cpp` becomes a shim over `core`; suite/PHM/SM
  manifest loaders; `tools/nexigen-em`, `tools/nexigen-phmd`, `tools/nexigen-sm`.
- **Performance**: cold path only. CRC-32C over a 64-byte buffer is a few tens
  of nanoseconds; SHA-256 of a manifest is milliseconds at startup. Nothing is
  added to a tick, producer, or consumer path. The fault-log writer adds one
  `crc32c` over the record's fields per record, outside the monitoring loop.
- **Memory**: ~2 KiB of `constexpr` tables in read-only data per translation
  unit that includes the CRC headers (inline `constexpr` arrays), no runtime
  allocation. The SHA-256 state stays one 32-byte accumulator plus a 64-byte
  block.
- **Compatibility**: `exec::Sha256`/`sha256_hex`/`sha256_file` keep their names
  and signatures as aliases — no breaking change. Old fault logs stay readable
  as legacy records; new records are appended in a superset format.
