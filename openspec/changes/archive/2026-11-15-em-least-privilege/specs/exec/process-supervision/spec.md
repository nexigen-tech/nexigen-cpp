# Spec Delta

## ADDED Requirements

### Requirement: Child credentials are dropped before exec

The launch contract SHALL accept an optional `uid` and `gid` per application
entry (`uid` alone implies `gid = uid`). When configured, the child SHALL clear
supplementary groups if it is privileged, call `setgid`, then `setuid`, verify
the drop with `getuid`/`geteuid`/`getgid`, and only then `exec`. Any failed step
SHALL report a setup failure and start no application. An entry declaring `uid`
or `gid` equal to `0` SHALL be rejected during manifest validation.

#### Scenario: The child runs as the configured user

- **WHEN** an application declares `uid`/`gid` equal to the supervisor's current user
- **THEN** the launched process reports that user id

#### Scenario: UID zero is rejected

- **WHEN** a manifest entry declares `uid` or `gid` equal to `0`
- **THEN** manifest validation fails with `InvalidArgument`

#### Scenario: A failed drop starts nothing

- **WHEN** `setgid` or `setuid` fails in the child
- **THEN** the setup handshake reports a failure and the application does not start

#### Scenario: GID without UID is rejected

- **WHEN** an entry declares `gid` but no `uid`
- **THEN** manifest validation fails with `InvalidArgument`

### Requirement: Non-root launch policy

When `trusted_platform.require_non_root` is set, the execution manager SHALL
refuse to launch an application with effective UID `0` unless the entry declares
an explicit `uid`; the refusal SHALL be `SecurityError`.

#### Scenario: Root is refused under the policy

- **WHEN** the supervisor has effective UID `0`, the policy is set, and the entry has no `uid`
- **THEN** the launch fails with `SecurityError`

#### Scenario: An explicit uid satisfies the policy

- **WHEN** the supervisor has effective UID `0` and the entry declares a non-zero `uid`
- **THEN** the launch proceeds and the child drops to that user

#### Scenario: The policy is off by default

- **WHEN** a manifest omits `require_non_root`
- **THEN** a root supervisor launches applications exactly as before
