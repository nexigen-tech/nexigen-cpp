# hub/http-api Specification

## Purpose

Defines the local HTTP/1.1 API that exposes the hub tree, stream descriptors, recent records, and live Server-Sent Events, including endpoint shapes, limits, errors, and localhost-only access.

## Requirements

### Requirement: HTTP listener is loopback by default

`nexigen-hub` SHALL listen on `127.0.0.1:8080` by default and SHALL accept
`--listen <host>:<port>` as an override, binding IPv4 literals and `localhost`
over `AF_INET` exactly as before and IPv6 literals over `AF_INET6` (see the
address-form scenarios). A binding to a non-loopback address SHALL be an
explicit operator choice and SHALL emit a no-authentication warning at
startup. The hub SHALL provide no authentication.

#### Scenario: Default listener is localhost

- **WHEN** the hub starts without `--listen`
- **THEN** it accepts connections on `127.0.0.1:8080` only

#### Scenario: Listen override is honored

- **WHEN** `--listen 127.0.0.1:9000` is supplied
- **THEN** the server binds that address and port

#### Scenario: IPv6 address forms bind AF_INET6

- **WHEN** `--listen [::1]:9080` is supplied (full multi-colon literals use the
  RFC bracket form; a bare `<addr>:<port>` is accepted only when the address
  part holds at most one colon, and ambiguous forms fail as malformed)
- **THEN** the server binds the IPv6 loopback on port 9080 and emits no
  non-loopback warning

#### Scenario: Full-dual-stack wildcard binds both families

- **WHEN** `--listen [::]:8080` (or `--listen ::` with an explicit port form it
  parses as) is supplied
- **THEN** the server binds `AF_INET6` with `IPV6_V6ONLY=0` — IPv4 traffic
  double-publishes on the same port — and emits the non-loopback warning; a
  specific IPv6 address binds with `IPV6_V6ONLY=1`

#### Scenario: Loopback classification does not truncate IPv6 literals

- **WHEN** `--listen ::1` or `[::1]:PORT` is parsed
- **THEN** the host reads as `::1` (the loopback set covers `127.0.0.1`,
  `localhost`, and `::1`) and no non-loopback warning is emitted

#### Scenario: Non-loopback bind warns

- **WHEN** `--listen 0.0.0.0:8080` is supplied
- **THEN** the server starts and emits a warning that no authentication is
  configured

#### Scenario: Invalid address fails startup

- **WHEN** `--listen` is malformed or the port is already in use
- **THEN** startup returns an error and the process exits non-zero

### Requirement: Read-only JSON endpoints

The hub SHALL serve `GET /api/health`, `GET /api/tree`, `GET /api/streams`, and `GET /api/streams/<id>/recent?limit=<N>`. Successful JSON responses SHALL use `application/json` and the structures defined by `hub/discovery` and `hub/streaming`. The recent endpoint SHALL default to the whole ring and cap `limit` at the ring capacity.

#### Scenario: Health reports readiness

- **WHEN** `GET /api/health` is requested after startup
- **THEN** the response is `200` with a JSON status object

#### Scenario: Tree is returned

- **WHEN** `GET /api/tree` is requested
- **THEN** the response contains the current run/suite/app/instance/attempt tree with states

#### Scenario: Stream list is returned

- **WHEN** `GET /api/streams` is requested
- **THEN** the response contains every discovered stream descriptor

#### Scenario: Recent records are limited

- **WHEN** `GET /api/streams/<id>/recent?limit=10` is requested
- **THEN** at most ten newest records are returned in sequence order

#### Scenario: Unknown stream is not found

- **WHEN** the recent endpoint names an unknown stream
- **THEN** the response is `404` with a JSON error

### Requirement: Live updates use Server-Sent Events

`GET /api/events?stream=<id>` SHALL return `text/event-stream`. The stream SHALL begin with a replay of the ring bounded by the subscriber's sequence cursor, emit a snapshot boundary, then emit live records. Events SHALL use named SSE events and JSON data. Omitting `stream` SHALL subscribe to all streams; `tree=1` SHALL subscribe to tree snapshots instead.

#### Scenario: SSE headers are correct

- **WHEN** a client requests `/api/events?stream=<id>`
- **THEN** the response is `200` with `Content-Type: text/event-stream`, no `Content-Length`, and `Transfer-Encoding: chunked` so the body is streamed incrementally

#### Scenario: Replay then live

- **WHEN** a client subscribes to a stream with recent records
- **THEN** it receives replay events, a snapshot boundary, and then live events

#### Scenario: All streams are available

- **WHEN** `/api/events` omits the `stream` parameter and `tree`
- **THEN** events from every stream are delivered with their stream identities

#### Scenario: Tree snapshots are streamed

- **WHEN** a client subscribes to `/api/events?tree=1`
- **THEN** it receives tree snapshot events as discovery changes the tree and no stream records

#### Scenario: Heartbeat keeps the connection alive

- **WHEN** no records arrive for the heartbeat interval
- **THEN** the server emits an SSE comment or heartbeat event

#### Scenario: Slow client is closed

- **WHEN** an SSE client stops draining its socket
- **THEN** the server closes that connection without affecting other clients

### Requirement: HTTP parsing and errors are bounded

The server SHALL accept only `GET`, SHALL require `HTTP/1.0` or `HTTP/1.1`, and SHALL enforce limits on request line, headers, URL, and concurrent clients. Malformed requests SHALL return `400`, unknown paths `404`, and unsupported methods `405`, all with JSON error bodies where a response can be sent.

#### Scenario: Unsupported method is rejected

- **WHEN** `POST /api/tree` is requested
- **THEN** the response is `405` and no state changes

#### Scenario: Unknown path is not found

- **WHEN** `GET /api/missing` is requested
- **THEN** the response is `404` with a JSON error

#### Scenario: Oversized request is rejected

- **WHEN** the request line or headers exceed the configured limit
- **THEN** the server returns `400` or closes the connection without unbounded buffering

#### Scenario: Keep-alive is supported

- **WHEN** a client sends two sequential GET requests on one HTTP/1.1 connection
- **THEN** both receive valid responses

### Requirement: Web assets are served by the binary

The hub SHALL serve the embedded UI at `GET /`, `GET /index.html`, `GET /app.js`, and `GET /style.css` with correct content types. The assets SHALL be part of the binary and SHALL NOT require external network requests or a build step.

#### Scenario: Index is served

- **WHEN** `GET /` is requested
- **THEN** `index.html` is returned with `text/html`

#### Scenario: Assets are served

- **WHEN** `GET /app.js` or `GET /style.css` is requested
- **THEN** the embedded asset is returned with its content type

#### Scenario: Assets work offline

- **WHEN** the browser loads the UI without internet access
- **THEN** the UI loads and functions from the binary only

### Requirement: The API is observation-only

The hub SHALL NOT expose endpoints that start, stop, restart, signal, or modify applications or runner metadata. Methods other than `GET` SHALL be rejected.

#### Scenario: No control endpoint exists

- **WHEN** a client requests any start/stop/restart path
- **THEN** the response is `404` or `405` and no process is affected

#### Scenario: State files are not modified

- **WHEN** API requests are served
- **THEN** no runner state or metadata file is created, changed, or deleted

### Requirement: HTTP serving is cold path only

HTTP parsing, JSON rendering, SSE delivery, and asset serving SHALL run in the hub control loop and MUST NOT add work to application hot paths.

#### Scenario: Application ticks are untouched

- **WHEN** a supervised application runs its tick loop
- **THEN** no HTTP parsing, JSON rendering, or SSE write occurs in that loop

### Requirement: Request headers are matched case-insensitively

The server SHALL match HTTP request header names case-insensitively and SHALL
ignore optional whitespace around a header value. The `Last-Event-ID` header
SHALL supply the replay cursor when the `since` query parameter is absent,
regardless of the header name's casing, and SHALL be ignored when its value is
not a decimal sequence number.

#### Scenario: Lowercase header resumes the stream

- **WHEN** an SSE request sends `last-event-id: <stream>:2`
- **THEN** replay starts after sequence 2

#### Scenario: Mixed-case header resumes the stream

- **WHEN** an SSE request sends `LAST-EVENT-ID: <stream>:2`
- **THEN** replay starts after sequence 2

#### Scenario: Header whitespace is tolerated

- **WHEN** an SSE request sends `Last-Event-ID:    <stream>:2   `
- **THEN** replay starts after sequence 2

#### Scenario: Invalid cursor is ignored

- **WHEN** an SSE request sends a non-numeric `Last-Event-ID` value
- **THEN** the server treats the cursor as zero and still opens the stream

#### Scenario: A similarly named header is not used

- **WHEN** a request sends `X-Last-Event-ID: <stream>:2` without `Last-Event-ID`
- **THEN** the value does not affect the replay cursor

### Requirement: Control-plane refresh failures are reported

The hub SHALL report discovery rescan and stream reconciliation failures to a
diagnostic sink instead of discarding them. With no sink configured the server
SHALL remain silent.

#### Scenario: Rescan failure is reported

- **WHEN** a discovery rescan fails during a periodic or dirty-tree refresh
- **THEN** the diagnostic sink receives a message that identifies the failed operation

#### Scenario: Stream sync failure is reported

- **WHEN** stream reconciliation cannot open or refresh a stream
- **THEN** the diagnostic sink receives a message that identifies the affected stream

#### Scenario: A failing refresh does not stop serving

- **WHEN** a refresh fails
- **THEN** the server keeps its listener open and continues serving subsequent requests

### Requirement: Malformed requests are rejected consistently

The server SHALL reject malformed requests with a bounded status response and
SHALL NOT mutate discovery or stream state while rejecting them.

#### Scenario: Request line has no target

- **WHEN** a client sends `GET\r\n\r\n`
- **THEN** the response is `400`

#### Scenario: HTTP version is unsupported

- **WHEN** a client sends `GET /api/health HTTP/2.0`
- **THEN** the response is `400`

#### Scenario: Request line exceeds its limit

- **WHEN** a client sends a request line longer than 4096 bytes
- **THEN** the response is `400` or the connection closes without unbounded buffering

#### Scenario: URL cannot bypass the request-line limit

- **WHEN** a client requests a path longer than the request-line budget
- **THEN** the response is `400` before any path handling occurs

#### Scenario: Invalid percent escape is treated literally

- **WHEN** a recent-stream request contains an invalid percent escape
- **THEN** the server does not crash and answers with a bounded response

#### Scenario: Invalid limit and cursor values are ignored

- **WHEN** a request supplies a non-numeric `limit` or `since` value
- **THEN** the server applies the documented defaults instead of failing

#### Scenario: Unsupported method does not change state

- **WHEN** a client sends `POST`, `PUT`, `DELETE`, or `PATCH`
- **THEN** the response is `405` and no stream or discovery state changes
