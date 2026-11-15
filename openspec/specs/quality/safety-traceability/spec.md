# quality/safety-traceability Specification

## Purpose

Keeps a pinned, machine-checked mapping from every requirement of
`AUTOSAR_FO_RS_Safety` (document 986, release R24-11) to an OpenSpec capability,
its evidence, and an explicit coverage status, so safety claims stay auditable
and cannot drift as the platform evolves.

## Requirements

### Requirement: Pinned requirement catalog

The repository SHALL keep `docs/safety/rs-safety-r24-11.tsv` as the canonical
catalog of the source document: one row per requirement with its identifier,
title, and requirement level. The file header SHALL pin the document identity,
release, page count, and the SHA-256 of the PDF the rows were derived from.

#### Scenario: Catalog covers the source document

- **WHEN** the catalog is compared against `AUTOSAR_FO_RS_Safety` R24-11
- **THEN** it contains each of the 62 requirement identifiers exactly once

#### Scenario: Catalog pins its source

- **WHEN** the catalog header is read
- **THEN** it names document 986, release R24-11, 44 pages, and the source PDF SHA-256

#### Scenario: Duplicate identifier is rejected

- **WHEN** two catalog rows share one identifier
- **THEN** the traceability gate exits non-zero and names the duplicated identifier

### Requirement: Traceability matrix

`docs/safety/traceability.md` SHALL contain exactly one row per catalog
requirement, and each row SHALL state a status, at least one owning OpenSpec
capability path, and evidence. A `supported` or `partial` row SHALL name
existing capability specs and at least one existing evidence path; a `partial`
row SHALL also state its limitation.

#### Scenario: Supported row is evidence-backed

- **WHEN** a requirement is marked `supported`
- **THEN** its row names at least one existing capability spec and at least one existing evidence path

#### Scenario: Partial row states its limitation

- **WHEN** a requirement is marked `partial`
- **THEN** its row names the gap that keeps it from being `supported`

#### Scenario: Missing row fails the gate

- **WHEN** a catalog requirement has no matrix row
- **THEN** the traceability gate exits non-zero and names the missing identifier

#### Scenario: Unknown identifier fails the gate

- **WHEN** the matrix contains an identifier that is absent from the catalog
- **THEN** the traceability gate exits non-zero and names the unknown identifier

#### Scenario: Dangling paths fail the gate

- **WHEN** a row references a capability spec or evidence path that does not exist
- **THEN** the traceability gate exits non-zero and names the missing path

### Requirement: Status vocabulary

The matrix SHALL classify every requirement with exactly one of `supported`,
`partial`, `not-implemented`, or `out-of-scope`. A `not-implemented` row SHALL
name the change that will close it or an accepted-deviation reason; an
`out-of-scope` row SHALL state the product boundary that excludes it.

#### Scenario: Unknown status is rejected

- **WHEN** a row carries a status outside the four allowed values
- **THEN** the traceability gate exits non-zero and names the row and the status

#### Scenario: Unclaimed gap is rejected

- **WHEN** a row is `not-implemented` without a closing change or an accepted-deviation reason
- **THEN** the traceability gate exits non-zero

#### Scenario: Out-of-scope row carries a boundary

- **WHEN** a row is `out-of-scope` without a stated boundary
- **THEN** the traceability gate exits non-zero

### Requirement: Declared programme boundary

The matrix SHALL record the AUTOSAR Classic Platform cluster, the Update and
Configuration Management cluster (`RS_UCM_*`), and the Persistency cluster
(`RS_PER_*` and `RS_SAF_10040`) as `out-of-scope` with an explicit rationale.

#### Scenario: Classic Platform cluster is declared

- **WHEN** the matrix is inspected for the Classic Platform requirements (`RS_SAF_311xx` through `RS_SAF_313xx`)
- **THEN** every one of them is `out-of-scope` with a product-boundary rationale

#### Scenario: UCM cluster is declared

- **WHEN** the matrix is inspected for `RS_UCM_*`
- **THEN** every one of them is `out-of-scope` with a product-boundary rationale

#### Scenario: Persistency cluster is declared

- **WHEN** the matrix is inspected for `RS_PER_*` and `RS_SAF_10040`
- **THEN** every one of them is `out-of-scope` with a product-boundary rationale

### Requirement: Static traceability gate

A repository check SHALL validate the catalog and matrix without network access
or a compiler, and SHALL exit non-zero on any drift: a missing, extra, or
duplicate identifier, an invalid status, a missing required field, or a
dangling path. The check SHALL run in CI and in the `scripts/validate.sh`
static-gate tier.

#### Scenario: Drift fails CI

- **WHEN** a matrix row is added, removed, or renamed without a matching catalog change
- **THEN** the CI job that runs the traceability gate fails

#### Scenario: Clean tree passes

- **WHEN** the catalog and matrix are consistent
- **THEN** the gate exits zero and prints the number of rows checked per status

#### Scenario: The gate needs no build

- **WHEN** the gate runs in a checkout with no build directory and no network
- **THEN** it completes from the two text files alone
