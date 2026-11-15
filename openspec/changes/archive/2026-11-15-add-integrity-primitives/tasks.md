# Tasks

## 1. Core primitives

- [x] 1.1 Add CRC-32C one-shot and streaming functions with `constexpr` tables; verify the `"123456789"` check value `0xE3069283`, the empty input, and streaming-equals-one-shot.
- [x] 1.2 Add CRC-64/XZ one-shot and streaming functions; verify `0x995DC9BBDF1939FA`, the empty input, and chunked equality.
- [x] 1.3 Move the FIPS 180-4 SHA-256 from `exec` to `core/integrity` and leave `exec::Sha256`/`sha256_hex`/`sha256_file` as aliases; verify the `""` and `"abc"` vectors and that the existing trust-gate tests still pass unchanged.
- [x] 1.4 Add the constant-time hex comparison; verify equal digests match, first-character and last-character mismatches both return false, and a length mismatch is rejected.
- [x] 1.5 Prove the hashing path allocates nothing; verify the allocation counter is unchanged across CRC-32C, CRC-64, and SHA-256 calls.

## 2. Verified load

- [x] 2.1 Add `read_verified(path, expected_hex)` returning a result with the exact bytes it hashed; verify a matching file is returned, a one-byte change is refused with a security error, and an unreadable path returns the error without content.
- [x] 2.2 Document the single-read TOCTOU guarantee in the header and cover it with a test that rewrites the file between two loads; verify the first load returns the hashed bytes and the second refuses the new content.

## 3. Manifest verification

- [x] 3.1 Let `load_suite_manifest` accept an optional expected digest and add the `nexigen-em`/`nexigen-exec` flag; verify a matching manifest starts the suite and a mismatched one starts no process.
- [x] 3.2 Let the PHM manifest loader accept an optional expected digest and add the `nexigen-phmd` flag; verify the daemon refuses to start on mismatch with a non-zero exit.
- [x] 3.3 Let the SM manifest loader accept an optional expected digest and add the `nexigen-sm` flag; verify the daemon refuses to start on mismatch.
- [x] 3.4 Document the flag in each tool's usage text; verify the usage output names it and the flag parses.

## 4. Fault log integrity

- [x] 4.1 Append `seq` and `crc32c` to every fault record and compute the CRC over the exact preceding bytes; verify a written record round-trips and the sequence increases strictly.
- [x] 4.2 Add a verification scan returning intact, corrupted, truncated, and legacy counts; verify a flipped byte is reported corrupted, a cut tail truncated, and a four-field legacy line accepted.
- [x] 4.3 Expose the scan through `nexigen-phmd --verify-fault-log <path>`; verify the exit code is zero on a clean log and non-zero when any record is corrupted.

## 5. Documentation and verification

- [x] 5.1 Document the fault-log format change and the manifest digest flags in `docs/`, and update the traceability rows for `RS_SAF_10002`, `RS_SAF_10005`, `RS_SAF_10037`, and `RS_SAF_10039`; verify the traceability gate exits 0.
- [x] 5.2 Run `openspec validate --changes --strict` and `openspec validate --specs`; verify both exit 0.

## Workflow follow-up

- Archive the change after review.
- Verify the archived capability `core/integrity` exists under `openspec/specs/`.
