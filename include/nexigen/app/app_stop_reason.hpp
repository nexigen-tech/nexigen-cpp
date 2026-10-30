// nexigen::app — why the application stopped (change add-app-class; spec
// app/application "App stop reason is serializable").
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

/// Why the application loop stopped.
enum class App_stop_reason : std::uint8_t {
    None,      ///< No stop requested yet.
    Requested, ///< Host code requested a stop (explicit call or false tick).
    Sigint,    ///< SIGINT received.
    Sigterm,   ///< SIGTERM received.
};

[[nodiscard]] constexpr std::string_view to_string(App_stop_reason reason) noexcept {
    switch (reason) {
        case App_stop_reason::None:      return "none";
        case App_stop_reason::Requested: return "requested";
        case App_stop_reason::Sigint:    return "sigint";
        case App_stop_reason::Sigterm:   return "sigterm";
        default:                         return "Unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name,
                                         App_stop_reason& out) noexcept {
    if (name == "none")      { out = App_stop_reason::None; return true; }
    if (name == "requested") { out = App_stop_reason::Requested; return true; }
    if (name == "sigint")    { out = App_stop_reason::Sigint; return true; }
    if (name == "sigterm")   { out = App_stop_reason::Sigterm; return true; }
    return false;
}

inline void to_json(nlohmann::json& j, const App_stop_reason reason) {
    j = std::string(to_string(reason));
}

inline void from_json(const nlohmann::json& j, App_stop_reason& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

} // namespace nexigen::app

template <>
struct std::formatter<nexigen::app::App_stop_reason>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::app::App_stop_reason reason, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(
            nexigen::app::to_string(reason), ctx);
    }
};
