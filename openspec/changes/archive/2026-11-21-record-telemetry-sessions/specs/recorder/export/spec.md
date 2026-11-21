# Spec Delta

## ADDED Requirements

### Requirement: Perfetto gap objects

Perfetto output SHALL emit one explicit gap object for every gap marker on a selected trace
dataset, carrying `first_lost_sequence`, `lost_count`, and `reason`, positioned at the gap record's
event time on that dataset's track. A gap SHALL NOT be silently dropped.

#### Scenario: Gap appears in Perfetto output

- **WHEN** a selected trace dataset contains a gap marker with `lost_count = 3`
- **THEN** the Perfetto output contains one gap object with `first_lost_sequence`, `lost_count = 3`,
  and `reason`

#### Scenario: Gap keeps its position

- **WHEN** the gap record's event time falls between two spans
- **THEN** its object is ordered at that event time on the same track as the dataset's spans

#### Scenario: Clean dataset has no gap objects

- **WHEN** a selected trace dataset has no gap markers
- **THEN** the Perfetto output contains no gap objects

#### Scenario: Every gap is emitted

- **WHEN** a dataset contains three gap markers
- **THEN** the output contains exactly three gap objects
