# Spec Delta

## ADDED Requirements

### Requirement: View tabs have editable titles

The viewer SHALL let the operator rename any open view. Renaming SHALL change the tab label, SHALL NOT change the view identity, filters, selection or dock position, and SHALL persist for LIVE views. The clone dialog SHALL accept a title for the new view, default it from the source view's title, and reject an empty title.

#### Scenario: Rename keeps view state and docking

- **WHEN** the operator renames an open view
- **THEN** its tab label changes while its filters, selection and dock position stay unchanged

#### Scenario: Clone with a custom title

- **WHEN** the operator clones a view and enters `Powertrain`
- **THEN** the new tab is named `Powertrain`

#### Scenario: Empty title is rejected

- **WHEN** the operator submits a whitespace-only title
- **THEN** the rename or clone is rejected and the previous title remains

#### Scenario: Same-named clones stay distinct

- **WHEN** two views have the same visible title
- **THEN** both windows remain independently addressable and keep their own state
