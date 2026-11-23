# Spec Delta

## MODIFIED Requirements

### Requirement: Channel configuration and limits

`ChannelConfig` SHALL define `max_consumers` in `1..16`, `max_publishers` in `1..16` (default 1), `queue_size` (default 512), `history_size` (default 16, not exceeding the queue), and a default timestamp domain. Invalid values SHALL be rejected before any service is created. `EventChannel<T>` SHALL use 16 consumers, one publisher, 512/16 queue/history when no configuration is supplied.

#### Scenario: Defaults match the event contract

- **WHEN** an `EventChannel<T>` is created without a configuration
- **THEN** it requests at most 16 consumers, one publisher, a 512-sample queue, and 16 history samples

#### Scenario: Invalid limits are rejected

- **WHEN** `max_consumers` is zero or greater than 16
- **THEN** channel creation returns `InvalidArgument`

#### Scenario: Invalid publisher limit is rejected

- **WHEN** `max_publishers` is zero or greater than 16
- **THEN** channel creation returns `InvalidArgument`

#### Scenario: History cannot exceed the queue

- **WHEN** `history_size` is greater than `queue_size`
- **THEN** channel creation returns `InvalidArgument`

#### Scenario: Service limits follow the configuration

- **WHEN** a channel is created with 16 consumers and one publisher
- **THEN** the data service requests 16 subscribers, 1 publisher, 17 nodes, and safe overflow with history 16

#### Scenario: Two publishers share one service

- **WHEN** a status channel is created with `max_publishers=2`
- **THEN** the data service accepts two publishers and 18 nodes, and both producers can publish
