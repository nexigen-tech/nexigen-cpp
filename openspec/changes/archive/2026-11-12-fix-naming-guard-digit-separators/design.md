# Design

## Context

See `proposal.md` — Why. The stripper is duplicated verbatim in both guards: it walks the text and blanks comments, raw strings and ordinary literals so the regexes only see code. The apostrophe branch assumes char literals and skips a backslash-escaped successor.

C++14 digit separators are valid inside integer literals and hex literals, so an apostrophe can legitimately appear between digits in code that the guard must still inspect.

## Goals / Non-Goals

**Goals:**

- A digit separator never hides following code from either guard.
- Character literals, prefixed literals and escaped quotes keep working.
- A regression test that fails if the rule is removed.

**Non-Goals:**

- Deduplicating the stripper into a shared module. The two guards are standalone scripts invoked directly by CI; a shared Python package would add an import-path dependency for no functional gain. Both copies get the same fix and the same self-test.
- Changing what the guards flag beyond the fix (no new rules).

## Decisions

### D1 — Separator rule: apostrophe between two hex digits

`text[i-1] in HEX_DIGITS and text[i+1] in HEX_DIGITS` is the narrowest rule that covers `1'000`, `250'000`, `0xA'B` and `0b1010'1010`. Character literals cannot match it (`'0'` is preceded by code punctuation or space; `L'x'` is preceded by `L`, which is not a hex digit), so the existing literal branch stays untouched for them.

Alternatives rejected: a full C++ lexer (far too much machinery for a lint guard); treating every apostrophe as a separator (breaks literal blanking and would let string contents leak into the regexes); requiring the previous character to be `[0-9]` only (misses `0xA'B`).

### D2 — Self-test inside each script, one CTest entry

Each guard gains `--self-test`, which runs its own scanner over embedded fixtures and asserts the verdict:

- a violation placed after `250'000` must still be reported;
- a declaration inside a genuine `std::string` / `L'x'` literal must not be reported;
- a plain violation without separators must still be reported.

This keeps the test next to the rule it protects and needs no extra fixture files. A single CTest entry runs both scripts so a future regression fails the suite instead of silently weakening CI.

### D3 — Rename the two revealed declarations in this change

Leaving them would keep both gates red. Both are file-local (an example's sink class and a test-local alias), so `Capture_sink` → `CaptureSink` and `Stress_provider` → `StressProvider` are pure renames with no API impact.

## Risks / Trade-offs

- **Another false negative remains elsewhere in the stripper** (for example around raw-string delimiters) → out of scope here; the self-test pins the separator rule, and any further hole should be fixed the same way with its own fixture.
- **The stricter guard reveals more violations later** → that is the intended behavior; the two current ones are fixed now.
- **Duplicated fix drifts between the two scripts** → the same `--self-test` block exists in both, and the CTest entry runs both, so drift is caught.
