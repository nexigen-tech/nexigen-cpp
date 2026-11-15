# Design

## Context

See `proposal.md` — Why. Relevant constraints:

- `nexigen::core` is a header-only INTERFACE target; adding a header-only
  `core/integrity` needs no build-target changes.
- `nexigen::exec` already ships a FIPS 180-4 SHA-256 with an incremental API
  (`Sha256`, `sha256_hex`, `sha256_file`) used by the executable trust gate; its
  header comment already claims "no allocation on the update path".
- The fault log is plain text written by a dedicated thread:
  `<kind> <process> <checkpoint> <timestamp_ns>`.
- The project has a precedent for promoting shared machinery into `core` and
  leaving a thin shim at the old location (`affinity`, `sink_io`, `settings_io`,
  `fixed_writer`).

## Goals / Non-Goals

**Goals:**

- One place for CRCs and digests, usable by any module without layering hacks.
- A verified-load contract that cannot be bypassed by a file swap.
- Corrupted or truncated fault records are detectable after the fact.
- Zero cost on runtime hot paths.

**Non-Goals:**

- Authentication, signatures, or tamper resistance — this is corruption
  detection (`RS_SAF_10039` speaks about unintended alteration).
- AUTOSAR E2E CRCs (CRC-8/16/32 with AUTOSAR polynomials); they arrive with the
  E2E profiles and their framing.
- A binary fault-log format or a rotation/retention policy.
- Moving `sha256_file`'s file I/O into `core` beyond a small read helper.

## Decisions

**D1 — CRC-32C (Castagnoli) for records, CRC-64/XZ for large blobs.** CRC-32C
has hardware support on x86 (`SSE4.2 crc32`) and better short-message detection
than CRC-32/IEEE; the software table keeps the build portable, and a hardware
path can be added later behind the same function. CRC-64/XZ covers future large
payloads with a standard, tool-friendly polynomial.
*Alternative rejected:* CRC-32/IEEE (zlib) — no advantage here and weaker on
short records.

**D2 — SHA-256 moves to `core` and `exec` keeps aliases.** `exec` should not be
the owner of a general digest, but breaking every call site is unnecessary:
`namespace exec { using Sha256 = core::integrity::Sha256; using
core::integrity::sha256_hex; using core::integrity::sha256_file; }` preserves
source compatibility. The old header stays as a one-line shim, matching the
promotion pattern the project already uses.
*Alternative rejected:* duplicate the implementation — two digests that can
diverge is exactly the class of defect the project has been removing.

**D3 — Verified load reads once and hashes the returned buffer.** The helper
returns the exact bytes it hashed, so replacing the file between read and hash
cannot bypass the check. A "hash the path, then read the path" API would leave a
TOCTOU window; a file-descriptor-based API would still need a single read buffer
for the comparison to mean anything.
*Alternative rejected:* hashing the path and re-reading — the window is small but
real, and the fix costs nothing.

**D4 — Constant-time comparison is a separate helper.** Cheap to implement, and
it keeps callers from writing an early-exit `memcmp` when the digests later
protect something more sensitive than a manifest. The comparison is not
advertised as a security boundary.

**D5 — Fault records gain `seq` and `crc32c` as trailing fields.** The checksum
is computed over the exact bytes of the preceding fields as written, including
separators, so the verifier never has to re-serialize and cannot drift from the
writer. Sequence numbers catch silently dropped lines; the CRC catches bit
flips. The verifier accepts four-field lines as legacy/unverified rather than
failing, because existing logs are still evidence.
*Alternative rejected:* a binary framed format — smaller and stronger, but it
loses grep-ability, and the format change is not justified for a log this small.

**D6 — Layout and cost.** `core/integrity` is header-only with `inline
constexpr` tables (~2 KiB of rodata). CRC-32C over 64 bytes is a few tens of
nanoseconds; SHA-256 is startup-only. Nothing is called from a tick, producer,
or consumer path. The fault writer adds one CRC per record on its own thread.

**D7 — E2E CRCs stay out.** AUTOSAR profiles specify CRC-8 (poly `0x1D`),
CRC-16 (`0x1021`), and CRC-32 (`0xF4ACFB13`), plus profile-specific framing and
counter semantics. Putting them in `core` would mix two unrelated polynomial
families and invite misuse. They will live in the E2E codec, built on the same
"table-driven, `noexcept`, allocation-free" contract.

## Risks / Trade-offs

- **CRC is not tamper-proof.** → Document that this detects corruption, not a
  malicious writer; authentication is out of scope and is stated so in the
  capability purpose.
- **Fault-log format change breaks external parsers.** → The verifier accepts
  both formats; the format is documented in the daemon docs and the change note.
- **Promoting SHA-256 churns includes.** → The shim keeps existing includes
  valid; tasks update only the implementation file and the new tests.
- **Table size in every TU.** → `inline constexpr` keeps one copy per binary;
  the tables are read-only and no initialization order is involved.

## Migration Plan

Additive. Old fault logs remain readable as legacy. The digest flags are
optional, so existing invocations behave as before. Rollback is deleting the new
headers and the two call-site changes; no data migration.

## Open Questions

- Whether to add the x86 `crc32` hardware path now or when a benchmark shows the
  table is a bottleneck. The API does not change either way.
