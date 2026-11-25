# Proposal

## Why

The A change batches only the internal virtual driver links. The public `rx` and `tx` services still carry one `CanMessage` per sample, so `nexigen-canrx` and `nexigen-cantx` remain dominated by per-message iceoryx2 work at 10 000 frames/s. A single packet-shaped contract is needed on the public CAN services to reduce that cost for every consumer and producer.

## What Changes

- **BREAKING**: change the payload of `nexigen/hw/can/<channel>/rx` and `nexigen/hw/can/<channel>/tx` from `CanMessage` to `CanMessageBatch` (1..100 messages), while keeping `status` as `CanChannelStatus`.
- Keep `CanClient`'s C++ callbacks and `CanPublisher::send()` per-message; the client and publisher batch internally, so application source code does not need a new callback shape.
- Make `nexigen-canrx` publish batches on `rx` and `nexigen-cantx` consume batches from `tx`; `nexigen-canmon` and `nexigen-canview` continue to render individual messages by expanding batches.
- Remove the A-only requirement that batch is internal to `vbus/*`; `vbus/*` and the public `rx`/`tx` now share the same packet contract.
- Preserve per-message timestamp, hardware sequence, kind, flags, origin and payload; batching never rewrites a message.
- Keep the driver contract batch-capable (`submit_batch`) and the public service names unchanged.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `hw/can/transport`: `rx`/`tx` carry `CanMessageBatch`; one packet may contain multiple messages.
- `hw/can/virtual-bus`: remove the internal-only batch requirement and state that `vbus/*` and public `rx`/`tx` use the same packet contract.

## Impact

- **Code**: `include/nexigen/hw/can/com/` (`can_client`, `can_publisher`, `can_receiver`), `tools/nexigen-canrx`, `tools/nexigen-cantx`, `tools/nexigen-canmon`, `tools/nexigen-canview/backend`, `tools/detail/can_tool_common.hpp`, `tests/hw/can/`, and `tools/nexigen-canvbus`/`VirtualCanDriver` where service config changes.
- **Public contract**: service names stay the same; payload type and size change. Old and new processes cannot share `rx`/`tx`; mixed-version deployments fail with an incompatible type layout. In-tree tools and examples must ship together.
- **Performance**: removes the remaining per-message public `rx`/`tx` publication; expect the largest CPU reduction at `canrx` and `cantx` at 10 000 frames/s. Batch overhead is a maximum 10 ms flush and 11 216 B per packet.
- **Memory/allocation**: packet queues use bounded 32-sample settings; batches are trivially copyable and preallocated. No per-message heap allocation.
- **Serialization**: JSONL/session formats remain per-message and unchanged.
