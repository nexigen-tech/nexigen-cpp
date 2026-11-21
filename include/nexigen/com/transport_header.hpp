// nexigen::com — transport sequence header for telemetry pub/sub services
// (change record-telemetry-sessions; spec com/publish-subscribe "Transport
// sequence header").
//
// The header travels beside the payload in the iceoryx2 user header. The
// publisher increments `sequence` on every send; subscribers compare it per
// publisher to report how many samples were missed.
#pragma once

#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::com {

/// @brief Publisher identity and per-publisher sequence.
///
/// `publisher_id` changes on every publisher construction, so a restart starts
/// a new sequence baseline. The header is trivially copyable and pointer-free.
///
/// @note Spec `com/publish-subscribe`, requirement "Transport sequence header".
struct TransportHeader {
    char app_id[4]{};              ///< Four-byte ASCII application code.
    std::uint32_t pid{0};          ///< Publisher process id.
    std::uint64_t publisher_id{0}; ///< Unique per publisher construction.
    std::uint64_t sequence{0};     ///< Monotonic send counter.

    /// @brief The application code as a bounded view.
    ///
    /// @return Up to four bytes, trimmed at the first NUL.
    [[nodiscard]] std::string_view app_id_text() const noexcept {
        std::size_t n = 0;
        while (n < sizeof(app_id) && app_id[n] != '\0') {
            ++n;
        }
        return std::string_view(app_id, n);
    }

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable header summary.
    [[nodiscard]] std::string to_string() const {
        return "TransportHeader(app=" + std::string(app_id_text()) +
               ", pid=" + std::to_string(pid) + ", publisher=" + std::to_string(publisher_id) +
               ", sequence=" + std::to_string(sequence) + ")";
    }
};

static_assert(sizeof(TransportHeader) == 24U);
static_assert(std::is_trivially_copyable_v<TransportHeader>);

/// @brief Free ADL diagnostic helper.
[[nodiscard]] inline std::string to_string(const TransportHeader& header) {
    return header.to_string();
}

/// @brief Cold-path JSON writer.
inline void to_json(nlohmann::json& j, const TransportHeader& header) {
    j = nlohmann::json{{"app_id", std::string(header.app_id_text())},
                       {"pid", header.pid},
                       {"publisher_id", header.publisher_id},
                       {"sequence", header.sequence}};
}

/// @brief Hostile-input-safe JSON reader: wrong types leave the destination unchanged.
inline void from_json(const nlohmann::json& j, TransportHeader& header) noexcept {
    if (!j.is_object()) {
        return;
    }
    if (const auto it = j.find("app_id"); it != j.end() && it->is_string()) {
        const std::string text = it->get<std::string>();
        const std::size_t n =
            text.size() < sizeof(header.app_id) ? text.size() : sizeof(header.app_id);
        std::memset(header.app_id, 0, sizeof(header.app_id));
        std::memcpy(header.app_id, text.data(), n);
    }
    const auto read_u32 = [&j](const char* key, std::uint32_t& out) {
        if (const auto it = j.find(key); it != j.end() && it->is_number_unsigned()) {
            out = it->get<std::uint32_t>();
        }
    };
    const auto read_u64 = [&j](const char* key, std::uint64_t& out) {
        if (const auto it = j.find(key); it != j.end() && it->is_number_unsigned()) {
            out = it->get<std::uint64_t>();
        }
    };
    read_u32("pid", header.pid);
    read_u64("publisher_id", header.publisher_id);
    read_u64("sequence", header.sequence);
}

} // namespace nexigen::com

/// @brief `std::formatter` rendering the header diagnostic.
template <>
struct std::formatter<nexigen::com::TransportHeader> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::com::TransportHeader& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
