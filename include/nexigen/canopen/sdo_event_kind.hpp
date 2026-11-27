// nexigen::canopen — SDO event vocabulary.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::canopen {

/// @brief Kind of one decoded SDO event.
enum class SdoEventKind : std::uint8_t {
    None,                     ///< No event.
    UploadRequest,            ///< SDO upload request.
    ExpeditedWriteRequest,    ///< Expedited download request.
    SegmentedDownloadRequest, ///< Segmented download initiate.
    UploadResponse,           ///< Expedited upload response.
    DownloadResponse,         ///< Download acknowledgement without completion.
    DownloadSegment,          ///< Segmented download segment.
    UploadSegment,            ///< Segmented upload segment.
    CompletedWrite,           ///< A download completed and its response arrived.
    Abort,                    ///< SDO abort.
    TooLarge,                 ///< Transfer exceeded the assembly buffer.
};

} // namespace nexigen::canopen

namespace nexigen::core {

/// @brief Vocabulary table for `SdoEventKind`.
template <> struct EnumTraits<nexigen::canopen::SdoEventKind> {
    /// @brief One value/name row.
    struct Entry {
        nexigen::canopen::SdoEventKind value; ///< Enumerator.
        std::string_view name;                ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 11> entries{{
        {nexigen::canopen::SdoEventKind::None, "None"},
        {nexigen::canopen::SdoEventKind::UploadRequest, "UploadRequest"},
        {nexigen::canopen::SdoEventKind::ExpeditedWriteRequest, "ExpeditedWriteRequest"},
        {nexigen::canopen::SdoEventKind::SegmentedDownloadRequest, "SegmentedDownloadRequest"},
        {nexigen::canopen::SdoEventKind::UploadResponse, "UploadResponse"},
        {nexigen::canopen::SdoEventKind::DownloadResponse, "DownloadResponse"},
        {nexigen::canopen::SdoEventKind::DownloadSegment, "DownloadSegment"},
        {nexigen::canopen::SdoEventKind::UploadSegment, "UploadSegment"},
        {nexigen::canopen::SdoEventKind::CompletedWrite, "CompletedWrite"},
        {nexigen::canopen::SdoEventKind::Abort, "Abort"},
        {nexigen::canopen::SdoEventKind::TooLarge, "TooLarge"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canopen {

/// @brief Canonical name of an SDO event kind.
[[nodiscard]] constexpr std::string_view to_string(SdoEventKind value) noexcept {
    return core::enum_name(value);
}
/// @brief Parse an SDO event kind.
[[nodiscard]] constexpr bool from_string(std::string_view name, SdoEventKind& out) noexcept {
    return core::enum_value(name, out);
}
/// @brief Write an SDO event kind as JSON.
inline void to_json(nlohmann::json& j, const SdoEventKind value) {
    core::mandate::enum_to_json(j, value);
}
/// @brief Read an SDO event kind without throwing.
inline void from_json(const nlohmann::json& j, SdoEventKind& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::canopen
