# Tasks

## 1. Vocabulary, envelope, and codec

- [x] 1.1 Add the `E2eProfile` enum (`None`, `P04`, `P05`, `P06`, `P07`) with the full conversion set; verify each value round-trips through string, JSON, and the formatter.
- [x] 1.2 Add CRC-32 (`0xF4ACFB13`) and CRC-16 (`0x1021`) primitives in the E2E codec, allocation-free and table-driven; verify them against the published AUTOSAR check values and the empty input.
- [x] 1.3 Add `E2eHeader` (16 bytes: data id, counter, length, crc), the `E2eMessage<T>` envelope, the `e2e_bytes` customization point, and the trivially-copyable `Message` assertions; verify `sizeof(E2eHeader)` is 16 and a payload with a custom byte view seals and checks.
- [x] 1.4 Implement `seal` and `check` with length and data-id validation before the CRC; verify a sealed message checks `Ok`, a flipped bit is `Error`, a length mismatch is `Error`, and a foreign data id is `Error`.

## 2. Sender and receiver state

- [x] 2.1 Implement sender counter progression (start at 1, increment, wrap at the profile width); verify three consecutive sends increment and a wrap from the maximum returns to 0.
- [x] 2.2 Implement receiver supervision (`Ok`, `Repeated`, `WrongSequence`) with the configured maximum delta and per-endpoint counters; verify a duplicate is `Repeated`, a gap above the delta is `WrongSequence`, and a valid in-window message does not advance state on a failed CRC.

## 3. Zero-copy ports

- [x] 3.1 Add `E2ePublisher<T>` and `E2eSubscriber<T>` over `E2eMessage<T>` with the loan/write/send shape; verify an end-to-end publish/receive round-trip returns the payload with `Ok` and the allocation counter is unchanged.
- [x] 3.2 Add per-endpoint `to_string` plus free helper and `std::formatter` reporting profile, data id, and the four counters; verify the formatter matches the member output.
- [x] 3.3 Measure the added cost and record it in the capability notes; verify the CRC-plus-header overhead for a 64-byte and a 256-byte payload stays inside the documented budget.

## 4. Method protection

- [x] 4.1 Add `E2eClient<Req, Resp>` and `E2eServer<Req, Resp>` over the enveloped request and response types; verify a protected call round-trips with `Ok` on both sides.
- [x] 4.2 Pass the request status to the server handler and seal the response independently; verify a corrupted request reaches the handler as a failure and the client still receives a well-formed response envelope.

## 5. Configuration, docs, and verification

- [x] 5.1 Add the optional `e2e` section (profile, data id, max delta) to provided and required service entries with validation; verify a declared section is parsed, an absent section keeps plain communication, and an unknown profile fails the parse.
- [x] 5.2 Document the envelope, profile set, counter rules, padding caveat, and the both-ends-together migration in `docs/`, and update the traceability rows for `RS_CM_00223`, `RS_CM_00224`, `RS_CM_00400`, `RS_CM_00401`, `RS_CM_00404`, `RS_SAF_10014`, `RS_SAF_10039`, and `RS_SAF_10041`; verify the traceability gate exits 0.
- [x] 5.3 Run `openspec validate --changes --strict` and `openspec validate --specs`; verify both exit 0.

## Workflow follow-up

- Archive the change after review.
- Verify the archived capability `com/e2e` exists under `openspec/specs/`.
