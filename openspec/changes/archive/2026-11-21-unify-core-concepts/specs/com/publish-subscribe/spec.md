# Spec Delta

## MODIFIED Requirements

### Requirement: Trivially-copyable message contract

Every `com` port template SHALL require `com::concepts::Message<T>`, an alias of `core::concepts::Message<T>`, which is satisfied exactly when `std::is_trivially_copyable_v<T>` is true, so payloads are plain bytes with no pointers, no virtual tables, and no owning members; a violating type SHALL be rejected at compile time.

#### Scenario: POD payload is accepted

- **WHEN** a plain struct of arithmetic members is used as a publisher payload type
- **THEN** the `Message<T>` constraint holds and the publisher compiles

#### Scenario: Non-POD payload is a compile error

- **WHEN** a negative-compile probe instantiates a publisher over a type containing `std::string`, `std::vector`, a smart pointer, or a virtual function
- **THEN** compilation fails at the constraint, not at runtime

#### Scenario: Alias resolves to the core concept

- **WHEN** `com::concepts::Message<T>` and `core::concepts::Message<T>` are compared for any `T`
- **THEN** both concepts evaluate identically
