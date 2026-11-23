// nexigen-logview — save formats.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::logview {

/// @brief Record file formats the viewer can write.
enum class ExportFormat : std::uint8_t {
    Jsonl, ///< `nexigen-logd`-compatible JSON lines.
    Csv,   ///< Quoted comma-separated rows.
    Txt,   ///< Plain readable columns.
};

} // namespace nexigen::logview

namespace nexigen::core {

/// @brief Vocabulary table for `ExportFormat`.
template <> struct EnumTraits<nexigen::logview::ExportFormat> {
    /// @brief One table row.
    struct Entry {
        nexigen::logview::ExportFormat value; ///< Enumerator.
        std::string_view name;                ///< Canonical lowercase wire name.
    };
    /// @brief Table covering every enumerator.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::logview::ExportFormat::Jsonl, "jsonl"},
        {nexigen::logview::ExportFormat::Csv, "csv"},
        {nexigen::logview::ExportFormat::Txt, "txt"},
    }};
};
} // namespace nexigen::core

namespace nexigen::logview {

/// @brief Canonical name of an export format.
/// @param format Format to render.
/// @return Static-storage name.
[[nodiscard]] constexpr std::string_view to_string(ExportFormat format) noexcept {
    return nexigen::core::enum_name(format);
}

/// @brief Parse a canonical export-format name.
/// @param name Candidate spelling.
/// @param out Destination, untouched on a miss.
/// @return `true` on an exact match.
[[nodiscard]] constexpr bool from_string(std::string_view name, ExportFormat& out) noexcept {
    return nexigen::core::enum_value(name, out);
}

/// @brief ADL JSON writer.
/// @param j Destination JSON value.
/// @param format Format to serialize.
inline void to_json(nlohmann::json& j, ExportFormat format) {
    j = to_string(format);
}

/// @brief Non-throwing ADL JSON reader.
/// @param j Source JSON value.
/// @param out Destination, untouched on invalid input.
inline void from_json(const nlohmann::json& j, ExportFormat& out) noexcept {
    if (!j.is_string()) {
        return;
    }
    static_cast<void>(from_string(j.get_ref<const nlohmann::json::string_t&>(), out));
}

} // namespace nexigen::logview

/// @brief `std::formatter` for `ExportFormat`.
template <>
struct std::formatter<nexigen::logview::ExportFormat> : std::formatter<std::string_view> {
    /// @brief Format the canonical name.
    template <class Ctx> auto format(nexigen::logview::ExportFormat format, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::logview::to_string(format), ctx);
    }
};
