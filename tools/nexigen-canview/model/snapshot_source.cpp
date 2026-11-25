#include "snapshot_source.hpp"

#include "can_ingress_record.hpp"

#include <algorithm>

namespace nexigen::canview {

SnapshotSource::~SnapshotSource() = default;

void SnapshotSource::build(const RecordSource& source, std::uint64_t max_bytes) {
    records_.clear();
    const std::size_t slot_bytes = sizeof(CanIngressRecord);
    const std::size_t max_records = slot_bytes == 0U ? 0U : max_bytes / slot_bytes;
    if (max_records == 0U) {
        return;
    }
    records_.reserve(std::min<std::size_t>(max_records, source.size()));
    for (std::uint64_t seq = source.tail(); seq > source.head() && records_.size() < max_records;) {
        --seq;
        const auto view = source.find(seq);
        if (!view.has_value() || view->message == nullptr) {
            continue;
        }
        SnapshotRecord record{};
        record.seq = seq;
        record.message = *view->message;
        record.delta_same_id_ns = view->delta_same_id_ns;
        record.co_node_index = view->co_node_index;
        record.meta = view->meta;
        record.time_epoch = view->time_epoch;
        record.co_map_epoch = view->co_map_epoch;
        records_.push_back(record);
    }
    std::reverse(records_.begin(), records_.end());
}

std::optional<CanRecordView> SnapshotSource::find(std::uint64_t seq) const noexcept {
    const auto it = std::lower_bound(
        records_.begin(), records_.end(), seq,
        [](const SnapshotRecord& record, std::uint64_t value) { return record.seq < value; });
    if (it == records_.end() || it->seq != seq) {
        return std::nullopt;
    }
    CanRecordView view{};
    view.seq = it->seq;
    view.message = &it->message;
    view.delta_same_id_ns = it->delta_same_id_ns;
    view.co_node_index = it->co_node_index;
    view.meta = it->meta;
    view.time_epoch = it->time_epoch;
    view.co_map_epoch = it->co_map_epoch;
    return view;
}

std::uint64_t SnapshotSource::head() const noexcept {
    return records_.empty() ? 0U : records_.front().seq;
}

std::uint64_t SnapshotSource::tail() const noexcept {
    return records_.empty() ? 0U : records_.back().seq + 1U;
}

} // namespace nexigen::canview
