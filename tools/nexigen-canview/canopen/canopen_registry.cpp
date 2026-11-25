#include "canopen_registry.hpp"

#include <format>
#include <utility>

#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/core/enum_traits.hpp>

namespace nexigen::canview {

namespace {

std::uint64_t heartbeat_timeout(const EdsDictionary& dictionary) noexcept {
    const EdsObject* producer = dictionary.find(0x1017U, 0U);
    if (producer != nullptr && producer->has_default && producer->default_value.valid) {
        const std::uint64_t milliseconds = producer->default_value.value;
        if (milliseconds > 0U) {
            return milliseconds * 1'000'000ULL * 3ULL;
        }
    }
    return 3'000'000'000ULL;
}

} // namespace

std::string NodeBinding::to_string() const {
    return std::format("NodeBinding(channel={}, node={}, alias='{}', colour=0x{:08x}, hash={})",
                       channel_id, node_id, alias, colour, eds_hash);
}

void to_json(nlohmann::json& j, const NodeBinding& binding) {
    j = nlohmann::json{{"channel_id", binding.channel_id}, {"node_id", binding.node_id},
                       {"alias", binding.alias},           {"colour", binding.colour},
                       {"eds_path", binding.eds_path},     {"eds_hash", binding.eds_hash},
                       {"nmt_state", binding.nmt_state}};
}

void from_json(const nlohmann::json& j, NodeBinding& binding) noexcept {
    if (!j.is_object()) {
        return;
    }
    static_cast<void>(core::detail::read_unsigned(j, "channel_id", binding.channel_id));
    static_cast<void>(core::detail::read_unsigned(j, "node_id", binding.node_id));
    static_cast<void>(core::detail::read_string(j, "alias", binding.alias));
    static_cast<void>(core::detail::read_unsigned(j, "colour", binding.colour));
    static_cast<void>(core::detail::read_string(j, "eds_path", binding.eds_path));
    static_cast<void>(core::detail::read_unsigned(j, "eds_hash", binding.eds_hash));
    core::mandate::read_enum(j, "nmt_state", binding.nmt_state);
}

bool NodeBinding::stale(std::uint64_t now_ns) const noexcept {
    return heartbeat_seen && now_ns > last_heartbeat_ns &&
           (now_ns - last_heartbeat_ns) > heartbeat_timeout_ns;
}

std::uint16_t CanOpenRegistry::slot_for(std::uint16_t channel_id,
                                        std::uint8_t node_id) const noexcept {
    for (std::size_t slot = 0; slot < slots_.size(); ++slot) {
        if (!slots_[slot].has_value()) {
            continue;
        }
        if (slots_[slot]->channel_id == channel_id && slots_[slot]->node_id == node_id) {
            return static_cast<std::uint16_t>(slot);
        }
    }
    return static_cast<std::uint16_t>(capacity);
}

nexigen::core::Result<std::uint16_t>
CanOpenRegistry::bind(std::uint16_t channel_id, std::uint8_t node_id, std::string alias,
                      std::uint32_t colour, std::shared_ptr<const EdsDictionary> dictionary,
                      std::string eds_path, bool replace) {
    if (node_id == 0U || node_id > 127U) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    const std::uint16_t existing = slot_for(channel_id, node_id);
    if (existing < capacity && !replace) {
        return std::unexpected(nexigen::core::ErrorCode::Conflict);
    }
    std::uint16_t slot = existing;
    if (slot >= capacity) {
        for (std::size_t candidate = 0; candidate < slots_.size(); ++candidate) {
            if (!slots_[candidate].has_value()) {
                slot = static_cast<std::uint16_t>(candidate);
                break;
            }
        }
    }
    if (slot >= capacity) {
        return std::unexpected(nexigen::core::ErrorCode::ResourceExhausted);
    }
    NodeBinding binding{};
    binding.registry_slot = slot;
    binding.channel_id = channel_id;
    binding.node_id = node_id;
    binding.alias = std::move(alias);
    binding.colour = colour;
    binding.dictionary = std::move(dictionary);
    binding.eds_path = std::move(eds_path);
    binding.eds_hash = binding.dictionary != nullptr ? binding.dictionary->content_hash() : 0U;
    binding.heartbeat_timeout_ns =
        binding.dictionary != nullptr ? heartbeat_timeout(*binding.dictionary) : 3'000'000'000ULL;
    if (existing >= capacity) {
        ++size_;
    }
    slots_[slot] = std::move(binding);
    ++revision_;
    return slot;
}

bool CanOpenRegistry::unbind(std::uint16_t channel_id, std::uint8_t node_id) noexcept {
    const std::uint16_t slot = slot_for(channel_id, node_id);
    return slot < capacity && unbind_slot(slot);
}

bool CanOpenRegistry::unbind_slot(std::uint16_t slot) noexcept {
    if (slot >= slots_.size() || !slots_[slot].has_value()) {
        return false;
    }
    slots_[slot].reset();
    --size_;
    ++revision_;
    return true;
}

const NodeBinding* CanOpenRegistry::find(std::uint16_t channel_id,
                                         std::uint8_t node_id) const noexcept {
    const std::uint16_t slot = slot_for(channel_id, node_id);
    return slot < capacity ? &*slots_[slot] : nullptr;
}

NodeBinding* CanOpenRegistry::find(std::uint16_t channel_id, std::uint8_t node_id) noexcept {
    const std::uint16_t slot = slot_for(channel_id, node_id);
    return slot < capacity ? &*slots_[slot] : nullptr;
}

const NodeBinding* CanOpenRegistry::at(std::uint16_t slot) const noexcept {
    if (slot >= slots_.size() || !slots_[slot].has_value()) {
        return nullptr;
    }
    return &*slots_[slot];
}

NodeBinding* CanOpenRegistry::at(std::uint16_t slot) noexcept {
    if (slot >= slots_.size() || !slots_[slot].has_value()) {
        return nullptr;
    }
    return &*slots_[slot];
}

std::size_t CanOpenRegistry::refresh_dictionary(std::string_view path,
                                                std::shared_ptr<const EdsDictionary> dictionary,
                                                std::uint64_t hash) noexcept {
    std::size_t updated = 0U;
    for (auto& slot : slots_) {
        if (!slot.has_value() || slot->eds_path != path) {
            continue;
        }
        slot->dictionary = dictionary;
        slot->eds_hash = hash;
        if (dictionary != nullptr) {
            const EdsObject* producer = dictionary->find(0x1017U, 0U);
            if (producer != nullptr && producer->has_default && producer->default_value.valid &&
                producer->default_value.value > 0U) {
                slot->heartbeat_timeout_ns =
                    static_cast<std::uint64_t>(producer->default_value.value) * 1'000'000ULL * 3ULL;
            }
        }
        ++updated;
    }
    if (updated > 0U) {
        ++revision_;
    }
    return updated;
}

void CanOpenRegistry::observe_heartbeat(std::uint16_t slot, NmtState state,
                                        std::uint64_t now_ns) noexcept {
    NodeBinding* binding = at(slot);
    if (binding == nullptr) {
        return;
    }
    binding->nmt_state = state;
    binding->last_heartbeat_ns = now_ns;
    binding->heartbeat_seen = true;
    ++revision_;
}

void CanOpenRegistry::set_nmt_state(std::uint16_t slot, NmtState state) noexcept {
    NodeBinding* binding = at(slot);
    if (binding == nullptr || binding->nmt_state == state) {
        return;
    }
    binding->nmt_state = state;
    ++revision_;
}

void CanOpenRegistry::clear() noexcept {
    for (auto& slot : slots_) {
        slot.reset();
    }
    size_ = 0U;
    ++revision_;
}

std::string CanOpenRegistry::to_string() const {
    return std::format("CanOpenRegistry(bindings={}, capacity={}, revision={})", size_, capacity,
                       revision_);
}

} // namespace nexigen::canview
