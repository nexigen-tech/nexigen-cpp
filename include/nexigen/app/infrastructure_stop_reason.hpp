// nexigen::app — why the infrastructure stopped (change
// rework-app-as-infrastructure; spec app/application).
//
// Full enum contract: constexpr to_string/from_string, non-throwing ADL
// to_json/from_json under JSON_NOEXCEPTION=1, and std::formatter. The values
// are stable and append-only.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::app {

/// Why the infrastructure run loop stopped.
enum class Infrastructure_stop_reason : std::uint8_t {
    None,      ///< No stop requested yet.
    Requested, ///< Host code requested a stop (explicit call or false tick).
    Sigint,    ///< SIGINT received.
    Sigterm,   ///< SIGTERM received.
};

[[nodiscard]] constexpr std::string_view to_string(Infrastructure_stop_reason reason) noexcept {
    switch (reason) {
        case Infrastructure_stop_reason::None:      return "none";
        case Infrastructure_stop_reason::Requested: return "requested";
        case Infrastructure_stop_reason::Sigint:    return "sigint";
        case Infrastructure_stop_reason::Sigterm:   return "sigterm";
        default:                                    return "Unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name,
                                         Infrastructure_stop_reason& out) noexcept {
    if (name == "none")      { out = Infrastructure_stop_reason::None; return true; }
    if (name == "requested") { out = Infrastructure_stop_reason::Requested; return true; }
    if (name == "sigint")    { out = Infrastructure_stop_reason::Sigint; return true; }
    if (name == "sigterm")   { out = Infrastructure_stop_reason::Sigterm; return true; }
    return false;
}

inline void to_json(nlohmann::json& j, const Infrastructure_stop_reason reason) {
    j = std::string(to_string(reason));
}

inline void from_json(const nlohmann::json& j, Infrastructure_stop_reason& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

} // namespace nexigen::app

template <>
struct std::formatter<nexigen::app::Infrastructure_stop_reason>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::app::Infrastructure_stop_reason reason, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(
            nexigen::app::to_string(reason), ctx);
    }
};
