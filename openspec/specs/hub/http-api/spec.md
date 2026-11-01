# hub/http-api Specification

## Purpose

Defines the local HTTP/1.1 API that exposes the hub tree, stream descriptors, recent records, and live Server-Sent Events, including endpoint shapes, limits, errors, and localhost-only access.

## Requirements

### Requirement: HTTP listener is loopback by default

`nexigen-hub` SHALL listen on `127.0.0.1:8080` by default and SHALL accept `--listen <host>:<port>` as an override. It SHALL provide no authentication. Binding to a non-loopback address SHALL be an explicit operator choice and SHALL emit a warning at startup.

#### Scenario: Default listener is localhost

- **WHEN** the hub starts without `--listen`
- **THEN** it accepts connections on `127.0.0.1:8080` only

#### Scenario: Listen override is honored

- **WHEN** `--listen 127.0.0.1:9000` is supplied
- **THEN** the server binds that address and port

#### Scenario: Non-loopback bind warns

- **WHEN** `--listen 0.0.0.0:8080` is supplied
- **THEN** the server starts and emits a warning that no authentication is configured

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
- **THEN** the response is `200` with `Content-Type: text/event-stream` and no `Content-Length`

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
