# Spec Delta

## MODIFIED Requirements

### Requirement: Complexity of decomposed functions stays within the project target

Every function introduced or modified by this refactor SHALL have cyclomatic complexity ≤ 15 as measured by lizard. The overall average complexity for the suite implementation translation units (`suite_manifest.cpp`, `suite_launch_plan.cpp`, `suite_supervisor.cpp`, `suite_control.cpp`, `suite_reporting.cpp`) SHALL not increase relative to the pre-refactor `app_suite.cpp` baseline (average CCN 3.6, 1 file, 99 functions).

#### Scenario: Post-refactor lizard report

- **WHEN** lizard is run over `src/runner/suite_manifest.cpp`, `src/runner/suite_launch_plan.cpp`, `src/runner/suite_supervisor.cpp`, `src/runner/suite_control.cpp`, and `src/runner/suite_reporting.cpp`
- **THEN** no function in any of those files exceeds complexity 15
