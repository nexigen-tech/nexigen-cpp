# Proposal

## Why

The hub's embedded `app.js` is wrapped in an immediately-invoked arrow function whose opening
wrapper parenthesis is missing: the script is `() => { ... })();` instead of
`(() => { ... })();`. That is a JavaScript syntax error, so the browser never executes the
script: the tree never renders and the connection badge stays on its hardcoded `connecting` state
forever. The bug was invisible to the server-side and raw-socket tests (which never parse or run
the UI script), so it only surfaced when a real browser opened the hub.

## What Changes

- Fix the embedded `app.js` so the IIFE is well-formed (`(() => { ... })();`).
- Update the `hub/web-ui` spec so the embedded script must parse and bootstrap.
- Extend the `web_assets` test to pin the IIFE structure, catching the missing-paren class of bug.

## Capabilities

### Modified Capabilities

- `hub/web-ui`: require the embedded script to be valid, executable JavaScript that connects the
  SSE tree subscription and renders the discovered tree.

## Impact

- One character in `src/hub/web_assets.cpp` (add the missing IIFE wrapper parenthesis) plus a
  `tests/hub/hub_test.cpp` assertion. No public API, serialization, or data-model change.
- Performance, latency, and memory allocation: none — this is static embedded UI text; the fix
  changes no runtime path, hot path, or serving logic.
