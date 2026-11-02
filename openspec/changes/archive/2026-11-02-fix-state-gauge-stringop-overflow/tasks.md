# Tasks

## 1. Fix the copy helper signatures

- [x] 1.1 Change `StateStorage`'s copy helpers in `include/nexigen/metrics/state_gauge.hpp` (`copy`/`take`/`observe` single-threaded and `copy_under_seqlock`/`take`/`observe` multi-threaded) to take `char (&dst)[max_state_length + 1]`; verify `nexigen_metrics_tests` compiles in Debug

## 2. Verify the optimizing builds

- [x] 2.1 Build `nexigen_metrics_tests` in Release and run it; verify 100% pass with no `-Wstringop-overflow` error
- [x] 2.2 Build `nexigen_metrics_tests` in ThreadSanitizer and run it; verify 100% pass with no sanitizer report
- [x] 2.3 Build and run the full metrics and hub test suites in AddressSanitizer; verify no sanitizer report

## Workflow follow-up

- Add dimension/units to metrics (a separate library change).
- Explore a full operator dashboard for the hub.
