#include "eds_dictionary.hpp"

#include <algorithm>
#include <format>

namespace nexigen::canview {

namespace {

void append_mapping(std::vector<EdsPdoMapping>& mappings, std::uint16_t mapping_index,
                    std::uint8_t pdo, bool transmit, std::uint32_t value) {
    auto found = std::find_if(mappings.begin(), mappings.end(),
                              [mapping_index](const EdsPdoMapping& mapping) {
                                  return mapping.mapping_index == mapping_index;
                              });
    if (found == mappings.end()) {
        EdsPdoMapping mapping{};
        mapping.mapping_index = mapping_index;
        mapping.pdo = pdo;
        mapping.transmit = transmit;
        mappings.push_back(mapping);
        found = mappings.end() - 1;
    }
    if (found->count >= found->entries.size()) {
        return;
    }
    EdsMappingEntry entry{};
    entry.index = static_cast<std::uint16_t>((value >> 16U) & 0xFFFFU);
    entry.subindex = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    entry.bits = static_cast<std::uint8_t>(value & 0xFFU);
    if (entry.bits != 0U) {
        found->entries[found->count] = entry;
        ++found->count;
    }
}

void maybe_mapping(const EdsObject& object, std::vector<EdsPdoMapping>& mappings) {
    if (object.subindex < 1U || !object.has_default) {
        return;
    }
    if (object.index >= 0x1A00U && object.index <= 0x1BFFU) {
        append_mapping(mappings, object.index,
                       static_cast<std::uint8_t>(object.index - 0x1A00U + 1U), true,
                       object.default_value.value);
    } else if (object.index >= 0x1600U && object.index <= 0x17FFU) {
        append_mapping(mappings, object.index,
                       static_cast<std::uint8_t>(object.index - 0x1600U + 1U), false,
                       object.default_value.value);
    }
}

void maybe_cob_id(const EdsObject& object, std::vector<EdsCobIdEntry>& cob_ids) {
    const bool tpdo = object.index >= 0x1800U && object.index <= 0x19FFU;
    const bool rpdo = object.index >= 0x1400U && object.index <= 0x15FFU;
    if ((!tpdo && !rpdo) || object.subindex != 1U) {
        return;
    }
    EdsCobIdEntry entry{};
    entry.comm_index = object.index;
    entry.pdo = tpdo ? static_cast<std::uint8_t>(object.index - 0x1800U + 1U)
                     : static_cast<std::uint8_t>(object.index - 0x1400U + 1U);
    entry.transmit = tpdo;
    if (object.has_cob_id) {
        entry.cob_id = object.cob_id;
    } else if (object.has_default) {
        entry.cob_id = object.default_value;
    }
    if (!entry.cob_id.valid) {
        return;
    }
    entry.disabled = (entry.cob_id.value & 0x80000000U) != 0U;
    cob_ids.push_back(entry);
}

} // namespace

void EdsDictionary::add_object(EdsObject object) {
    objects_.push_back(std::move(object));
}

void EdsDictionary::finalize() {
    std::stable_sort(objects_.begin(), objects_.end(),
                     [](const EdsObject& lhs, const EdsObject& rhs) {
                         if (lhs.index != rhs.index) {
                             return lhs.index < rhs.index;
                         }
                         return lhs.subindex < rhs.subindex;
                     });
    objects_.erase(std::unique(objects_.begin(), objects_.end(),
                               [](const EdsObject& lhs, const EdsObject& rhs) {
                                   return lhs.index == rhs.index && lhs.subindex == rhs.subindex;
                               }),
                   objects_.end());
    derive_tables();
}

const EdsObject* EdsDictionary::find(std::uint16_t index, std::uint8_t subindex) const noexcept {
    const auto it = std::lower_bound(
        objects_.begin(), objects_.end(), std::pair{index, subindex},
        [](const EdsObject& object, const std::pair<std::uint16_t, std::uint8_t>& key) {
            return object.index < key.first ||
                   (object.index == key.first && object.subindex < key.second);
        });
    if (it == objects_.end() || it->index != index || it->subindex != subindex) {
        return nullptr;
    }
    return &*it;
}

void EdsDictionary::derive_tables() {
    mappings_.clear();
    cob_ids_.clear();
    for (const EdsObject& object : objects_) {
        maybe_mapping(object, mappings_);
        maybe_cob_id(object, cob_ids_);
    }
}

std::string EdsDictionary::to_string() const {
    return std::format("EdsDictionary(objects={}, mappings={}, cob_ids={}, path='{}', hash={})",
                       objects_.size(), mappings_.size(), cob_ids_.size(), source_path_,
                       content_hash_);
}

} // namespace nexigen::canview
