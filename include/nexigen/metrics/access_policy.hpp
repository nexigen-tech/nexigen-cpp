// nexigen::metrics — AccessPolicy enum.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::metrics {

/// Whether the read side (.read() / .snapshot()) is part of the public API.
enum class AccessPolicy : std::uint8_t {
    Readable,
    WriteOnly,
};

[[nodiscard]] constexpr std::string_view to_string(AccessPolicy policy) noexcept {
    switch (policy) {
        case AccessPolicy::Readable:  return "Readable";
        case AccessPolicy::WriteOnly: return "WriteOnly";
        default:                      return "Unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name, AccessPolicy& out) noexcept {
    if (name == "Readable") {
        out = AccessPolicy::Readable;
        return true;
    }
    if (name == "WriteOnly") {
        out = AccessPolicy::WriteOnly;
        return true;
    }
    return false;
}

inline void from_json(const nlohmann::json& j, AccessPolicy& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}


inline void to_json(nlohmann::json& j, const AccessPolicy accessPolicy) {
    j = std::string(to_string(accessPolicy));
}

} // namespace nexigen::metrics
