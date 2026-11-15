# Spec Delta

## ADDED Requirements

### Requirement: RT priority capability is opt-in

The launch contract SHALL accept `allow_rt_priority` per application (default
`false`). Unless it is set, the child SHALL drop `CAP_SYS_NICE` from the
capability bounding set before `exec`, so no later `exec` can regain it. A
failed drop on a privileged child SHALL report a setup failure and start no
application; a non-root child SHALL skip the call.

#### Scenario: The default drops CAP_SYS_NICE

- **WHEN** an application is launched without `allow_rt_priority` by a privileged supervisor
- **THEN** the child's capability bounding set no longer contains `CAP_SYS_NICE`

#### Scenario: Opt-in keeps the capability

- **WHEN** an application declares `allow_rt_priority: true`
- **THEN** the bounding set is left unchanged

#### Scenario: A failed drop starts nothing

- **WHEN** the privileged child cannot drop the capability
- **THEN** the setup handshake reports a failure and the application does not start

#### Scenario: The opt-in cannot target a non-root uid

- **WHEN** an application declares both `allow_rt_priority: true` and a `uid`
- **THEN** manifest validation fails, because the credential drop would clear
  the capability and the opt-in would be a silent no-op
