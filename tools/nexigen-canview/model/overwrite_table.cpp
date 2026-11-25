#include "overwrite_table.hpp"

#include <algorithm>
#include <bit>
#include <format>
#include <functional>
#include <limits>
#include <utility>

namespace nexigen::canview {

namespace {

namespace hwcan = nexigen::hw::can;

constexpr std::uint64_t rate_bucket_ns{100'000'000ULL};

bool is_error_kind(hwcan::CanMessageKind kind) noexcept {
    return kind == hwcan::CanMessageKind::Error || kind == hwcan::CanMessageKind::Overload;
}

void observe_rate(OverwriteRow& row, std::uint64_t now_ns) noexcept {
    const auto epoch = static_cast<std::int64_t>(now_ns / rate_bucket_ns);
    for (std::size_t index = 0; index < OverwriteRow::rate_buckets; ++index) {
        if (row.rate_epoch[index] < epoch - 9) {
            row.rate_epoch[index] = -1;
            row.rate_count[index] = 0U;
        }
    }
    const std::size_t slot = static_cast<std::size_t>(epoch) % OverwriteRow::rate_buckets;
    if (row.rate_epoch[slot] != epoch) {
        row.rate_epoch[slot] = epoch;
        row.rate_count[slot] = 0U;
    }
    ++row.rate_count[slot];
}

void observe_delta(OverwriteRow& row, const hwcan::CanMessage& message,
                   std::int64_t anomaly_threshold_ns) noexcept {
    if (!row.has_previous) {
        row.has_previous = true;
        row.delta_cur = 0;
        return;
    }
    const auto current = static_cast<std::int64_t>(message.hardware_timestamp_ns);
    const auto previous = static_cast<std::int64_t>(row.previous_timestamp);
    const std::int64_t delta = current - previous;
    row.delta_cur = delta;
    if (row.skip_next_delta) {
        row.skip_next_delta = false;
        row.anomalous = true;
        return;
    }
    if (delta < 0) {
        return; // negative periods stay in Cur but never pollute the window
    }
    if (row.delta_count < OverwriteRow::delta_window) {
        row.deltas[(row.delta_head + row.delta_count) % OverwriteRow::delta_window] = delta;
        ++row.delta_count;
    } else {
        row.deltas[row.delta_head] = delta;
        row.delta_head = (row.delta_head + 1U) % OverwriteRow::delta_window;
    }
    row.anomalous = anomaly_threshold_ns > 0 && delta > anomaly_threshold_ns;
}

void observe_bytes(OverwriteRow& row, const hwcan::CanMessage& message,
                   std::uint64_t now_ns) noexcept {
    const std::uint8_t dlc = message.dlc > 64U ? 64U : message.dlc;
    for (std::size_t index = 0; index < 64U; ++index) {
        if (index >= dlc) {
            row.byte_changed[index] = false;
            row.byte_at[index] = 0U;
            row.previous_data[index] = 0U;
            continue;
        }
        const bool changed = !row.error_row && (!row.has_payload || index >= row.previous_dlc ||
                                                message.data[index] != row.previous_data[index]);
        row.byte_changed[index] = changed;
        if (changed) {
            row.byte_at[index] = now_ns;
        }
        row.previous_data[index] = message.data[index];
    }
    row.previous_dlc = dlc;
    row.has_payload = true;
}

} // namespace

void OverwriteRow::observe(const hwcan::CanMessage& message, std::uint64_t now_ns,
                           std::int64_t anomaly_threshold_ns) noexcept {
    ++count;
    first_ns = count == 1U ? message.hardware_timestamp_ns : first_ns;
    last_ns = message.hardware_timestamp_ns;
    error_row = is_error_kind(message.kind);
    observe_rate(*this, now_ns);
    if (error_row) {
        return;
    }
    observe_delta(*this, message, anomaly_threshold_ns);
    observe_bytes(*this, message, now_ns);
    previous_timestamp = message.hardware_timestamp_ns;
}

void OverwriteRow::reset_period() noexcept {
    delta_head = 0;
    delta_count = 0;
    delta_cur = 0;
    has_previous = false;
    skip_next_delta = true;
    anomalous = true;
}

bool OverwriteRow::byte_fading(std::size_t index, std::uint64_t now_ns,
                               std::uint64_t fade_duration_ns) const noexcept {
    if (index >= byte_changed.size() || !byte_changed[index]) {
        return false;
    }
    return now_ns >= byte_at[index] && (now_ns - byte_at[index]) < fade_duration_ns;
}

OverwriteStats OverwriteRow::stats(std::uint64_t now_ns) const noexcept {
    OverwriteStats out{};
    out.count = count;
    out.error_row = error_row;
    out.delta_cur = delta_cur;
    out.anomalous = anomalous && !error_row;
    const auto epoch = static_cast<std::int64_t>(now_ns / rate_bucket_ns);
    for (std::size_t index = 0; index < rate_buckets; ++index) {
        if (rate_epoch[index] >= epoch - 9 && rate_epoch[index] <= epoch) {
            out.msg_per_s += rate_count[index];
        }
    }
    if (error_row || delta_count == 0U) {
        return out;
    }
    std::int64_t minimum = std::numeric_limits<std::int64_t>::max();
    std::int64_t maximum = std::numeric_limits<std::int64_t>::min();
    std::int64_t sum = 0;
    for (std::size_t index = 0; index < delta_count; ++index) {
        const std::int64_t value = deltas[(delta_head + index) % delta_window];
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
        sum += value;
    }
    out.has_delta = true;
    out.delta_min = minimum;
    out.delta_max = maximum;
    out.delta_avg = sum / static_cast<std::int64_t>(delta_count);
    out.jitter = maximum - minimum;
    return out;
}

void OverwriteTable::init(std::size_t capacity) {
    capacity_ = std::max<std::size_t>(capacity, 1U);
    rows_.clear();
    rows_.reserve(capacity_);
    touch_ = 0U;
    evictions_ = 0U;
    selected_evicted_ = 0U;
    gap_pending_ = false;
    selected_.clear();
    disabled_ = false;
    resize_index();
}

void OverwriteTable::resize_index() {
    std::size_t slots = 8U;
    while (slots < capacity_ * 2U) {
        slots <<= 1U;
    }
    slots_.assign(slots, -1);
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        index_insert(index);
    }
}

std::size_t OverwriteTable::probe(const OverwriteKey& key) const noexcept {
    const std::size_t mask = slots_.size() - 1U;
    return std::hash<OverwriteKey>{}(key)&mask;
}

void OverwriteTable::index_insert(std::size_t row_index) {
    if (slots_.empty()) {
        return;
    }
    const std::size_t mask = slots_.size() - 1U;
    std::size_t slot = probe(rows_[row_index].key);
    for (std::size_t step = 0; step < slots_.size(); ++step) {
        const std::int32_t value = slots_[(slot + step) & mask];
        if (value < 0) {
            slots_[(slot + step) & mask] = static_cast<std::int32_t>(row_index);
            return;
        }
    }
}

void OverwriteTable::index_erase(const OverwriteKey& key) noexcept {
    if (slots_.empty()) {
        return;
    }
    const std::size_t mask = slots_.size() - 1U;
    std::size_t slot = probe(key);
    for (std::size_t step = 0; step < slots_.size(); ++step) {
        const std::size_t position = (slot + step) & mask;
        const std::int32_t value = slots_[position];
        if (value == -1) {
            return;
        }
        if (value >= 0 && rows_[static_cast<std::size_t>(value)].key == key) {
            slots_[position] = -2;
            return;
        }
    }
}

void OverwriteTable::drop_selected(const OverwriteKey& key) noexcept {
    const auto it = std::find(selected_.begin(), selected_.end(), key);
    if (it != selected_.end()) {
        selected_.erase(it);
        ++selected_evicted_;
    }
}

std::size_t OverwriteTable::evict_lru() {
    std::size_t victim = 0U;
    for (std::size_t index = 1U; index < rows_.size(); ++index) {
        if (rows_[index].last_touch < rows_[victim].last_touch) {
            victim = index;
        }
    }
    const OverwriteKey old_key = rows_[victim].key;
    index_erase(old_key);
    drop_selected(old_key);
    rows_[victim] = OverwriteRow{};
    ++evictions_;
    return victim;
}

OverwriteRow* OverwriteTable::update(const CanRecordView& record, std::uint64_t now_ns,
                                     std::int64_t anomaly_threshold_ns) noexcept {
    if (disabled_ || record.message == nullptr || !record.is_frame() ||
        record.message->kind == hwcan::CanMessageKind::BusIdle) {
        return nullptr;
    }
    OverwriteKey key{};
    key.channel_id = record.message->channel_id;
    key.can_id = record.message->can_id;
    key.extended = record.message->extended;
    key.kind = record.message->kind;

    const std::size_t mask = slots_.empty() ? 0U : slots_.size() - 1U;
    std::size_t row_index = rows_.size();
    if (!slots_.empty()) {
        const std::size_t start = probe(key);
        for (std::size_t step = 0; step < slots_.size(); ++step) {
            const std::int32_t value = slots_[(start + step) & mask];
            if (value == -1) {
                break;
            }
            if (value >= 0 && rows_[static_cast<std::size_t>(value)].key == key) {
                row_index = static_cast<std::size_t>(value);
                break;
            }
        }
    }
    if (row_index == rows_.size()) {
        if (rows_.size() < capacity_) {
            rows_.push_back(OverwriteRow{});
            row_index = rows_.size() - 1U;
        } else {
            row_index = evict_lru();
        }
        rows_[row_index].key = key;
        index_insert(row_index);
    }
    OverwriteRow& row = rows_[row_index];
    row.last_seq = record.seq;
    row.latest = *record.message;
    row.last_touch = ++touch_;
    row.observe(*record.message, now_ns, anomaly_threshold_ns);
    return &row;
}

const OverwriteRow* OverwriteTable::find(const OverwriteKey& key) const noexcept {
    if (slots_.empty()) {
        return nullptr;
    }
    const std::size_t mask = slots_.size() - 1U;
    const std::size_t start = probe(key);
    for (std::size_t step = 0; step < slots_.size(); ++step) {
        const std::int32_t value = slots_[(start + step) & mask];
        if (value == -1) {
            return nullptr;
        }
        if (value >= 0 && rows_[static_cast<std::size_t>(value)].key == key) {
            return &rows_[static_cast<std::size_t>(value)];
        }
    }
    return nullptr;
}

OverwriteRow* OverwriteTable::find(const OverwriteKey& key) noexcept {
    return const_cast<OverwriteRow*>(std::as_const(*this).find(key));
}

void OverwriteTable::reset_channel(std::uint16_t channel_id) noexcept {
    bool touched = false;
    for (auto& row : rows_) {
        if (row.key.channel_id == channel_id) {
            row.reset_period();
            touched = true;
        }
    }
    gap_pending_ = gap_pending_ || touched;
}

void OverwriteTable::reset_all() noexcept {
    for (auto& row : rows_) {
        row.reset_period();
    }
    gap_pending_ = true;
}

bool OverwriteTable::consume_gap() noexcept {
    const bool pending = gap_pending_;
    gap_pending_ = false;
    return pending;
}

void OverwriteTable::select_key(const OverwriteKey& key) {
    if (!is_key_selected(key)) {
        selected_.push_back(key);
    }
}

bool OverwriteTable::toggle_key(const OverwriteKey& key) {
    const auto it = std::find(selected_.begin(), selected_.end(), key);
    if (it != selected_.end()) {
        selected_.erase(it);
        return false;
    }
    selected_.push_back(key);
    return true;
}

bool OverwriteTable::is_key_selected(const OverwriteKey& key) const noexcept {
    return std::find(selected_.begin(), selected_.end(), key) != selected_.end();
}

void OverwriteTable::clear_selection() noexcept {
    selected_.clear();
}

std::string OverwriteTable::to_string() const {
    return std::format("OverwriteTable(rows={}, capacity={}, evictions={}, selected={}, gap={})",
                       rows_.size(), capacity_, evictions_, selected_.size(), gap_pending_);
}

} // namespace nexigen::canview
