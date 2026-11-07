# Tasks

## 1. Baseline and guards

- [x] 1.1 Record the pre-refactor lizard CCN/location for `Infrastructure::initialize`, `FileLogger::initialize`, `TracerProvider::consume_loop`, and `safe_id`
- [x] 1.2 Confirm no unrelated working-tree changes and note the Debug, Release, ASan, and TSan test commands

## 2. app/telemetry_layout `safe_id`

- [x] 2.1 Extract `allowed_id_char` and reduce `safe_id`; verify the identifier grammar, length limit, `.`/`..` rejection, and all existing telemetry-layout tests are unchanged and `safe_id` is ≤ CCN 15

## 3. logging/FileLogger `initialize`

- [x] 3.1 Add `valid_path`, `valid_optional_text`, and `valid_file_logger_config`; verify every invalid-config case still returns `InvalidArgument` without starting the worker
- [x] 3.2 Verify the successful path keeps the same `FileSink::Settings`, producer calibration, worker tuning, and start sequence, and `initialize` is ≤ CCN 15

## 4. app/Infrastructure `initialize`

- [x] 4.1 Replace the `fail`/`emplace_checked` lambdas with private members and extract `build_time_and_logging`; verify chrono/logger/bridge start order and rollback are unchanged
- [x] 4.2 Extract `build_metrics` and `build_tracing`; verify the `Required` startup-status check, tracer initialize, `Conflict`/`NotInitialized` guards, and signal watcher start are unchanged
- [x] 4.3 Verify `Infrastructure::initialize` is ≤ CCN 15 and the app/app-suite tests pass

## 5. tracing/TracerProvider `consume_loop`

- [x] 5.1 Add `start_worker`, `deliver_batch`, `answer_flush`, and `shutdown_drain`; verify the `Required` thread-spec failure path, first-error precedence, mask snapshot, and start publication are unchanged
- [x] 5.2 Verify the 64-record batch, idle backoff/tuning, flush handshake, final drain, and sink stop order are unchanged, `consume_loop` is ≤ CCN 15, and tracing tests pass

## 6. Verification

- [x] 6.1 Run `clang-format` check on all touched files and verify it is clean
- [x] 6.2 Run lizard over the touched files and verify every touched function is ≤ CCN 15
- [x] 6.3 Compile and pass 100% of tests in Debug
- [x] 6.4 Compile and pass 100% of tests in Release
- [x] 6.5 Compile and pass 100% of tests under AddressSanitizer
- [x] 6.6 Compile and pass 100% of tests under ThreadSanitizer
- [x] 6.7 Confirm coverage module gates (`runner >= 90`, `hub >= 90`) still pass

## Workflow follow-up

- Run OpenSpec verification for this change.
- Archive the change after verification passes.
