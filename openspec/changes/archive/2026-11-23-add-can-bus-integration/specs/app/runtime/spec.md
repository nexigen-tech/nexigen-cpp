# Spec Delta

## RENAMED Requirements

- FROM: `### Requirement: One event input and an optional event output`
- TO: `### Requirement: One or more event inputs and an optional event output`

## MODIFIED Requirements

### Requirement: One or more event inputs and an optional event output

`App` SHALL expose `subscribe<T>(service, callback)` for any trivially copyable payload types and `publish<T>(service)` for optional outputs. It SHALL accept up to sixteen subscriptions, each with its own payload type, all dispatched on the `run()` thread. The subscriptions and borrowed producer handles SHALL stamp the application `app_id`, and `run()` SHALL wait on all inputs through a bounded `EventConsumerSet`/WaitSet timeout so an idle application still observes stop requests.

#### Scenario: Input is delivered to the callback

- **WHEN** an event arrives on a subscribed service
- **THEN** the registered callback runs on the `run()` thread with the payload and event header

#### Scenario: Multiple subscriptions are accepted

- **WHEN** the application subscribes to a second service with its own payload type
- **THEN** `subscribe` returns `true`, both services are attached, and their callbacks are dispatched on the `run()` thread

#### Scenario: Seventeenth subscription is rejected

- **WHEN** the application already holds sixteen subscriptions and requests a seventeenth
- **THEN** `subscribe` returns `false`, records a diagnostic naming the service, and the first sixteen remain active

#### Scenario: Output carries the application identity

- **WHEN** the callback uses the publisher handle
- **THEN** the published event header carries the `App` `app_id` and the configured event code

#### Scenario: Idle input still observes stop

- **WHEN** no event arrives on any subscribed service for longer than the wait timeout
- **THEN** `run()` re-checks the stop request and time-base state instead of blocking forever

#### Scenario: Callback work stays on the run thread

- **WHEN** callbacks for several subscriptions run
- **THEN** every callback executes on the `run()` thread and no callback is invoked from a transport worker thread
## ADDED Requirements

### Requirement: App exposes loss and output-channel configuration

`App` SHALL provide a subscribe overload with an explicit transport-loss callback and a publish overload accepting a `com::ChannelConfig` for the output channel. The loss callback SHALL run on the `run()` thread; the explicit channel configuration SHALL be validated before service creation.

#### Scenario: Loss callback receives transport loss
- **WHEN** a subscribed consumer reports lost samples
- **THEN** the supplied loss callback runs on the `run()` thread with the loss information

#### Scenario: Output channel uses explicit limits
- **WHEN** the application publishes with a `ChannelConfig` carrying `max_publishers=2`
- **THEN** the created output service requests two publishers

#### Scenario: Invalid output configuration is diagnosed
- **WHEN** the supplied output `ChannelConfig` is invalid
- **THEN** publication returns `nullptr` after recording a diagnostic
