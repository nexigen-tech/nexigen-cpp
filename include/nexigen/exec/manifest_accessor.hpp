// nexigen::exec — manifest format accessor
// (change p3-arxml-manifest-accessor; spec exec/manifest-accessor).
//
// One entry point that accepts the existing JSON suite manifest or the
// documented ARXML execution-manifest profile and returns the same normalized
// SuiteManifest. The XML reader is deliberately small: no external dependency,
// no XSD, no reference resolution.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/serialization_mandate.hpp>
#include <nexigen/exec/suite_manifest.hpp>

namespace nexigen::exec {

struct MachineManifest;

/// @brief Source format of a suite manifest.
enum class ManifestFormat : std::uint8_t {
    Json,
    Arxml,
};

[[nodiscard]] constexpr std::string_view to_string(ManifestFormat value) noexcept {
    return core::enum_name(value);
}
[[nodiscard]] constexpr bool from_string(std::string_view name, ManifestFormat& out) noexcept {
    return core::enum_value(name, out);
}
inline void to_json(nlohmann::json& j, const ManifestFormat value) {
    core::mandate::enum_to_json(j, value);
}
inline void from_json(const nlohmann::json& j, ManifestFormat& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

/// @brief Format detection and normalized manifest loading.
class ManifestAccessor {
public:
    /// @brief Detect the source format from the path and content.
    [[nodiscard]] static ManifestFormat detect(std::string_view path,
                                               std::string_view content) noexcept;

    /// @brief Load a JSON or ARXML suite manifest into the normalized model.
    ///
    /// @param path Manifest path.
    /// @param out Receives the parsed and validated manifest.
    /// @return Success, `InvalidArgument` for malformed content, or
    ///         `SystemError` when the file cannot be read.
    [[nodiscard]] static core::Status load(std::string_view path, SuiteManifest& out) noexcept;

    /// @brief Load a JSON or ARXML Machine Manifest and resolve its references.
    [[nodiscard]] static core::Status load_machine(std::string_view path,
                                                   MachineManifest& out) noexcept;
};

/// @brief Load a suite manifest with automatic JSON/ARXML detection.
[[nodiscard]] core::Status load_suite_manifest_auto(std::string_view path,
                                                    SuiteManifest& out) noexcept;

/// @brief Load a JSON suite manifest after verifying its SHA-256.
///
/// ARXML manifests carry no byte-level digest contract, so a non-empty
/// `expected_sha256_hex` restricts this path to JSON.
///
/// @param path Manifest path.
/// @param expected_sha256_hex Expected digest; empty disables the check.
/// @param out Receives the parsed and validated manifest.
/// @return Success, `SecurityError` on a digest mismatch, or the usual parse
///         and validation errors.
/// @note Spec `core/integrity`, requirement "Verified file load".
[[nodiscard]] core::Status load_suite_manifest_auto(std::string_view path,
                                                    std::string_view expected_sha256_hex,
                                                    SuiteManifest& out) noexcept;

/// @brief Load a Machine Manifest with automatic JSON/ARXML detection.
[[nodiscard]] core::Status load_machine_manifest_auto(std::string_view path,
                                                      MachineManifest& out) noexcept;

} // namespace nexigen::exec

namespace nexigen::core {

template <> struct EnumTraits<nexigen::exec::ManifestFormat> {
    struct Entry {
        nexigen::exec::ManifestFormat value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::exec::ManifestFormat::Json, "Json"},
        {nexigen::exec::ManifestFormat::Arxml, "Arxml"},
    }};
};

} // namespace nexigen::core

template <>
struct std::formatter<nexigen::exec::ManifestFormat> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::exec::ManifestFormat value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::exec::to_string(value), ctx);
    }
};
