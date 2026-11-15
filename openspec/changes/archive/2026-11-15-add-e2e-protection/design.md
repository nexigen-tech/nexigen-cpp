# Design

## Context

See `proposal.md` — Why. Transport facts that shape the design:

- `Publisher<T, H>` and `Subscriber<T, H>` already support an optional iceoryx2
  user header, but `Client<Req, Resp>` and `Server<Req, Resp>` do not.
- Payloads are trivially copyable C++ objects living in shared memory; there is
  no serialization step and no byte-level wire contract beyond the struct
  layout.
- `send_copy()` cannot attach a user header; the header path is
  `loan_uninit()` → `write_payload()` → `user_header_mut()` → `send()`.
- `ServiceManifest` already describes provided and required services and is the
  natural place for per-service parameters.
- Change `add-integrity-primitives` supplies table-driven CRC style and the
  allocation-free contract, but not the AUTOSAR polynomials.

## Goals / Non-Goals

**Goals:**

- The same protection for events, fields, and methods without touching the
  transport templates.
- A check result the application can act on, not a silent drop.
- Zero allocation and zero extra copies on the publish path.
- Opt-in per service; plain services keep their current types.

**Non-Goals:**

- Byte-level AUTOSAR wire-profile compatibility (no shared bus; both ends use
  this library).
- `P01` (4-bit packed counter) and profile negotiation.
- Authentication, freshness beyond the counter, or replay protection across
  restarts.
- Protecting the internal daemon control channels.

## Decisions

**D1 — A message envelope, not a transport header.** `E2eMessage<T>` is
`{ E2eHeader header; T payload; }`, trivially copyable, so it satisfies the
existing `Message` concept. This covers methods — which have no header support —
with the same mechanism as events, and requires zero changes to `Publisher`,
`Subscriber`, `Client`, or `Server`.
*Alternative rejected:* the iceoryx2 user header — it works for pub/sub only and
would split the design into two incompatible halves.

**D2 — Our framing, AUTOSAR CRC polynomials.** The header is fixed at 16 bytes
(`data_id`, `counter`, `length`, `crc`); the checksum uses the AUTOSAR
polynomials for the selected profile. Byte-level compatibility with AUTOSAR
wire profiles is meaningless without a shared bus, and pretending otherwise
would freeze a format we cannot test against a real stack. The polynomials are
implemented in the E2E codec, not in `core/integrity`, because they are a
protocol detail with a different error model.
*Alternative rejected:* P01-style bit packing now — 4-bit counters in a
byte-aligned local transport buy nothing and cost clarity.

**D3 — The payload byte view is a customization point.** The codec hashes
`e2e_bytes(payload)`, defaulting to the object representation
(`std::as_bytes` of the value). A payload with padding can specialize the
function to return a stable view, and the docs say so; for the common
all-`uintN_t`/enum/array payloads the default is byte-stable.
*Alternative rejected:* canonical serialization — it would add a copy and a
second wire format to a zero-copy transport.

**D4 — `Repeated` is distinct from `WrongSequence`.** ISO 26262 D.2.4 lists
repetition and incorrect sequence as separate failure modes, and applications
treat them differently (a duplicate may be harmless, a gap usually is not).
Collapsing them would lose information the receiver can act on.

**D5 — Counter rules.** The sender starts at 1 and increments per sealed
message modulo the profile width. The receiver accepts `last + 1` through
`last + max_delta` as `Ok`, an exact repeat as `Repeated`, and anything else as
`WrongSequence`; the data id and CRC are checked before the counter, because a
wrong-id or corrupt message must not advance sequence state.

**D6 — Wrappers, not template flags.** `E2ePublisher<T>` owns a
`Publisher<E2eMessage<T>>`; `E2eSubscriber<T>` owns the matching subscriber and
returns a sample plus `E2eStatus`/`E2eState`. This keeps every existing signature
and lets a codebase migrate one service at a time. The wrappers expose the same
`loan`/`write`/`send` shape so the hot path stays zero-copy: the caller writes
the payload in place, `send` seals it.
*Alternative rejected:* a `Policy` template parameter on `Publisher` — it would
touch every instantiation and every test for an opt-in feature.

**D7 — Methods use the envelope on both directions.** `E2eServer` hands the
handler the request payload *and* the request's E2E status; the handler decides
what to return, and the server seals the response. The client checks the
response and reports its status. Nothing is echoed automatically: silently
copying a failure into the response would let a broken server look healthy.

**D8 — `max_delta_counter` defaults per profile** (a small constant, for example
`10`) and is overridable in the manifest. A conservative default catches gaps
without producing false `WrongSequence` on a busy event stream.

## Risks / Trade-offs

- **Padding in payloads makes the CRC unstable.** → Customization point plus a
  documented rule; a test seals and checks a representative payload set and the
  docs list the requirement.
- **Mismatched profiles between endpoints turn every message into `Error`.** →
  Expected and visible in the failure counters; there is no negotiation, and the
  docs state that both sides must be configured together.
- **The 16-byte envelope reduces shared-memory density.** → Measure the per-
  sample cost and document the effect on history/buffer sizing.
- **A single `E2eMessage<T>` type change is a wire break for that service.** →
  Opt-in per service; the manifest section is the migration trigger, and a
  service without it is untouched.
- **CRC does not protect against a malicious writer.** → Stated in the purpose
  and docs; authentication is out of scope.

## Migration Plan

Opt-in and reversible. Add the `e2e` section to a service, switch both endpoints
to the wrappers in the same change, and run the suite. Removing the section and
the wrappers restores plain communication. No storage or manifest migration.

## Open Questions

- Adding `P01` and profile negotiation if a real AUTOSAR-facing transport
  appears; the envelope and codec interfaces already accommodate both without a
  breaking change.
