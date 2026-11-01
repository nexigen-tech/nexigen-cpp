# Spec Delta

## ADDED Requirements

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
