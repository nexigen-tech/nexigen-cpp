# Design

## Context

`nexigen-hub` serves regular JSON/asset responses through `respond()`, which emits a correct
`Content-Length`. The SSE response is built separately in `start_sse` with headers that carry no
`Content-Length` (correct for a stream) but also no `Transfer-Encoding: chunked`. HTTP/1.1
requires a response body to be delimited by `Content-Length`, chunked transfer encoding, or
connection close. The SSE response has none, so strict clients (browsers) cannot stream it:
`EventSource` stays `CONNECTING` (verified: the TCP connection is established and the response
bytes are sent, but the browser never fires `onopen`), and the web UI never leaves its initial
"connecting" state. `curl` and the raw-socket test read bytes leniently and so never caught this.

## Goals / Non-Goals

**Goals:**

- Make the SSE response a valid, incrementally streamable HTTP/1.1 body so a browser's
  `EventSource` connects and the UI transitions out of "connecting".

**Non-Goals:**

- No change to SSE event framing, replay, heartbeats, or the JSON/asset `respond()` path.
- No change to the server's connection lifecycle beyond sending the terminating chunk.

## Decisions

### D1 — Use `Transfer-Encoding: chunked` and frame every SSE write

Add a `chunked` flag to the SSE client, a `Transfer-Encoding: chunked` response header, and an
`append_sse(Client&, string_view)` helper that writes each body piece as
`<size hex>\r\n<data>\r\n`. Route the body writes in `start_sse`, `dispatch_live`, and `timers`
through `append_sse`, and send the terminating `0\r\n\r\n` chunk in `close_client`.

- *Rationale:* chunked encoding is the standard, universally supported way to stream an
  HTTP/1.1 body with no `Content-Length`; browsers deliver each chunk to `EventSource`
  incrementally. A close-delimited body (`Connection: close`) was tried and rejected: the browser
  still held `CONNECTING`, so chunking is the remaining correct mechanism.
- *Alternative considered:* `Connection: close` (close-delimited body). Rejected after empirical
  testing showed the browser still buffers the body.

## Risks / Trade-offs

- [Framing overhead] Each SSE event now carries a small chunk header and trailing CRLF. →
  Mitigation: this is the hub's cold delivery path only; events are already batched per poll.
- [Terminating chunk on error close] A hard close may skip the terminating chunk. → Mitigation:
  browsers treat an unterminated chunked stream on disconnect as a normal `onerror`/reconnect,
  which is the existing SSE behavior.

## Migration Plan

Not applicable — a header/framing bug fix with no persisted format or API change. Deployed hubs
rebuild and restart.

## Open Questions

None.
