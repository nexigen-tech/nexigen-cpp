// Shared payload for the quickstart example and its test sender.
#pragma once

#include <cstdint>

namespace nexigen::examples::quickstart {

/// @brief One input/output scalar carried by the quickstart event channels.
struct Signal {
    std::uint64_t sequence{};
    double value{};
};

} // namespace nexigen::examples::quickstart
