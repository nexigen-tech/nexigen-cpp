# Proposal

## Why

E2E protection is the only Adaptive Platform safety cluster with no code at all.
The transport is zero-copy local IPC with no integrity layer, so a bit flip in
shared memory, a stale sample, a duplicated delivery, or a sample published to
the wrong service is silently accepted by the receiver. `RS_CM_00223`,
`RS_CM_00224`, `RS_CM_00400`, `RS_CM_00401`, `RS_CM_00404`, and
`RS_SAF_10014`/`10039`/`10041` require protected exchange plus the check result
delivered to the application.

Doing this after the integrity primitives means the CRC building blocks exist,
and doing it before any network transport exists means the protection layer is
in place before a real bus can introduce the failure modes it is meant to catch.

## What Changes

- **New `com/e2e` capability.** A trivially copyable `E2eMessage<T>` envelope
  (fixed 16-byte header plus payload) that travels as an ordinary
  `com::concepts::Message`, so no transport template changes are needed.
- **Profiles.** A `E2eProfile` enum with `None`, `P04`, `P05`, `P06`, and `P07`,
  each selecting the AUTOSAR CRC polynomial and counter width (`P04`/`P07`
  CRC-32 `0xF4ACFB13`, `P05`/`P06` CRC-16 `0x1021`). `P01` (4-bit counter
  packing) is deferred.
- **Codec.** `seal` writes the header and checksum before send; `check`
  validates length, data id, CRC, and counter sequence, and returns an
  `E2eStatus` (`Ok`, `Repeated`, `WrongSequence`, `Error`, `NoNewData`,
  `NotAvailable`) plus state counters.
- **Sender and receiver wrappers.** `E2ePublisher<T>`, `E2eSubscriber<T>`,
  `E2eClient<Req, Resp>`, and `E2eServer<Req, Resp>` wrap the existing ports,
  keep the zero-copy `loan`/`write`/`send` style, and surface the check result
  next to the sample instead of hiding it.
- **Configuration.** Service entries gain an optional `e2e` section (profile
  and data id); a service without it behaves exactly as today.
- **Diagnostics.** Per-endpoint `to_string` plus counters for checked, failed,
  repeated, and out-of-sequence messages.

Explicitly out of scope: byte-level compatibility with AUTOSAR wire profiles
(there is no shared bus here; both ends use this library), `P01`, E2E for the
internal daemon control channels, and any authentication (E2E detects
corruption and misdelivery, not a malicious sender).

## Capabilities

### New Capabilities

- `com/e2e`: end-to-end protection for events, fields, and methods — profile
  selection, envelope checksum, counter supervision, result reporting to the
  application, and configuration.

### Modified Capabilities

None. The transport types keep their signatures; E2E is an opt-in wrapper over
them, and non-E2E services are untouched.

## Impact

- **Code**: new `include/nexigen/com/e2e/` headers and `src/com/e2e_codec.cpp`;
  `com/service_manifest.hpp` gains the optional section; tests in `tests/com/`.
- **Performance**: one CRC per E2E message on the producer path — table-driven,
  roughly 10–40 ns for a 64–256 byte payload — plus one 16-byte header write.
  No allocation, no extra copy, no syscall. The receiver adds a length/data-id
  compare and one CRC. Non-E2E services pay nothing.
- **Memory**: 16 bytes per E2E sample in shared memory and about 32 bytes of
  per-endpoint counter state. Allocated with the port.
- **Compatibility**: opt-in. A service that adds the `e2e` section changes its
  wire type from `T` to `E2eMessage<T>`, so both ends must be updated together;
  the manifest validation will refuse a publisher/receiver profile mismatch only
  when both sides are declared in the same manifest.
- **Dependencies**: none beyond `com` and `core/integrity`'s table-driven CRC
  style (the AUTOSAR polynomials live in the E2E codec, not in `core`).
