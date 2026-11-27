// nexigen::canopen — immutable CiA 306 object dictionary.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nexigen/canopen/eds_cob_id_entry.hpp>
#include <nexigen/canopen/eds_object.hpp>
#include <nexigen/canopen/eds_pdo_mapping.hpp>

namespace nexigen::canopen {

/// @brief Immutable parsed dictionary shared by all bindings of one file.
class EdsDictionary {
public:
    /// @brief Default limits.
    static constexpr std::size_t max_objects{65'536U};
    static constexpr std::size_t max_names{128U}; ///< Maximum parameter name bytes.

    /// @brief Append one parsed object (parser only).
    /// @param object Object to append.
    void add_object(EdsObject object);

    /// @brief Sort, de-duplicate and derive mappings/COB-IDs.
    void finalize();

    /// @brief Look up one object.
    /// @param index Object index.
    /// @param subindex Subindex.
    /// @return Object or `nullptr`.
    [[nodiscard]] const EdsObject* find(std::uint16_t index, std::uint8_t subindex) const noexcept;
    /// @brief Look up the var/record entry with subindex zero.
    /// @param index Object index.
    /// @return Object or `nullptr`.
    [[nodiscard]] const EdsObject* find(std::uint16_t index) const noexcept {
        return find(index, 0U);
    }

    /// @brief Parsed objects.
    /// @return Borrowed vector.
    [[nodiscard]] const std::vector<EdsObject>& objects() const noexcept { return objects_; }
    /// @brief Default PDO mappings.
    /// @return Borrowed vector.
    [[nodiscard]] const std::vector<EdsPdoMapping>& mappings() const noexcept { return mappings_; }
    /// @brief EDS COB-ID overrides.
    /// @return Borrowed vector.
    [[nodiscard]] const std::vector<EdsCobIdEntry>& cob_ids() const noexcept { return cob_ids_; }
    /// @brief Number of objects.
    /// @return Object count.
    [[nodiscard]] std::size_t object_count() const noexcept { return objects_.size(); }

    /// @brief Source path recorded by the loader.
    /// @return Path text.
    [[nodiscard]] const std::string& source_path() const noexcept { return source_path_; }
    /// @brief Set the source path (loader only).
    /// @param path File path.
    void set_source_path(std::string path) { source_path_ = std::move(path); }
    /// @brief Content hash recorded by the parser.
    /// @return FNV-1a 64-bit hash.
    [[nodiscard]] std::uint64_t content_hash() const noexcept { return content_hash_; }
    /// @brief Set the content hash (parser only).
    /// @param hash Content hash.
    void set_content_hash(std::uint64_t hash) noexcept { content_hash_ = hash; }
    /// @brief File size recorded by the loader.
    /// @return Byte count.
    [[nodiscard]] std::size_t source_size() const noexcept { return source_size_; }
    /// @brief Set the source size (loader only).
    /// @param size Byte count.
    void set_source_size(std::size_t size) noexcept { source_size_ = size; }

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with object/mapping/COB-ID counts.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Derive mappings and COB-ID overrides.
    void derive_tables();

    std::vector<EdsObject> objects_{};      ///< Sorted objects.
    std::vector<EdsPdoMapping> mappings_{}; ///< Default mappings.
    std::vector<EdsCobIdEntry> cob_ids_{};  ///< COB-ID overrides.
    std::string source_path_{};             ///< Source path.
    std::uint64_t content_hash_{0};         ///< Content hash.
    std::size_t source_size_{0};            ///< Source byte count.
};

} // namespace nexigen::canopen
