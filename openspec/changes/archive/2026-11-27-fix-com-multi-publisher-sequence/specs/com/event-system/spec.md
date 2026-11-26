# Spec Delta

## ADDED Requirements

### Requirement: Multi-publisher sequence tracking

When `max_publishers` is greater than one, `EventConsumer` SHALL track the transport sequence and producer epoch independently per `app_id`. Interleaved publishers SHALL NOT report `ProducerRestart`. An epoch change for one `app_id` SHALL report `ProducerRestart`; gaps, duplicates, and unexpected resets SHALL be detected per publisher. With `max_publishers=1`, the single-sequence behaviour SHALL apply unchanged.

#### Scenario: Interleaved publishers do not restart

- **WHEN** two producers with different application codes alternately publish on a channel created with `max_publishers=2`
- **THEN** the consumer delivers every sample without calling the loss callback

#### Scenario: One publisher restarts

- **WHEN** one of the two producers changes its epoch
- **THEN** the loss callback receives `ProducerRestart` for that publisher with the old and new epoch

#### Scenario: Gaps stay per publisher

- **WHEN** one publisher skips a transport sequence while the other publisher keeps publishing in between
- **THEN** the loss callback receives `TransportGap` with that publisher's exact lost count

#### Scenario: New publisher establishes its own baseline

- **WHEN** a previously unseen application code publishes on a multi-publisher channel
- **THEN** its first sample establishes that publisher's expected sequence without a loss callback

#### Scenario: Single-publisher channel is unchanged

- **WHEN** the channel is created with `max_publishers=1` and the producer epoch changes
- **THEN** the loss callback still receives `ProducerRestart` with the old and new epoch
