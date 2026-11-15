# Design

## Context

See `proposal.md` — Why. The E2E layer is typed: `E2ePublisher<T>` publishes
`E2eMessage<T>`, which always contains a 16-byte `E2eHeader`. The profile is a
runtime value, so a wrapper cannot drop the header type at runtime.

## Goals / Non-Goals

**Goals:**

- Make "`None` = no protection" unambiguous in both the spec and the API.
- Fail loudly instead of sending a header-only envelope that protects nothing.
- Keep the protected paths byte-for-byte unchanged.

**Non-Goals:**

- A headerless mode inside the E2E wrappers.
- Changing the envelope layout or the profile set.

## Decisions

**D1 — Reject `None` at construction.** A wrapper is a protection primitive; if
no protection is configured, the plain ports are the correct tool. Checking the
profile in `create()` — before `Publisher::create`/`Subscriber::create`/
`Client::create`/`Server::create` — guarantees no service is opened for a
degenerate configuration.
*Alternative rejected:* a headerless mode inside `E2ePublisher<T>`. The envelope
is a compile-time property of the type; supporting both would need a variant,
type erasure, or two parallel wrapper families for a case the plain ports
already cover.

**D2 — `None` stays in the vocabulary.** Configuration needs a default: an
absent `e2e` section is `None`, and `valid_e2e_config` uses it to skip the data
id requirement. Removing the enumerator would make "no section" unrepresentable.

**D3 — The error is `InvalidArgument`.** The caller supplied a profile that is
invalid for this API, not a transport failure. `create()` already returns
`com::Result`, so the error travels the normal path.

**D4 — `publish` writes into the slot.** `publish(const T&)` loans a slot,
default-constructs the envelope in place, copies the payload once, seals there,
and sends. The previous stack envelope plus `send_copy` made two payload copies;
one copy is the floor for a `const T&` API and matches plain `send_copy`.
*Alternative rejected:* a variadic in-place constructor — the transport's
`write_payload` takes a complete value, so a temporary would still be copied.

**D5 — Parse findings live on the manifest.** `E2eConfig` stays a plain value
(profile, data id, maximum delta). `from_json` records an unknown profile name
in `ServiceManifest::diagnostics`, and `valid_service_manifest(manifest, error)`
returns it. This follows the settings-loader pattern: value types carry data,
the manifest carries best-effort findings.
*Alternative rejected:* a flag inside `E2eConfig` — it made every consumer of a
configuration value type carry parse state.

**D6 — The measured cost is the contract.** `e2e_overhead` enforces 1 us / 2.5 us
in Release and 5 us / 10 us in sanitizer builds; the architecture note records
the measured numbers. A future slicing-by-8 or hardware CRC may lower them
without changing the API.

## Risks / Trade-offs

- **A caller that relied on `None` wrappers now fails.** → Desired: the previous
  behavior was a silent no-op with 16 wasted bytes. The plain ports are the
  documented replacement.
- **Two ways to "disable" (plain ports vs `None` in config) could confuse.** →
  The config comment and the architecture section state the rule in one sentence.

## Migration Plan

Callers using `E2ePublisher<T>`/`E2eSubscriber<T>` with `None` switch to
`Publisher<T>`/`Subscriber<T>`; method wrappers switch to `Client`/`Server`.
No manifest or wire-format migration.
