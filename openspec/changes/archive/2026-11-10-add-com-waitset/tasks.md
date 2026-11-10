# Tasks

## 1. Vocabulary

- [x] 1.1 Create `include/nexigen/com/callback_progression.hpp`: `CallbackProgression{Continue,Stop}` scoped enum with `EnumTraits` table, constexpr `to_string`/`from_string`, ADL `to_json`/`from_json`, and `std::formatter`. Verify: unit cases round-trip both names, garbage-cast renders `Unknown`-style fallback, hostile JSON leaves the target unchanged, formatter equals `to_string`.

## 2. Core WaitSet entities

- [x] 2.1 Create `include/nexigen/com/waitset.hpp`: non-template `WaitSet` over `iox2::WaitSet<Ipc>` with `create()`, `attach_notification(EventListener)`, `attach_deadline(EventListener, deadline)`, `attach_interval(duration)`, `wait_and_process(cb)`, `wait_and_process_once(cb)`, `wait_and_process_once_with_timeout(cb, timeout)`, `capacity()`/`len()`/`is_empty()`, and cold-path `to_string` + formatter; the public callback signature uses only `nexigen::com` types (design D2). Verify: gate-ON Debug build compiles `nexigen_com` and a smoke test creates a WaitSet and reports `len() == 0`.

- [x] 2.2 Create `include/nexigen/com/wait_set_guard.hpp`: `WaitSetGuard` (move-only RAII over `iox2::WaitSetGuard<Ipc>`, detach on destruction) with `to_string` + formatter. Verify: guard drop decrements the WaitSet attachment count.

- [x] 2.3 Create `include/nexigen/com/wait_set_attachment_id.hpp`: `WaitSetAttachmentId` (move-only) with `from_guard`, `has_event_from(guard)`, `has_missed_deadline(guard)`, `hash()`, and `to_string` + formatter. Verify: an id produced from a guard reports `has_event_from` true when that source fires.

## 3. Error mapping

- [x] 3.1 Extend `src/com/error_mapping.cpp` + `detail/error_mapping.hpp` with `map_error` for `WaitSetCreateError`, `WaitSetAttachmentError`, and `WaitSetRunError` per design D3. Verify: mapping unit cases for `AlreadyAttached -> InvalidArgument`, `InsufficientCapacity -> OutOfSamples`, and `TerminationRequest/Interrupt -> Ok`.

## 4. WaitSet behavior tests

- [x] 4.1 Multiplexing: attach two event listeners, fire one notifier, and verify the callback observes exactly that source (spec "Multiplexed blocking wait"). Verify: `test_waitset` in `tests/com/com_test.cpp` passes.

- [x] 4.2 Deadline: attach a listener with a short deadline, emit no event, and verify the wait wakes with `has_missed_deadline` true (spec "Deadline attachment"). Verify: deadline case passes.

- [x] 4.3 Interval: attach a short interval and verify `wait_and_process_once` returns without any notifier event (spec "Interval attachment"). Verify: interval case passes.

- [x] 4.4 Callback progression: a callback returning `Stop` ends `wait_and_process` after the first source (spec "Callback dispatch"). Verify: progression case passes.

- [x] 4.5 Empty/teardown: `wait_and_process_once_with_timeout` on an empty WaitSet returns promptly with `InvalidArgument` (the transport's `NoAttachments`) without spinning, and a dropped guard detaches (specs "Empty wait set does not spin", "Attachment lifecycle"). Verify: cases pass.

- [x] 4.6 Diagnostics: `to_string`/formatter for `WaitSet` report the attachment count; `CallbackProgression` round-trips names (spec "WaitSet diagnostics"). Verify: cases pass.

## 5. Gates

- [x] 5.1 Debug build with the gate OFF builds byte-identical to before; gate ON builds `nexigen_com` + `nexigen_com_tests` and all com tests pass under `ctest`.

- [x] 5.2 Release (`-O3 -march=native -flto`), TSAN, and ASAN gate-ON builds pass all tests.

- [x] 5.3 `openspec validate "add-com-waitset" --strict` passes.
