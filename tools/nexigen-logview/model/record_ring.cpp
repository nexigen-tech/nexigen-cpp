#include "record_ring.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <format>
#include <limits>
#include <string_view>

#include <sys/mman.h>

#include "ingress_record.hpp"

namespace nexigen::logview {

namespace {

constexpr std::size_t align_up(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

constexpr std::uint64_t kIngressBudgetBytes = 65536ULL * sizeof(IngressRecord);
constexpr std::uint64_t kFixedReserveBytes = 32ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kApproxBytesPerRecord = 273ULL;

} // namespace

void* RecordRing::default_map(std::size_t bytes) noexcept {
    void* const address = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return address == MAP_FAILED ? nullptr : address;
}

RecordRing::Layout RecordRing::layout_for(std::size_t capacity) noexcept {
    Layout layout{};
    layout.slots = 0;
    layout.kinds = align_up(layout.slots + capacity * sizeof(nexigen::logging::LogFrame), 64U);
    layout.prev = align_up(layout.kinds + capacity * sizeof(std::uint8_t), 64U);
    layout.visible = align_up(layout.prev + capacity * sizeof(std::int64_t), 64U);
    const std::size_t bit_words = align_up(capacity, 64U) / 64U;
    layout.selected = align_up(layout.visible + capacity * sizeof(std::uint64_t), 64U);
    layout.bookmarked = align_up(layout.selected + bit_words * sizeof(std::uint64_t), 64U);
    layout.total = align_up(layout.bookmarked + bit_words * sizeof(std::uint64_t), 64U);
    return layout;
}

std::size_t RecordRing::capacity_for_budget(std::uint64_t budget_bytes) noexcept {
    const std::uint64_t available = budget_bytes > (kIngressBudgetBytes + kFixedReserveBytes)
                                        ? budget_bytes - kIngressBudgetBytes - kFixedReserveBytes
                                        : 0ULL;
    std::uint64_t capacity = available / kApproxBytesPerRecord;
    if (capacity < 1024ULL) {
        capacity = 1024ULL;
    }
    const std::uint64_t power = std::bit_floor(capacity);
    return static_cast<std::size_t>(power);
}

nexigen::core::Result<RecordRing> RecordRing::create(std::size_t capacity,
                                                     MapFunction map_function) {
    if (capacity == 0U || (capacity & (capacity - 1U)) != 0U) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    const Layout layout = layout_for(capacity);
    void* const mapping = map_function(layout.total);
    if (mapping == nullptr) {
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    RecordRing ring;
    ring.mapping_ = mapping;
    ring.mapped_bytes_ = layout.total;
    ring.capacity_ = capacity;
    auto* const base = static_cast<unsigned char*>(mapping);
    ring.slots_ = reinterpret_cast<nexigen::logging::LogFrame*>(base + layout.slots);
    ring.kinds_ = base + layout.kinds;
    ring.prev_ = reinterpret_cast<std::int64_t*>(base + layout.prev);
    ring.visible_ = reinterpret_cast<std::uint64_t*>(base + layout.visible);
    ring.selected_ = reinterpret_cast<std::uint64_t*>(base + layout.selected);
    ring.bookmarked_ = reinterpret_cast<std::uint64_t*>(base + layout.bookmarked);
    return ring;
}

nexigen::core::Result<RecordRing> RecordRing::create_for_budget(std::uint64_t budget_bytes,
                                                                MapFunction map_function) {
    return create(capacity_for_budget(budget_bytes), map_function);
}

RecordRing::RecordRing(RecordRing&& other) noexcept
    : mapping_(other.mapping_), mapped_bytes_(other.mapped_bytes_), capacity_(other.capacity_),
      head_(other.head_), tail_(other.tail_), evictions_(other.evictions_), slots_(other.slots_),
      kinds_(other.kinds_), prev_(other.prev_), visible_(other.visible_),
      selected_(other.selected_), bookmarked_(other.bookmarked_),
      visible_start_(other.visible_start_), visible_count_(other.visible_count_),
      selected_count_(other.selected_count_), apps_(other.apps_), minimap_(other.minimap_) {
    other.mapping_ = nullptr;
    other.mapped_bytes_ = 0;
    other.capacity_ = 0;
    other.slots_ = nullptr;
    other.kinds_ = nullptr;
    other.prev_ = nullptr;
    other.visible_ = nullptr;
    other.selected_ = nullptr;
    other.bookmarked_ = nullptr;
}

RecordRing& RecordRing::operator=(RecordRing&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    if (mapping_ != nullptr) {
        static_cast<void>(::munmap(mapping_, mapped_bytes_));
    }
    mapping_ = other.mapping_;
    mapped_bytes_ = other.mapped_bytes_;
    capacity_ = other.capacity_;
    head_ = other.head_;
    tail_ = other.tail_;
    evictions_ = other.evictions_;
    slots_ = other.slots_;
    kinds_ = other.kinds_;
    prev_ = other.prev_;
    visible_ = other.visible_;
    selected_ = other.selected_;
    bookmarked_ = other.bookmarked_;
    visible_start_ = other.visible_start_;
    visible_count_ = other.visible_count_;
    selected_count_ = other.selected_count_;
    apps_ = other.apps_;
    minimap_ = other.minimap_;
    other.mapping_ = nullptr;
    other.mapped_bytes_ = 0;
    other.capacity_ = 0;
    other.slots_ = nullptr;
    other.kinds_ = nullptr;
    other.prev_ = nullptr;
    other.visible_ = nullptr;
    other.selected_ = nullptr;
    other.bookmarked_ = nullptr;
    return *this;
}

RecordRing::~RecordRing() {
    if (mapping_ != nullptr) {
        static_cast<void>(::munmap(mapping_, mapped_bytes_));
    }
}

bool RecordRing::bit(const std::uint64_t* words, std::size_t index) noexcept {
    return (words[index / 64U] & (1ULL << (index % 64U))) != 0ULL;
}

void RecordRing::set_bit(std::uint64_t* words, std::size_t index, bool value) noexcept {
    const std::uint64_t mask = 1ULL << (index % 64U);
    if (value) {
        words[index / 64U] |= mask;
    } else {
        words[index / 64U] &= ~mask;
    }
}

void RecordRing::clear_slot_state(std::size_t index) noexcept {
    set_bit(selected_, index, false);
    set_bit(bookmarked_, index, false);
}

void RecordRing::evict_one() noexcept {
    if (head_ >= tail_) {
        return;
    }
    const std::size_t index = head_ & (capacity_ - 1U);
    const nexigen::logging::LogFrame& frame = slots_[index];
    const auto app_slot = apps_.find(frame.app_id_text());
    if (app_slot < AppRegistry::capacity) {
        apps_.on_evict(app_slot);
    }
    minimap_.on_evict(head_, frame.log_level, bit(bookmarked_, index));
    if (bit(selected_, index) && selected_count_ > 0U) {
        --selected_count_;
    }
    clear_slot_state(index);
    ++head_;
    ++evictions_;
}

std::uint64_t RecordRing::push_frame(nexigen::logging::LogFrame frame) noexcept {
    if (tail_ - head_ == capacity_) {
        evict_one();
    }
    const std::uint64_t seq = tail_;
    const std::size_t index = (seq & (capacity_ - 1U));
    clear_slot_state(index);
    const std::string_view app = frame.app_id_text();
    const std::size_t previous_slot = apps_.find(app);
    const bool existed = previous_slot < AppRegistry::capacity;
    const std::uint64_t previous_stamp =
        existed ? apps_.at(previous_slot).last_stamp_ns : frame.timestamp_ns;
    const std::size_t app_slot = apps_.observe(app, frame.timestamp_ns);
    const std::int64_t delta = existed ? static_cast<std::int64_t>(frame.timestamp_ns) -
                                             static_cast<std::int64_t>(previous_stamp)
                                       : 0;
    static_cast<void>(app_slot);
    slots_[index] = frame;
    kinds_[index] = static_cast<std::uint8_t>(RecordKind::Frame);
    prev_[index] = delta;
    minimap_.on_ingest(seq, frame.log_level);
    ++tail_;
    return seq;
}

std::uint64_t RecordRing::push_gap(RecordKind kind, std::uint64_t count,
                                   std::uint64_t timestamp_ns) noexcept {
    if (tail_ - head_ == capacity_) {
        evict_one();
    }
    const std::uint64_t seq = tail_;
    const std::size_t index = (seq & (capacity_ - 1U));
    clear_slot_state(index);
    nexigen::logging::LogFrame frame{};
    frame.timestamp_ns = timestamp_ns;
    frame.log_level = kind == RecordKind::IngressOverflow ? nexigen::core::LogLevel::Error
                                                          : nexigen::core::LogLevel::Warning;
    const char* label = kind == RecordKind::TransportGap      ? "transport"
                        : kind == RecordKind::IngressOverflow ? "ingress"
                                                              : "publisher";
    const int written = std::snprintf(reinterpret_cast<char*>(frame.payload), sizeof(frame.payload),
                                      "[gap] %llu record(s) lost (%s)",
                                      static_cast<unsigned long long>(count), label);
    if (written > 0) {
        const std::size_t bytes = static_cast<std::size_t>(written);
        frame.payload_size = static_cast<std::uint16_t>(
            bytes < sizeof(frame.payload) ? bytes : sizeof(frame.payload));
    }
    slots_[index] = frame;
    kinds_[index] = static_cast<std::uint8_t>(kind);
    prev_[index] = static_cast<std::int64_t>(count);
    ++tail_;
    return seq;
}

std::optional<RecordView> RecordRing::find(std::uint64_t seq) const noexcept {
    if (!resident(seq)) {
        return std::nullopt;
    }
    const std::size_t index = (seq & (capacity_ - 1U));
    const auto kind = static_cast<RecordKind>(kinds_[index]);
    RecordView view{};
    view.seq = seq;
    view.kind = kind;
    view.frame = &slots_[index];
    if (kind == RecordKind::Frame) {
        view.prev_same_app_ns = prev_[index];
    } else {
        view.gap_count = static_cast<std::uint64_t>(prev_[index]);
    }
    return view;
}

void RecordRing::append_visible(std::uint64_t seq) noexcept {
    if (visible_count_ == capacity_) {
        visible_start_ = (visible_start_ + 1U) & (capacity_ - 1U);
        --visible_count_;
    }
    visible_[(visible_start_ + visible_count_) & (capacity_ - 1U)] = seq;
    ++visible_count_;
}

std::uint64_t RecordRing::visible_at(std::size_t index) const noexcept {
    if (index >= visible_count_) {
        return 0U;
    }
    return visible_[(visible_start_ + index) & (capacity_ - 1U)];
}

void RecordRing::clear_visible() noexcept {
    visible_start_ = 0;
    visible_count_ = 0;
}

void RecordRing::clear_selection() noexcept {
    std::memset(selected_, 0, align_up(capacity_, 64U) / 8U);
    selected_count_ = 0;
}

std::size_t RecordRing::compact_visible() noexcept {
    std::size_t kept = 0;
    for (std::size_t i = 0; i < visible_count_; ++i) {
        const std::uint64_t seq = visible_[(visible_start_ + i) & (capacity_ - 1U)];
        if (resident(seq)) {
            visible_[(visible_start_ + kept) & (capacity_ - 1U)] = seq;
            ++kept;
        }
    }
    const std::size_t removed = visible_count_ - kept;
    visible_count_ = kept;
    return removed;
}

std::size_t RecordRing::stale_visible() const noexcept {
    std::size_t stale = 0;
    for (std::size_t i = 0; i < visible_count_; ++i) {
        if (!resident(visible_[(visible_start_ + i) & (capacity_ - 1U)])) {
            ++stale;
        }
    }
    return stale;
}

bool RecordRing::toggle_selected(std::uint64_t seq) noexcept {
    if (!resident(seq)) {
        return false;
    }
    const std::size_t index = (seq & (capacity_ - 1U));
    const bool next = !bit(selected_, index);
    set_bit(selected_, index, next);
    if (next) {
        ++selected_count_;
    } else if (selected_count_ > 0U) {
        --selected_count_;
    }
    return next;
}

bool RecordRing::set_selected(std::uint64_t seq, bool selected) noexcept {
    if (!resident(seq)) {
        return false;
    }
    const std::size_t index = (seq & (capacity_ - 1U));
    const bool current = bit(selected_, index);
    if (current == selected) {
        return true;
    }
    set_bit(selected_, index, selected);
    if (selected) {
        ++selected_count_;
    } else if (selected_count_ > 0U) {
        --selected_count_;
    }
    return true;
}

bool RecordRing::is_selected(std::uint64_t seq) const noexcept {
    return resident(seq) && bit(selected_, (seq & (capacity_ - 1U)));
}

bool RecordRing::toggle_bookmark(std::uint64_t seq) noexcept {
    if (!resident(seq)) {
        return false;
    }
    const std::size_t index = (seq & (capacity_ - 1U));
    const bool next = !bit(bookmarked_, index);
    set_bit(bookmarked_, index, next);
    minimap_.on_bookmark(seq, next);
    return next;
}

bool RecordRing::is_bookmarked(std::uint64_t seq) const noexcept {
    return resident(seq) && bit(bookmarked_, (seq & (capacity_ - 1U)));
}

void RecordRing::clear() noexcept {
    head_ = 0;
    tail_ = 0;
    evictions_ = 0;
    visible_start_ = 0;
    visible_count_ = 0;
    selected_count_ = 0;
    std::memset(selected_, 0, align_up(capacity_, 64U) / 8U);
    std::memset(bookmarked_, 0, align_up(capacity_, 64U) / 8U);
    apps_.clear();
    minimap_.clear();
}

std::string RecordRing::to_string() const {
    return std::format(
        "RecordRing(capacity={}, size={}, head={}, tail={}, evictions={}, visible={}, selected={}, "
        "bytes={})",
        capacity_, size(), head_, tail_, evictions_, visible_count_, selected_count_,
        mapped_bytes_);
}

std::string to_string(const RecordRing& ring) {
    return ring.to_string();
}

} // namespace nexigen::logview
