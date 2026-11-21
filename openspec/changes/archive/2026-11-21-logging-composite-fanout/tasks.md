# Tasks

## 1. Residual deduplication

- [x] 1.1 `include/nexigen/logging/composite_logger.hpp`: confirm/keep public inheritance
      from `core::sink_pack::Fanout<Loggers...>`; delete the shadowing `size`, the `Base`
      alias and `using Base::get`, and the now-unused `<tuple>` include.
- [x] 1.2 Re-express `flush()` through `core::sink_pack::combine` (first failure wins,
      every member still visited).
- [x] 1.3 Update the file/class Doxygen comments to name the shared pack; keep
      `set_level`/`level`, `get<...>()` initialization orchestration, `to_string`, the
      concept gate and deleted copy/move untouched.
- [x] 1.4 Compile `nexigen_logging_tests` and `examples/logging/ex_composite_logger`.

## 2. Specification

- [x] 2.1 Modify `logging/composite-logger` requirement "Virtual-free fan-out composition":
      name `core::sink_pack::Fanout` as the non-polymorphic member pack, state that
      index/type access, ordered iteration and `size` come from it, and add the
      first-failure `flush` scenario.
- [x] 2.2 `openspec validate --changes logging-composite-fanout --strict` passes.

## 3. Verification matrix

- [x] 3.1 Debug: build `build-unify`/`build-com` with `-j 64` and run the full `ctest`
      suites.
- [x] 3.2 Release: build `build-release-com` and run `nexigen_logging_tests`.
- [x] 3.3 ThreadSanitizer: build `build-tsan-com` and run `nexigen_logging_tests`.
- [x] 3.4 AddressSanitizer: build `build-asan-com` and run `nexigen_logging_tests`.
- [x] 3.5 Complexity gate:
      `python3 cmake/check_complexity.py --threshold 15 include src tests tools`.
- [x] 3.6 Format gate:
      `cmake -DCLANG_FORMAT=/usr/bin/clang-format-22 -DSOURCE_DIR=$PWD -DFORMAT_MODE=check -P cmake/RunClangFormat.cmake`.

## 4. Wrap-up

- [x] 4.1 Mark TODO P3-29 `[x]` with a note naming this change.
- [x] 4.2 Run `graphify update .`.
- [x] 4.3 Archive this change with `openspec archive logging-composite-fanout -y` and
      commit the result.
