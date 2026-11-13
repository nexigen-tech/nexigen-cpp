// nexigen::integration suite_2_apps — shared payload crossing the com transport
// (change add-suite-2-apps; spec examples/suite-2-apps "One-hertz fixed-shape
// data generation").
//
// DataSample is the fixed, trivially-copyable value the generator publishes and
// the receiver consumes: a monotonic sequence number plus a fixed 64-byte string
// buffer. No pointers, virtual tables, or owning members cross shared memory, so
// the zero-copy transport treats the value as plain bytes.
#pragma once

#include <cstdint>
#include <type_traits>

namespace suite2_apps {

/// @brief Fixed payload of one one-second data sample.
struct DataSample {
    std::uint64_t sequence{0}; ///< Monotonic sequence number stamped by the generator.
    char message[64]{};        ///< Fixed 64-byte string payload (NUL-terminated).
};

static_assert(std::is_trivially_copyable_v<DataSample>,
              "DataSample must be trivially copyable to cross the com transport");

} // namespace suite2_apps
