// nexigen-canview — immutable CiA 306 object dictionary.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "canopen_types.hpp"

namespace nexigen::canview {

/// @brief Numeric EDS value, optionally relative to the node id.
struct EdsNumeric {
    bool valid{false};            ///< Whether the text parsed as a number.
    bool node_relative{false};    ///< Value contains `$NODEID`.
    std::uint32_t value{0};       ///< Literal part.
    std::uint32_t node_offset{0}; ///< Added to the node id when relative.

    /// @brief Resolve against one node id.
    /// @param node_id Node id (1..127).
    /// @return Resolved 32-bit value.
    [[nodiscard]] std::uint32_t resolve(std::uint8_t node_id) const noexcept {
        return value + (node_relative ? static_cast<std::uint32_t>(node_id) : 0U) + node_offset;
    }
};

/// @brief CiA 306 object type.
enum class EdsObjectType : std::uint8_t {
    Var,     ///< Single variable.
    Array,   ///< Array object.
    Record,  ///< Record object.
    Unknown, ///< Missing/unrecognised.
};

/// @brief One object dictionary entry.
struct EdsObject {
    std::uint16_t index{0};                     ///< Object index.
    std::uint8_t subindex{0};                   ///< Subindex.
    std::string name{};                         ///< Parameter name.
    EdsObjectType type{EdsObjectType::Unknown}; ///< Object type.
    std::uint16_t data_type{0};                 ///< CiA 301 data type code.
    EdsAccess access{EdsAccess::Unknown};       ///< Access type.
    bool pdo_mappable{false};                   ///< `PDOMapping=1`.
    bool has_default{false};                    ///< Numeric default value present.
    EdsNumeric default_value{};                 ///< Numeric default.
    bool has_cob_id{false};                     ///< `COB-ID` key present.
    EdsNumeric cob_id{};                        ///< COB-ID value.
    std::uint16_t sub_number{0};                ///< Declared subobject count.
    std::uint16_t compact_sub_obj{0};           ///< `CompactSubObj` count.
};

/// @brief One PDO mapping entry.
struct EdsMappingEntry {
    std::uint16_t index{0};   ///< Mapped object index.
    std::uint8_t subindex{0}; ///< Mapped subindex.
    std::uint8_t bits{0};     ///< Bit length; zero terminates the mapping.
};

/// @brief Default PDO mapping from one `[1600..17FF]`/`[1A00..1BFF]` object.
struct EdsPdoMapping {
    std::uint16_t mapping_index{0};           ///< Mapping object index.
    std::uint8_t pdo{0};                      ///< 1..4.
    bool transmit{false};                     ///< TPDO mapping when true.
    std::array<EdsMappingEntry, 8> entries{}; ///< Mapping entries.
    std::uint8_t count{0};                    ///< Valid entries.
};

/// @brief One EDS-provided COB-ID override.
struct EdsCobIdEntry {
    std::uint16_t comm_index{0}; ///< Communication parameter index.
    std::uint8_t pdo{0};         ///< 1..4.
    bool transmit{false};        ///< TPDO COB-ID when true.
    EdsNumeric cob_id{};         ///< Raw value, maybe node-relative.
    bool disabled{false};        ///< Bit 31 set: PDO disabled.
};

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

} // namespace nexigen::canview
