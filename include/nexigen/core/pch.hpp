// nexigen::core — precompiled-header prelude (change speedup-validation-loop,
// design D2). Compile-time only: every nexigen TU compiles with the same
// preprocessor state (verified: -DJSON_NOEXCEPTION=1 -D_GNU_SOURCE
// -D_POSIX_C_SOURCE=200809L on both lib and test units), so paying the
// nlohmann/json parse cost once per target instead of per TU is semantically
// invisible — same macros, same diagnostic waivers, no code emitted.
//
// The GCC-only -Wnull-dereference waiver lives here for the nlohmann include
// exactly as each TU carried it before (pragma push/pop scoped inside this
// header): the PCH snapshot restores the environment to the same state the
// following TU code compiles under, so -Werror stays strict everywhere else.
#pragma once

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#endif
#include <nlohmann/json.hpp>
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// Cold-path std headers shared by the settings loaders, sink plumbing and
// serialization pairs (common ancestors of every module TU).
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
