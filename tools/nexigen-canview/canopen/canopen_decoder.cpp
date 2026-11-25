#include "canopen_decoder.hpp"

#include <cstdio>

#include "emcy_decoder.hpp"
#include "nmt_decoder.hpp"
#include "pdo_decoder.hpp"

namespace nexigen::canview {

CanOpenDecoder::CanOpenDecoder(CanOpenRegistry& registry, CobIdRouter& router)
    : registry_(&registry), router_(&router) {
    sync();
}

void CanOpenDecoder::sync() {
    for (std::uint16_t slot = 0; slot < CanOpenRegistry::capacity; ++slot) {
        const NodeBinding* binding = registry_->at(slot);
        if (binding == nullptr || binding->dictionary == nullptr) {
            runtimes_[slot].reset();
            continue;
        }
        if (runtimes_[slot].has_value() &&
            runtimes_[slot]->dictionary_hash() == binding->eds_hash) {
            continue;
        }
        runtimes_[slot].emplace();
        runtimes_[slot]->init(*binding->dictionary, binding->channel_id, binding->node_id, slot);
    }
}

CoNodeRuntime* CanOpenDecoder::ensure_runtime(std::uint16_t slot) {
    if (slot >= CanOpenRegistry::capacity) {
        return nullptr;
    }
    const NodeBinding* binding = registry_->at(slot);
    if (binding == nullptr || binding->dictionary == nullptr) {
        return nullptr;
    }
    if (!runtimes_[slot].has_value() || runtimes_[slot]->dictionary_hash() != binding->eds_hash) {
        runtimes_[slot].emplace();
        runtimes_[slot]->init(*binding->dictionary, binding->channel_id, binding->node_id, slot);
    }
    return &*runtimes_[slot];
}

const CoNodeRuntime* CanOpenDecoder::runtime(std::uint16_t slot) const noexcept {
    if (slot >= CanOpenRegistry::capacity || !runtimes_[slot].has_value()) {
        return nullptr;
    }
    return &*runtimes_[slot];
}

std::uint32_t CanOpenDecoder::colour(const CoDecoded& decoded) const noexcept {
    const NodeBinding* binding =
        decoded.slot < CanOpenRegistry::capacity ? registry_->at(decoded.slot) : nullptr;
    return co_colour(decoded, binding, colour_mode_);
}

void CanOpenDecoder::decode_sdo(const CanRecordView& record, const CobIdRoute& route,
                                CoDecoded& out, std::uint64_t now_ns) {
    const nexigen::hw::can::CanMessage& message = *record.message;
    const NodeBinding* binding = registry_ != nullptr ? registry_->at(route.slot) : nullptr;
    static_cast<void>(sdo_.feed(message, route.service == CoService::SdoResponse, out, now_ns,
                                binding != nullptr ? binding->dictionary.get() : nullptr));
    for (;;) {
        auto write = sdo_.take_completed();
        if (!write.has_value()) {
            break;
        }
        CoNodeRuntime* node = ensure_runtime(route.slot);
        if (node != nullptr) {
            static_cast<void>(node->apply_write(write->index, write->subindex, write->data.data(),
                                                write->size, router_));
        }
    }
}

void CanOpenDecoder::decode_pdo_record(const CanRecordView& record, const CobIdRoute& route,
                                       CoDecoded& out) {
    CoNodeRuntime* node = ensure_runtime(route.slot);
    if (node == nullptr || route.pdo == 0U || route.pdo > 4U) {
        out.status = CoDecodeStatus::Unavailable;
        out.set_text("PDO without binding");
        return;
    }
    bool stale = false;
    const PdoSnapshot* snapshot = node->snapshot(record.co_map_epoch, stale);
    if (snapshot == nullptr) {
        out.status = CoDecodeStatus::Unavailable;
        out.set_text("PDO without mapping");
        return;
    }
    const EdsPdoMapping& mapping = route.service == CoService::Tpdo
                                       ? snapshot->tpdo[route.pdo - 1U]
                                       : snapshot->rpdo[route.pdo - 1U];
    const NodeBinding* binding = registry_->at(route.slot);
    static_cast<void>(decode_pdo(*record.message, mapping,
                                 binding != nullptr ? binding->dictionary.get() : nullptr, out));
    out.mapping_stale = stale;
    if (stale && out.status == CoDecodeStatus::Ok) {
        out.status = CoDecodeStatus::StaleMapping;
    }
    char text[96]{};
    std::snprintf(text, sizeof(text), "%s%u node %u epoch %u",
                  route.service == CoService::Tpdo ? "TPDO" : "RPDO", route.pdo, route.node_id,
                  record.co_map_epoch);
    out.set_text(text);
}

CoDecoded CanOpenDecoder::decode(const CanRecordView& record, std::uint64_t now_ns) {
    CoDecoded out{};
    if (record.message == nullptr || !record.is_frame()) {
        return out;
    }
    const nexigen::hw::can::CanMessage& message = *record.message;
    const auto route = router_->lookup(message.channel_id, message.can_id, message.extended);
    if (!route.has_value()) {
        out.status = CoDecodeStatus::Unavailable;
        out.set_text("no CANopen route");
        return out;
    }
    out.service = route->service;
    out.slot = route->slot;
    out.node_id = route->node_id;
    out.map_epoch = record.co_map_epoch;

    switch (route->service) {
        case CoService::Nmt: static_cast<void>(decode_nmt(message, *registry_, out)); break;
        case CoService::Heartbeat:
            static_cast<void>(decode_heartbeat(message, *registry_, out, now_ns));
            break;
        case CoService::Emcy: static_cast<void>(decode_emcy(message, out)); break;
        case CoService::Sync:
        case CoService::Time:
            out.status = CoDecodeStatus::Ok;
            out.set_text(route->service == CoService::Sync ? "SYNC" : "TIME");
            break;
        case CoService::SdoRequest:
        case CoService::SdoResponse: decode_sdo(record, *route, out, now_ns); break;
        case CoService::Tpdo:
        case CoService::Rpdo:        decode_pdo_record(record, *route, out); break;
        case CoService::Unknown:     out.status = CoDecodeStatus::Unavailable; break;
    }
    return out;
}

} // namespace nexigen::canview
