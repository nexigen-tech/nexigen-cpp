# Tasks

## 1. Manifest and error vocabulary

- [x] 1.1 Add `ResourceGroupConfig`, `TrustedPlatformConfig`, and `FunctionGroupPortMapping` types with conversion sets.
- [x] 1.2 Extend `SuiteManifest`/`AppSpec` with `resource_groups`, `trusted_platform`, `function_group_port_mapping`, `resource_group`, and `executable_digest`; serialize both ways.
- [x] 1.3 Validate resource groups, trusted policy, and port mappings.
- [x] 1.4 Append `core::ErrorCode::SecurityError` and `exec::StateResult::AccessDenied`; update every conversion table and test.
- [x] 1.5 Add `requester_id` to `StateCommand` and update its conversion/diagnostics.

## 2. Resource groups and trusted startup

- [x] 2.1 Add `StartOptions::memory_limit_bytes`/`working_dir`; resolve them in `SuiteLaunchPlan`.
- [x] 2.2 Apply `chdir` and `RLIMIT_AS` in `launch_application_child`.
- [x] 2.3 Add the SHA-256 file helper and known-vector tests.
- [x] 2.4 Verify the executable digest in `ExecutionManager::start` before layout/fork; return `SecurityError` on failure.
- [x] 2.5 Add tests for resource overrides, limit failures, digest match/mismatch, and no-layout-on-failure.

## 3. Access control

- [x] 3.1 Store the port mapping in `StateClient` and reject unmatched requesters with `AccessDenied`.
- [x] 3.2 Propagate `TriggerRequest::requester_id` through the SM pending transition into `StateCommand`.
- [x] 3.3 Give the SM MachineFG lifecycle client a requester identity.
- [x] 3.4 Add tests for authorized, unauthorized, empty-mapping, and SM propagation paths.

## 4. Deterministic execution

- [x] 4.1 Add `DeterministicConfig` and `DeterministicClient` with cycle clock, splitmix64, and conversion sets.
- [x] 4.2 Add tests for cycle advance, late wake-up, seeded sequences, and diagnostics.

## 5. Verification and docs

- [x] 5.1 Update `docs/architecture.md` and the example manifests where useful.
- [x] 5.2 Run Debug, Release, TSAN, ASAN and gate-OFF; run complexity/naming/result gates and `graphify update .`.
