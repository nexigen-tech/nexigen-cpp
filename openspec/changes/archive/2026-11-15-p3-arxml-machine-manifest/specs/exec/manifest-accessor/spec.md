# Spec Delta

## ADDED Requirements

### Requirement: Machine Manifest loading

`ManifestAccessor` SHALL load a Machine Manifest from JSON or from the ARXML
`<MACHINE-MANIFEST>` profile, resolve its references, validate it, and return
the normalized model. Malformed content SHALL fail with `InvalidArgument`;
unreadable files SHALL fail with `SystemError`.

#### Scenario: ARXML machine loads

- **WHEN** a valid `<MACHINE-MANIFEST>` is loaded
- **THEN** the model contains the machine, groups, processes, and resolved Function Groups

#### Scenario: Malformed machine is rejected

- **WHEN** the XML nesting is invalid or a reference cannot be resolved
- **THEN** loading fails with `InvalidArgument`
