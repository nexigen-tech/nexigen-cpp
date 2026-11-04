# hub/http-api delta spec

## MODIFIED Requirements

### Requirement: HTTP listener is loopback by default

`nexigen-hub` SHALL listen on `127.0.0.1:8080` by default and SHALL accept
`--listen <host>:<port>` as an override. IPv4 literals and the `localhost`
name SHALL bind `AF_INET` exactly as before; IPv6 addresses SHALL be accepted
in the RFC bracket form `[<addr>]:<port>` and, unambiguously, as bare IPv6
literals followed by an explicit port (`<addr>:<port>` where `<addr>` contains
at most one colon). A specific IPv6 address binds `AF_INET6` with
`IPV6_V6ONLY=1`; `::` binds an `AF_INET6` socket with `IPV6_V6ONLY=0`, which
double-publishes unmatched IPv4 traffic over the same port. The CLI loopback
classification SHALL NOT truncate an IPv6 literal (the loopback set covers
`127.0.0.1`, `localhost`, and `::1`) before deciding to warn. The hub SHALL
provide no authentication; binding to a non-loopback address SHALL be an
explicit operator choice and SHALL emit a warning at startup.

#### Scenario: Default listener is localhost

- **WHEN** the hub starts without `--listen`
- **THEN** it accepts connections on `127.0.0.1:8080` only

#### Scenario: Listen override is honored

- **WHEN** `--listen 127.0.0.1:9000` is supplied
- **THEN** the server binds that address and port

#### Scenario: Bracketed IPv6 listener is honored

- **WHEN** `--listen [::1]:9080` is supplied
- **THEN** the server binds the IPv6 loopback on port 9080, and no
  non-loopback warning is emitted

#### Scenario: Full-dual-stack wildcard binds both families

- **WHEN** `--listen [::]:8080` (or `--listen ::` with an explicit port form it
  parses as) is supplied
- **THEN** the server binds `AF_INET6` with dual-stack enabled and emits the
  non-loopback warning

#### Scenario: Non-loopback bind warns

- **WHEN** `--listen 0.0.0.0:8080` is supplied
- **THEN** the server starts and emits a warning that no authentication is
  configured

#### Scenario: Invalid address fails startup

- **WHEN** `--listen` is malformed or the port is already in use
- **THEN** startup returns an error and the process exits non-zero
