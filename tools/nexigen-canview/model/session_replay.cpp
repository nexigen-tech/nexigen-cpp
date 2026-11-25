#include "session_replay.hpp"

#include <format>

#include "record_meta.hpp"

namespace nexigen::canview {

void OfflineCanopen::build(const ImportResult& result, EdsLibrary* library) {
    registry_.clear();
    warnings_.clear();
    hash_mismatches_.clear();
    for (const SessionEdsBinding& binding : result.header.eds) {
        std::shared_ptr<const EdsDictionary> dictionary{};
        if (library != nullptr && !binding.path.empty()) {
            const EdsLoadResult loaded = library->load(binding.path);
            dictionary = loaded.dictionary;
            if (dictionary == nullptr) {
                warnings_.push_back("EDS unavailable: " + binding.path);
            } else if (binding.hash != 0U && dictionary->content_hash() != binding.hash) {
                warnings_.push_back("EDS hash mismatch: " + binding.path);
                hash_mismatches_.emplace_back(binding.channel_id, binding.node_id);
            }
        }
        const auto bound = registry_.bind(binding.channel_id, binding.node_id, binding.alias,
                                          binding.colour, dictionary, binding.path, true);
        if (!bound.has_value()) {
            warnings_.push_back("binding rejected for node " + std::to_string(binding.node_id));
        }
    }
    router_.rebuild(registry_);
    decoder_ = std::make_unique<CanOpenDecoder>(registry_, router_);
}

OfflineDecoded OfflineCanopen::decode(const SessionRecord& record) const {
    OfflineDecoded out{};
    if (record.type != CanViewRecordType::Frame) {
        return out;
    }
    if (decoder_ != nullptr) {
        CanRecordView view{};
        view.seq = record.frame.seq;
        view.message = &record.frame.message;
        view.meta = pack_frame_meta(CanRecordKind::Frame);
        out.decoded = decoder_->decode(view, 0U);
        if (out.decoded.status != CoDecodeStatus::Unavailable &&
            out.decoded.status != CoDecodeStatus::Invalid) {
            out.source = OfflineDecodeSource::Rebuilt;
            for (const auto& mismatch : hash_mismatches_) {
                if (mismatch.first == record.frame.message.channel_id &&
                    mismatch.second == out.decoded.node_id) {
                    out.hash_mismatch = true;
                    break;
                }
            }
            return out;
        }
    }
    if (!record.frame.co_decoded.empty()) {
        out.decoded.status = CoDecodeStatus::Ok;
        out.decoded.set_text(record.frame.co_decoded);
        out.source = OfflineDecodeSource::Exported;
    }
    return out;
}

ReplayReport replay_into_ring(const ImportResult& result, CanRecordRing& ring) {
    ReplayReport report{};
    for (const SessionRecord& record : result.records) {
        if (record.type == CanViewRecordType::Frame) {
            nexigen::hw::can::CanMessage message = record.frame.message;
            if (message.hardware_timestamp_ns == 0U) {
                message.hardware_timestamp_ns = record.frame.hw_ts_ns;
            }
            const std::uint8_t meta = pack_frame_meta(CanRecordKind::Frame);
            static_cast<void>(
                ring.push(message, record.frame.delta_id_ns, CanRecordView::no_node, meta, 0U, 0U));
            ++report.frames;
        } else if (record.type == CanViewRecordType::Gap) {
            nexigen::hw::can::CanMessage marker{};
            marker.channel_id = record.gap.channel_id;
            marker.hardware_timestamp_ns = record.gap.hw_ts_ns;
            const std::uint8_t meta =
                pack_gap_meta(record.gap.kind, record.gap.reason, record.gap.count_exact);
            static_cast<void>(ring.push(marker, static_cast<std::int64_t>(record.gap.lost),
                                        CanRecordView::no_node, meta, record.gap.time_epoch, 0U));
            ++report.gaps;
        } else if (record.type == CanViewRecordType::Status) {
            ++report.statuses;
        }
    }
    return report;
}

} // namespace nexigen::canview
