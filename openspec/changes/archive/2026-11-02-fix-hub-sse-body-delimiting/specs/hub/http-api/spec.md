# Spec Delta

## MODIFIED Requirements

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
