# Proposal

## Why

`nexigen-hub`'s Server-Sent Events response carries no `Content-Length` and no
`Transfer-Encoding: chunked`. That is an undelimited HTTP/1.1 body: strict clients such as
browsers cannot determine where the streamed body ends, so `EventSource` stays in its
`CONNECTING` state, the `tree` event never fires, and the embedded web UI shows "connecting"
forever. `curl` and the raw-socket tests tolerate the ambiguity, so the bug was invisible until
a real browser pointed at a live hub.

## What Changes

- Emit the SSE body with `Transfer-Encoding: chunked` and frame every SSE write as an
  HTTP/1.1 chunk, so browsers stream the event body incrementally.
- Update the `hub/http-api` spec so the SSE header contract requires chunked transfer encoding.
- Extend the HTTP test to assert the `Transfer-Encoding: chunked` header on the SSE response.

## Capabilities

### Modified Capabilities

- `hub/http-api`: require the SSE response to use `Transfer-Encoding: chunked` alongside
  `Content-Type: text/event-stream` and no `Content-Length`.

## Impact

- Changes the SSE header and write framing in `src/hub/http_server.cpp` (a `chunked` client flag,
  an `append_sse` helper, and the `start_sse`/`dispatch_live`/`timers`/`close_client` call sites)
  plus a test assertion in `tests/hub/hub_test.cpp`. No public API, serialization, or data-model
  change.
- Performance, latency, and memory allocation: the chunk framing adds one small `std::string`
  append per SSE event on the hub's cold delivery path only; no hot-path, ring, or discovery code
  is touched.
