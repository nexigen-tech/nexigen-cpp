# Design

## Context

See `proposal.md` — Why. Settings are directories (`global_settings_path`,
per-app `local_settings_path`) holding `logging.json`, `metrics.json`,
`tracing.json`, and `chrono.json`. The suite supervisor parses the manifest and
forks the applications; the applications read the files themselves during
`Infrastructure::initialize`.

## Goals / Non-Goals

**Goals:**

- Verify configuration bytes before any application is launched.
- Reuse the single-read `read_verified` helper (no second read inside the check).
- Keep the check off every runtime path.

**Non-Goals:**

- Read-time verification inside each settings loader.
- Periodic re-verification while the suite runs.
- Directory-level manifests or sidecar files.

## Decisions

**D1 — A list of `{path, sha256}` in the suite manifest.** The manifest is the
deployment artifact and can itself be digest-gated (`--manifest-sha256`), so the
expected digests travel with the deployment. A sidecar next to each file is
easier to tamper with; an env-var map would be invisible in review.
*Alternative rejected:* per-file sidecars and per-file CLI flags.

**D2 — Verify once, before the first launch.** The suite supervisor checks every
listed file in `start_suite_and_report`, before `start_suite_apps`. A mismatch
reports `SecurityError` on the start pipe, so `start` fails and nothing is
forked. This matches the existing executable-digest gate: verification happens
in the parent, then the child reads the same files.
*Alternative rejected:* verifying in each application loader — it would thread a
digest map through four module loaders, and the child already trusts the
supervisor's gate.

**D3 — Absolute paths and strict hex.** Relative paths are ambiguous under the
run directory; malformed digests are deployment errors, not warnings.

## Risks / Trade-offs

- **TOCTOU between the check and the child's read.** → The window is the same
  one the executable digest gate accepts; a stronger model needs read-time
  verification in the loaders (deferred).
- **A large settings list slows startup.** → One SHA-256 per file, cold path;
  settings files are kilobytes.
- **The section can be omitted.** → That is the compatible default; the matrix
  states that a deployment without it is unverified.

## Migration Plan

Additive. Deployments add the section with `sha256sum` output; removing it
restores the previous behavior.
