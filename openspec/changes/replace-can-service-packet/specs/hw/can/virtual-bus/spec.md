# Spec Delta

## REMOVED Requirements

### Requirement: Batch is internal to virtual driver links

**Reason**: The public `rx` and `tx` services now carry the same `CanMessageBatch` contract as `vbus/*`, so batching is no longer internal to the virtual driver links.

**Migration**: `nexigen-canrx`, `nexigen-cantx`, `CanClient`, `nexigen-canmon` and `nexigen-canview` consume or publish `CanMessageBatch` while retaining their per-message C++ callbacks. Mixed-version processes are incompatible and must be upgraded together.
