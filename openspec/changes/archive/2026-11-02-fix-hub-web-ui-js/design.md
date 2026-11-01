# Design

## Context

`app_js()` in `src/hub/web_assets.cpp` returns the UI script as a C++ raw string. The script is
meant to be an immediately-invoked arrow function, but the opening wrapper parenthesis is missing:
the raw content is `() => { ... })();` instead of `(() => { ... })();`. A bare arrow function
followed by `();` is a JavaScript syntax error, so the browser loads `app.js` and aborts parsing;
no `EventSource` is created, no `fetch("/api/tree")` runs, and the badge stays on the HTML's
hardcoded `connecting`. Server-side tests (curl, raw socket, Python) never parse the script, so
the defect went unnoticed.

## Goals / Non-Goals

**Goals:**

- Make the embedded script syntactically valid so it bootstraps the SSE tree subscription and the
  initial tree fetch, moving the badge off `connecting`.

**Non-Goals:**

- No UI behavior or layout changes beyond the one-character syntax fix.
- No change to the SSE protocol or server (chunked framing is handled by a separate change).

## Decisions

### D1 — Add the missing IIFE wrapper parenthesis

Change `return R"JS(() => {` to `return R"JS((() => {` so the script is `(() => { ... })();`.

- *Rationale:* the script already closes with `})();`, so the matching opening parenthesis is the
  only missing piece; this is the minimal, surgical fix.
- *Alternative considered:* wrap the body in a named function or use a `<script type="module">`.
  Rejected as larger changes for no benefit; the IIFE is the established pattern.

## Risks / Trade-offs

- [Regression not caught by server tests] The server tests do not parse the UI script. →
  Mitigation: add a `web_assets` test assertion that pins the IIFE opening `(() => {` and closing
  `})();`, so a missing wrapper paren fails the test.

## Migration Plan

Not applicable — static embedded text; a rebuild and restart picks up the fix.

## Open Questions

None.
