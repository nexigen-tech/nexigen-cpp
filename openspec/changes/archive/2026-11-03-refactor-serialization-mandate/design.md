# Design

## Context

The serialization mandate pattern repeats on 103 `std::formatter`
specializations; 55 of them are the two-line "delegate to to_string"
pattern (grep `formatter<std::string_view>`). Enums additionally carry a
`to_json`/`from_json` pair that routes through `nexigen::detail::
enum_to_json` / `enum_from_json` (the sys_helpers one-off template), and a
couple of CRTP-free free functions exist beside specializations that
require formatting only via member `to_string()`. All per-command
standardizations already delegate to to_string, so in areas where the
Mandate body is exactly "format via to_string" a generic machinery can
implement the whole surface once.

Constraints: C++23, `-fno-exceptions -fno-rtti`, `-Werror` GCC/Clang,
project-wide cold-path only (formatting is mandate-9 diagnostic — never
hot path). Higher-order decision recorded in the scope: skipping the
per-type conflict between CONSTRAINT-qualified partial `std::formatter`
and std types that could accidentally collide (`std::filesystem::path`
нет member to_string; the concept requires one, so the danger set is
narrow).

## Goals / Non-Goals

**Goals:**
- New types provide their serialization mandate by writing a member
  `to_string()` (and for enums: `to_string`/`from_string`) — NOTHING else,
  with `std::format`/`to_json/from_json` living on the same generic
  machinery.
- Existing types lose boilerplate that was verbatim duplication; types
  with distinct bodies keep their explicit definitions (documented as
  exceptions in the header).

**Non-Goals:**
- Changing any public formatting CONTRACT text (spec scenarios stay
  outcome-focused).
- Introducing formatting for types that currently lack the mandate.

## Decisions

### D1 — `.MandateToString<T>` concept + ONE generic formatter

`nexigen::core::detail::MandateToString<T>`:
`requires(const T& v) { { to_string(v) } -> std::convertible_to<std::string>; }`
— unqualified lookup at definition + ADL at instantiation resolves BOTH
member functions (member accessible? no — the CONCEPT's requires-expression
uses FUNCTION CALL SYNTAX; member to_string is reachable through
`v.to_string()` too: add both arms). One partial specialization
`std::formatter<T> : std::formatter<std::string_view>` formats via
`to_string(v)` for every matching type. Specializations deleted when their
body was exactly the delegate; their outcome identical.

### D2 — Enum JSON pair from the mandate header

The generic `nexigen::to_json(json&, const E&)` / `from_json(const json&, E&)`
constrained by the same mandate concept plus an ADL-reachable
`from_string(string_view, E&) -> bool`. Behavior equal to the per-enum
one-liners (which call the shared `enum_to_json/enum_from_json` helpers).
Non-template per-enum overloads keep resolution priority (non-template
beats template), so a module that wants custom JSON keeps writing it.

### D3 — CRTP `MandateString<T>` base

`struct MandateString { friend std::string to_string(const Self& v)
{ return v.to_string(); } };` — a CRTP base that types inherit to get the
free function for free. Adopted ONLY at types where the free function was
verbatim duplication; types with comments/extra behavior in their free
functions stay untouched.

### D4 — Deliberate overload ambiguity and std-type risk

The concept requires `to_string` to be callable by ADL/unqualified lookup
with a `std::string` return. Candidates that could accidentally match:
`std::to_string(int)` at GLOBAL scope — but our ADL phases only consider
nexigen namespaces and the GLOBAL namespace for unqualified lookup: for a
type owned by our namespaces, `std::to_string(int)` never matches our
enum (no conversion path). std-namespace types are not covered by the
concept since their ADL namespaces are `std` (where no member to_string is
declared for them — std::filesystem::path etc. — plus no free
nexigen::to_string). Any accidental member-based name may collide only on
types WE own.

## Risks / Trade-offs

- [Generic formatter may match a future type with a to_string that
  formats DIFFERENTLY from the intended formatter output] → the
  documentation in the header makes it explicit: if `to_string` is
  implemented, it BECOMES the formatter output. Author the member with
  that in mind. Reviewed at P3-28 sweep time for every existing type.
- [Deletion risk: a type with a delegate formatter but ADL-UNREACHABLE
  to_string] → verification task compiles a formatter instantiation for
  each type via the existing static_asserts of the serialization tests
  (which cover formatter for every public type).

## Migration Plan

No public API change — deletions are invisible to consumers because the
generic machinery provides the same symbols. Rollback = revert.

## Design notes (refactor-serialization-mandate)

### D2 — ADL-reached JSON pairs: the empirical boundary

The generic `nexigen::to_json/from_json` pair was REMOVED after a
minimal-replica experiment ([basic.lookup.argdep]):
for ENUM types the associated-namespace set is ONLY the enum's immediate
namespace (C++23 does not chain to enclosing namespaces for enums), so a
pair hosted at nexigen:: would never be found through ADL for module
enums — task 1.1 records the exact probes (`/tmp/opencode/
mandate_probe5..10.cpp` reasoning is captured in the tasks notes). STRUCT
types take the full chain, but struct JSON is per-field by design, not a
to_string delegate — so the JSON-pair cost stays with the per-enum
one-liner and its per-struct custom body (unaffected boilerplate).

### D4 — Concept correction discovered during the sweep

`MandateToString` accepts BOTH `std::string` and `std::string_view`
returns: per-enum mandate helpers are `constexpr string_view` and
struct members return `std::string` — the original `convertible_to<
std::string>` wording rejected the enum half of the mandate (the sweep's
own demonstration types failed to instantiate formatters until the view
arm was added).

### D3 — CRTP base removed in favor of a constrained generic free

`MandateString<T>` (CRTP friend) is invisible to ADL for deriving types:
the friend lands in the base's namespace, and a base at nexigen:: is NOT
in the associated-namespace set of a type declared in nexigen::<module>
(nor vice versa). The generic free `nexigen::to_string` template
(constrained by the core mandate concept) achieves the documented goal:
module scopes reach it through ordinary enclosing-namespace lookup;
existing per-type free functions outrank it (non-template wins); NEW
types declare only their member to reach everything.
