# Proposal

## Why

Verification of the archived `add-e2e-protection` change produced one critical
ambiguity and two follow-ups:

1. `E2eProfile::None` was ambiguous: the spec says "messages carry no E2E
   header", but a wrapper created with `None` still sends the 16-byte
   `E2eMessage<T>` envelope with zeroed fields and simply skips the checksum.
2. `E2ePublisher::publish` built a stack envelope and then copied it into shared
   memory — two payload copies, while the design promised no additional copy.
3. The measured seal+check cost (175 ns / 790 ns in Release) was not recorded as
   the performance contract, so a regression would not be caught.
4. `E2eConfig::profile_unknown` kept a parse diagnostic inside a value type.

## What Changes

- Clarify the vocabulary: `None` means **plain communication** — the application
  uses the ordinary `Publisher`/`Subscriber`/`Client`/`Server` ports and no E2E
  envelope exists at all. The E2E wrappers are for protected services only.
- Make the wrappers enforce that: `E2ePublisher`, `E2eSubscriber`, `E2eClient`,
  and `E2eServer` creation SHALL return `InvalidArgument` when the configured
  profile is `None`, before any transport port is opened.
- Rewrite the "None disables protection" scenario and add a requirement covering
  the rejection.
- Document the rule in the E2E section of `docs/architecture.md`.
- Make `E2ePublisher::publish` write the payload directly into the shared-memory
  slot: one copy, no intermediate envelope.
- Treat the measured seal+check cost as the contract and enforce it in
  `e2e_overhead` (1 us / 2.5 us in Release, 5 us / 10 us in sanitizer builds).
- Move the unknown-profile diagnostic out of `E2eConfig` into
  `ServiceManifest::diagnostics`, surfaced by `valid_service_manifest`.

Existing protected services (`P04`–`P07`) are unaffected. A caller that
previously created a wrapper with `None` now gets an error instead of a silent
no-op envelope.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `com/e2e`: `None` is defined as plain communication, and wrapper construction
  rejects it; a new requirement makes the guard testable.

## Impact

- **Code**: the four `create` factories in
  `include/nexigen/com/e2e/e2e_{publisher,subscriber,client,server}.hpp` gain a
  profile check before opening the transport; `E2ePublisher::publish` writes in
  place; `E2eConfig` drops the parse flag in favour of
  `ServiceManifest::diagnostics`.
- **Performance**: zero hot-path impact from the guard (one enum comparison on
  the cold creation path). `publish` drops from two payload copies to one; the
  measured cost is now the enforced contract.
- **Memory**: none.
- **Compatibility**: a wrapper constructed with `None` now fails with
  `InvalidArgument`; apps that want no protection should use the plain ports.
  No wire-format or manifest change.
