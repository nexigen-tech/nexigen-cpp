# Spec Delta

## REMOVED Requirements

### Requirement: Provider construction and sink binding
**Reason**: The capability retires: the provider splits into the threadless `chrono/provider` authority and the periodic `chrono/worker` daemon wrapper. Construction, two-phase lifecycle, and sink binding contracts move to the provider capability.
**Migration**: Construct a `chrono::Provider` with a `ProviderConfig` and a `concepts::Sink` sink; `snapshot_now()` and the UTC authority are available immediately, `prepare()` gates stamper issuance.

### Requirement: Background snapshot delivery and collection period
**Reason**: Periodic background delivery is now a `ChronoWorker` concern, not a provider concern; the provider is threadless in both daemon and manual modes.
**Migration**: Wrap the provider in a `chrono::ChronoWorker` and start the worker; zero-interval behavior is preserved by both capabilities.

### Requirement: CPU core affinity
**Reason**: Worker-thread attribute application belongs to `chrono/worker`; the threadless provider holds no thread and applies no attributes.
**Migration**: Configure the same `core::ThreadSpec` semantics on the `ChronoWorker`; `Required`-failure, best-effort visibility, CPU 64+, and empty-set behavior are carried over unchanged.

### Requirement: Snapshot content
**Reason**: The snapshot value contract moves to the provider capability, which now owns all sampling.
**Migration**: No consumer change: monotonic-raw stamp plus signed UTC offset stays the snapshot content; `boottime` remains reserved and unsampled.

### Requirement: Provider control plane
**Reason**: The control plane reshapes: the provider keeps `snapshot_now()`/`flush()` for manual triggering; the wrapper owns `start()`/`startup_status()` and the worker report.
**Migration**: Callers that polled the old daemon controls read `startup_status()` and the thread report from the `ChronoWorker` instead of the provider.

### Requirement: Provider shutdown
**Reason**: Shutdown reshapes compositionally: provider destruction flushes its sink without a hidden final snapshot; the final snapshot becomes a `chrono/worker` shutdown duty.
**Migration**: Users of the old daemon destructor behavior destroy the worker (final snapshot + flush) and then the provider (flush) in that order.

### Requirement: Deferred worker start
**Reason**: Deferred start lives on the `chrono/worker` wrapper; the provider has no start state at all.
**Migration**: Call `start()` on the `ChronoWorker` after construction; idempotence and zero-interval no-op semantics are carried over unchanged.

### Requirement: Optional worker logger
**Reason**: The worker logger moves to `chrono/worker` with the shared `core::LogLevel` vocabulary; the separate `ClockLogLevel` mini-level is deleted (its canon relocates to `core/log-level` by the companion mechanics change).
**Migration**: Bind the logger via the worker's `start(logger)`; double-start rejection (`Conflict`) and run-without-logger behavior are carried over unchanged.

### Requirement: Provider configuration serialization
**Reason**: The config type is renamed and re-scoped as `chrono::ProviderConfig` with the `kind` field, owned by the `chrono/provider` capability.
**Migration**: Use `chrono::ProviderConfig` (JSON round-trip, `to_string`, and `std::formatter` contracts carried over; kind joins the payload).

### Requirement: Provider diagnostics
**Reason**: Diagnostics split by owner: provider state (prepared/issued/capacity) on the provider, daemon state (interval, thread spec, startup) on the worker.
**Migration**: Use `chrono::Provider` `to_string()` for the authority state and `chrono::ChronoWorker` `to_string()` for the daemon state.
