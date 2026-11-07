# hub/http-api — internal structure (delta)

## Purpose

This delta specifies how the `hub/http-api` capability is internally reorganized to reduce cyclomatic complexity in the HTTP/SSE server implementation. All endpoint behavior, response formats, and connection lifecycle rules remain unchanged.

## Requirements

### Requirement: `HttpServer::Impl::start` is split into setup phases

The `start` method SHALL decompose into focused helpers: one that parses the listen address, one that creates and binds the listening socket (`bind_listen`), one that creates the epoll/eventfd set, one that registers the listening and wakeup file descriptors with epoll, and one that performs the initial discovery/stream sync. The public `start` SHALL orchestrate these helpers and propagate the first error.

#### Scenario: IPv6 dual-stack binding is isolated

- **WHEN** the listen string requests an IPv6 address
- **THEN** `bind_listen` configures `IPV6_V6ONLY` as required, binds, and listens; the caller only checks the returned status

#### Scenario: Epoll setup failure is reported cleanly

- **WHEN** `epoll_create1` or `eventfd` fails
- **THEN** the helper closes any already-created socket and returns `SystemError`

### Requirement: Client read logic is layered

`read_client` SHALL be split into a raw-read helper that drains the kernel buffer and a request-framing helper that extracts complete HTTP/1.1 request blocks from `client.input`. Request handling SHALL remain delegated to `handle_request`.

#### Scenario: Large request line is detected before routing

- **WHEN** a complete request line exceeds `max_request_line_bytes`
- **THEN** the framing helper responds 400 and stops processing the current request without touching route logic

#### Scenario: Multiple pipelined requests are processed in order

- **WHEN** the input buffer contains two or more complete HTTP requests
- **THEN** each is removed from the buffer and passed to `handle_request` sequentially

### Requirement: The server run loop uses an explicit state stepper

The `run` method SHALL express its event loop as a small set of discrete steps: `collect_events`, `dispatch_event`, `run_timers`, `streaming_poll`, `dispatch_live`, and `drain_outputs`. The steps MAY be inline helpers or an explicit `ServerState` value; in either case the loop body SHALL not contain deeply nested conditionals.

#### Scenario: Stop event cleanly exits the loop

- **WHEN** the eventfd fires
- **THEN** the stepper drains the event and signals loop termination without multiple nested returns

#### Scenario: Listen/read/write/error events are dispatched uniformly

- **WHEN** epoll returns a mix of listen, eventfd, inotify, and client events
- **THEN** each event category is routed through a single dispatch path

### Requirement: Refactored internals preserve all observable behavior

After decomposition, every HTTP endpoint SHALL return the same status, headers, and body for the same request. SSE replay, gap markers, heartbeats, tree revision pushes, and connection limits SHALL behave identically. No public header SHALL gain or lose a symbol.

#### Scenario: Existing HTTP API tests pass unchanged

- **WHEN** the existing tests for health, tree, streams, recent, events, and static assets run
- **THEN** they pass without modification

### Requirement: Complexity of decomposed functions stays within the project target

Every function introduced or modified by this refactor SHALL have cyclomatic complexity ≤ 15 as measured by lizard. The overall average complexity for `src/hub/http_server.cpp` SHALL not increase.

#### Scenario: Post-refactor lizard report

- **WHEN** lizard is run over `src/hub/http_server.cpp`
- **THEN** no function in the file exceeds complexity 15
