// nexigen::chrono — cold-path JSON for ConsoleSink.
//
// Bodies compiled once per module (spec quality/code-structure,
// "Cold-path serialization of compiled modules is built once").
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>
#pragma GCC diagnostic pop

#include <cstdint>

#include <nexigen/chrono/console_sink.hpp>

namespace nexigen::chrono {

void to_json(nlohmann::json& j, const ConsoleSink& sink) {
    j = nlohmann::json{{"type", "ConsoleSink"}, {"fd", sink.descriptor()}};
}

void from_json(const nlohmann::json& j, ConsoleSink& sink) noexcept {
    const auto it = j.find("fd");
    if (it != j.end() && it->is_number_integer()) {
        const auto value = it->get<std::int64_t>();
        sink = ConsoleSink{static_cast<int>(value)};
    }
}

} // namespace nexigen::chrono
