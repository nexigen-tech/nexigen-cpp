# Firmware time-base register map (D1–D3)

Contract between the FPGA firmware and the host `nexigen-chronod` device
backend (`nexigen::chrono::DeviceRegisterMap`, `DeviceAccessMode::RegisterPair`).
The same block is exposed through UIO, VFIO, or the vendor character device;
the host never applies a frequency or offset to the raw counter.

All fields are little-endian. Reset values are shown; a firmware implementation
that cannot provide an optional capability clears the corresponding `CAPS` bit
instead of writing a fake value. `SimulatedTimeBaseDevice` fills every field in
this table, and the `device_backend` chrono suite asserts each one.

## Access modes

A declared hardware domain uses exactly one mode
(`nexigen::chrono::DeviceAccessMode`):

| Mode | Pair source | Row flags |
|---|---|---|
| `phc_cross_timestamp` | `PTP_SYS_OFFSET_PRECISE` on `/dev/ptpN`; the driver captures both sides atomically. | `latched`, no `manual_pair`. |
| `register_pair` | The D1–D3 latch below, read under the sequence/CRC protocol. | `latched`, `ptm_capable` when `CAPS` bit 0 is set. |
| `manual_pair` | The device counter and `CLOCK_MONOTONIC_RAW` read in software around the counter read. | `manual_pair`, no `ptm_capable`; the read window becomes `unc_ns`. |

The host never converts ticks in the driver or the backend: it publishes the
raw pair and the estimator owns the map. A module without PCIe PTM or a latch
must use `manual_pair`; its larger, one-sided latency is visible in the row's
uncertainty.

`nexigen::chrono::RegisterPairDevice` implements the host side of this contract:
it snapshots the D1/D2 block twice, accepts the pair only when `pair_sequence`
did not move, validates `pair_crc` over the bytes before the CRC field, and
rejects a sequence already accepted (stale) along with an invalid identity.
`SimulatedTimeBaseDevice::advance_pair()` mirrors the firmware latch edge and
recomputes the CRC; a declared domain with `simulate: true` binds this simulator
in `nexigen-chronod`.

## D1 — identity and capabilities

| Offset | Size | Field     | Notes |
|---:|---:|---|---|
| 0x00 | 4 | `magic`       | `0x4E585442` (`"NXTB"`). |
| 0x04 | 2 | `version`     | Register-map version; `1` for this contract. |
| 0x06 | 2 | `caps`        | Bit 0 PTM supported, bit 1 hardware latch, bit 2 PHC registered. |
| 0x08 | 16 | `uuid`       | Device UUID; stable for the life of the module. |

A host that reads an unknown `version` or a wrong `magic` reports the device as
absent; the base stays `Degraded` and other domains keep working.

## D2 — latched correlation pair

| Offset | Size | Field          | Notes |
|---:|---:|---|---|
| 0x18 | 8 | `dev_ticks`      | Free-running device counter at the latch edge. |
| 0x20 | 8 | `ptm_ticks`      | PTM/root-complex ticks at the *same* edge. |
| 0x28 | 4 | `pair_sequence`  | Increments once per new pair; wraps are allowed. |
| 0x2C | 4 | `pair_crc`       | CRC-32C over D1+D2 bytes before this field. |

Latch semantics: firmware latches `dev_ticks` and `ptm_ticks` together on one
hardware edge (read-latch or trigger), then increments `pair_sequence` and
updates `pair_crc` with release ordering. The host reads `pair_sequence`, copies
D1/D2, reads `pair_sequence` again, and accepts the pair only when both reads
match. A torn or stale read is discarded, never averaged.

## D3 — status

| Offset | Size | Field                     | Notes |
|---:|---:|---|---|
| 0x30 | 1 | `lock_state`               | `0` free-run, `1` locked, `2` holdover. |
| 0x31 | 1 | `lock_lost_sticky`         | Set on a lock loss; cleared only by a host ack or reset. |
| 0x32 | 2 | `sync_epoch`               | Device lifecycle epoch; changes on firmware restart. |
| 0x34 | 8 | `last_exchange_dev_ticks`  | Counter value of the last completed exchange. |
| 0x3C | 4 | `prop_delay_ns`            | Measured one-way propagation delay. |
| 0x40 | 4 | `residual_ns`              | Residual of the last exchange. |

`lock_state` is the fast health signal; `lock_lost_sticky` records that a loss
happened even when the device has re-locked. The daemon maps `lock_state` to
`chrono::LockState` and uses `sync_epoch` as the domain epoch, so a firmware
restart fails fast for consumers that require the domain.

## Transport boundary

The in-repo host side is `nexigen::chrono::RegisterPairDevice`: it owns the
sequence/CRC acceptance protocol and asks a caller-supplied snapshot callback
for the current D1-D3 block. `SimulatedTimeBaseDevice` supplies that callback
in memory and `nexigen-chronod` binds it for a declared domain with
`simulate: true`.

A real deployment supplies the same callback from its UIO, VFIO, or vendor
character-device module: map the register window, copy D1-D3 into
`DeviceRegisterMap`, and return `true`. The module must not convert ticks or
apply a frequency; the daemon's estimator owns the map. Without such a module a
declared `register_pair` domain stays unbound, is reported as a finding, and
degrades the base — the daemon never guesses at hardware that is not present.

## Versioning

- Additive fields consume reserved space and bump `version`.
- Semantic changes to `dev_ticks` or `ptm_ticks` (unit, edge, width) require a
  version bump and a daemon change; the daemon rejects unknown versions.
- `pair_sequence` is the only ordering authority. `pair_crc` guards against
  torn reads, not against a malicious device.
