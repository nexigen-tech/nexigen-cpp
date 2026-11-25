#include "co_runtime.hpp"

#include <cstring>

#include "cob_id_router.hpp"

namespace nexigen::canview {

namespace {

std::uint32_t read_le32(const std::uint8_t* data) noexcept {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

} // namespace

std::uint32_t CoNodeRuntime::default_cob(bool transmit, std::uint8_t pdo,
                                         std::uint8_t node_id) noexcept {
    const std::uint32_t base = transmit ? 0x180U : 0x200U;
    return base + (static_cast<std::uint32_t>(pdo) - 1U) * 0x100U + node_id;
}

void CoNodeRuntime::reset() noexcept {
    snapshots_ = {};
    head_ = 0U;
    count_ = 0U;
    dictionary_ = nullptr;
    dictionary_hash_ = 0U;
    initialized_ = false;
}

void CoNodeRuntime::set_mapping(PdoSnapshot& snapshot, bool transmit, std::uint8_t pdo,
                                std::uint8_t subindex, std::uint32_t value) noexcept {
    if (pdo == 0U || pdo > 4U || subindex == 0U || subindex > 8U) {
        return;
    }
    EdsPdoMapping& mapping = transmit ? snapshot.tpdo[pdo - 1U] : snapshot.rpdo[pdo - 1U];
    mapping.transmit = transmit;
    mapping.pdo = pdo;
    EdsMappingEntry entry{};
    entry.index = static_cast<std::uint16_t>((value >> 16U) & 0xFFFFU);
    entry.subindex = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    entry.bits = static_cast<std::uint8_t>(value & 0xFFU);
    mapping.entries[subindex - 1U] = entry;
    if (subindex > mapping.count) {
        mapping.count = subindex;
    }
}

void CoNodeRuntime::build_defaults() noexcept {
    PdoSnapshot snapshot{};
    snapshot.valid = true;
    snapshot.epoch = 0U;
    for (std::uint8_t pdo = 1U; pdo <= 4U; ++pdo) {
        snapshot.tpdo[pdo - 1U].pdo = pdo;
        snapshot.tpdo[pdo - 1U].transmit = true;
        snapshot.rpdo[pdo - 1U].pdo = pdo;
        snapshot.rpdo[pdo - 1U].transmit = false;
        snapshot.tpdo_cob[pdo - 1U] = default_cob(true, pdo, node_id_);
        snapshot.rpdo_cob[pdo - 1U] = default_cob(false, pdo, node_id_);
    }
    if (dictionary_ != nullptr) {
        for (const EdsPdoMapping& mapping : dictionary_->mappings()) {
            if (mapping.pdo == 0U || mapping.pdo > 4U) {
                continue;
            }
            if (mapping.transmit) {
                snapshot.tpdo[mapping.pdo - 1U] = mapping;
            } else {
                snapshot.rpdo[mapping.pdo - 1U] = mapping;
            }
        }
        for (const EdsCobIdEntry& entry : dictionary_->cob_ids()) {
            if (entry.pdo == 0U || entry.pdo > 4U) {
                continue;
            }
            if (entry.disabled) {
                if (entry.transmit) {
                    snapshot.tpdo_cob[entry.pdo - 1U] = 0U;
                } else {
                    snapshot.rpdo_cob[entry.pdo - 1U] = 0U;
                }
                continue;
            }
            const std::uint32_t resolved = entry.cob_id.resolve(node_id_);
            if (resolved > 0x7FFU) {
                continue;
            }
            if (entry.transmit) {
                snapshot.tpdo_cob[entry.pdo - 1U] = resolved;
            } else {
                snapshot.rpdo_cob[entry.pdo - 1U] = resolved;
            }
        }
    }
    snapshots_[0] = snapshot;
    head_ = 0U;
    count_ = 1U;
    initialized_ = true;
}

void CoNodeRuntime::init(const EdsDictionary& dictionary, std::uint16_t channel_id,
                         std::uint8_t node_id, std::uint16_t slot) noexcept {
    reset();
    dictionary_ = &dictionary;
    channel_id_ = channel_id;
    node_id_ = node_id;
    slot_ = slot;
    dictionary_hash_ = dictionary.content_hash();
    build_defaults();
}

void CoNodeRuntime::allocate(PdoSnapshot snapshot) noexcept {
    if (count_ < snapshot_count) {
        snapshots_[(head_ + count_) % snapshot_count] = snapshot;
        ++count_;
        return;
    }
    snapshots_[head_] = snapshot;
    head_ = (head_ + 1U) % snapshot_count;
}

std::uint8_t CoNodeRuntime::current_epoch() const noexcept {
    return count_ == 0U ? 0U : snapshots_[(head_ + count_ - 1U) % snapshot_count].epoch;
}

const PdoSnapshot& CoNodeRuntime::current() const noexcept {
    static const PdoSnapshot empty{};
    return count_ == 0U ? empty : snapshots_[(head_ + count_ - 1U) % snapshot_count];
}

const PdoSnapshot* CoNodeRuntime::snapshot(std::uint8_t epoch, bool& stale) const noexcept {
    stale = false;
    if (count_ == 0U) {
        return nullptr;
    }
    for (std::size_t index = 0U; index < count_; ++index) {
        const PdoSnapshot& candidate = snapshots_[(head_ + index) % snapshot_count];
        if (candidate.epoch == epoch) {
            return &candidate;
        }
    }
    stale = true;
    return &current();
}

bool CoNodeRuntime::apply_mapping_write(std::uint16_t index, std::uint8_t subindex,
                                        std::uint32_t value) noexcept {
    const bool tpdo = index >= 0x1A00U && index <= 0x1BFFU;
    const bool rpdo = index >= 0x1600U && index <= 0x17FFU;
    if ((!tpdo && !rpdo) || subindex == 0U || subindex > 8U) {
        return false;
    }
    const std::uint8_t pdo = static_cast<std::uint8_t>((index - (tpdo ? 0x1A00U : 0x1600U)) + 1U);
    if (pdo > 4U) {
        return false;
    }
    PdoSnapshot next = current();
    next.epoch = static_cast<std::uint8_t>(next.epoch + 1U);
    set_mapping(next, tpdo, pdo, subindex, value);
    allocate(next);
    return true;
}

bool CoNodeRuntime::parse_cob_id(std::uint32_t value, std::uint32_t& resolved,
                                 bool& disabled) noexcept {
    disabled = (value & 0x80000000U) != 0U;
    resolved = value & 0x1FFFFFFFU;
    return disabled || resolved <= 0x7FFU;
}

void CoNodeRuntime::update_router(CobIdRouter* router, bool transmit, std::uint8_t pdo,
                                  std::uint32_t old_cob, std::uint32_t new_cob,
                                  bool disabled) noexcept {
    if (router == nullptr) {
        return;
    }
    if (old_cob != 0U) {
        router->erase_route(channel_id_, static_cast<std::uint16_t>(old_cob));
    }
    if (disabled) {
        return;
    }
    CobIdRoute route{};
    route.slot = slot_;
    route.node_id = node_id_;
    route.service = transmit ? CoService::Tpdo : CoService::Rpdo;
    route.pdo = pdo;
    route.custom = true;
    router->set_route(channel_id_, static_cast<std::uint16_t>(new_cob), route);
}

bool CoNodeRuntime::apply_cob_write(std::uint16_t index, std::uint8_t subindex, std::uint32_t value,
                                    CobIdRouter* router) {
    const bool tpdo = index >= 0x1800U && index <= 0x19FFU;
    const bool rpdo = index >= 0x1400U && index <= 0x15FFU;
    if ((!tpdo && !rpdo) || subindex != 1U) {
        return false;
    }
    const std::uint8_t pdo = static_cast<std::uint8_t>((index - (tpdo ? 0x1800U : 0x1400U)) + 1U);
    if (pdo > 4U) {
        return false;
    }
    std::uint32_t resolved = 0U;
    bool disabled = false;
    if (!parse_cob_id(value, resolved, disabled)) {
        return false;
    }
    PdoSnapshot next = current();
    next.epoch = static_cast<std::uint8_t>(next.epoch + 1U);
    const std::uint32_t old_cob = tpdo ? next.tpdo_cob[pdo - 1U] : next.rpdo_cob[pdo - 1U];
    const std::uint32_t new_cob = disabled ? 0U : resolved;
    if (tpdo) {
        next.tpdo_cob[pdo - 1U] = new_cob;
    } else {
        next.rpdo_cob[pdo - 1U] = new_cob;
    }
    update_router(router, tpdo, pdo, old_cob, new_cob, disabled);
    allocate(next);
    return true;
}

bool CoNodeRuntime::apply_write(std::uint16_t index, std::uint8_t subindex,
                                const std::uint8_t* data, std::size_t size, CobIdRouter* router) {
    if (!initialized_ || data == nullptr || size < 4U) {
        return false;
    }
    const std::uint32_t value = read_le32(data);
    if ((index >= 0x1A00U && index <= 0x1BFFU) || (index >= 0x1600U && index <= 0x17FFU)) {
        return apply_mapping_write(index, subindex, value);
    }
    return apply_cob_write(index, subindex, value, router);
}

} // namespace nexigen::canview
