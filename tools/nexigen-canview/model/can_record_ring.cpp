#include "can_record_ring.hpp"

#include <bit>
#include <cstring>
#include <format>

#include <sys/mman.h>

#include "budget.hpp"

namespace nexigen::canview {

namespace {

constexpr std::size_t align_up(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

// The record model is frozen at 126 bytes per slot; a layout change must be a
// deliberate contract change, not a silent memory regression.
static_assert(sizeof(nexigen::hw::can::CanMessage) == 112U,
              "CanMessage must stay 112 bytes for the 126-byte slot layout");

} // namespace

void* CanRecordRing::default_map(std::size_t bytes) noexcept {
    void* const address = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return address == MAP_FAILED ? nullptr : address;
}

CanRecordRing::Layout CanRecordRing::layout_for(std::size_t capacity) noexcept {
    Layout layout{};
    layout.messages = 0;
    layout.deltas =
        align_up(layout.messages + capacity * sizeof(nexigen::hw::can::CanMessage), 64U);
    layout.nodes = align_up(layout.deltas + capacity * sizeof(std::int64_t), 64U);
    layout.metas = align_up(layout.nodes + capacity * sizeof(std::uint16_t), 64U);
    layout.epochs = align_up(layout.metas + capacity * sizeof(std::uint8_t), 64U);
    layout.map_epochs = align_up(layout.epochs + capacity * sizeof(std::uint16_t), 64U);
    layout.total = align_up(layout.map_epochs + capacity * sizeof(std::uint8_t), 64U);
    return layout;
}

nexigen::core::Result<CanRecordRing> CanRecordRing::create(std::size_t capacity,
                                                           MapFunction map_function) {
    if (capacity == 0U || (capacity & (capacity - 1U)) != 0U) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    const Layout layout = layout_for(capacity);
    void* const mapping = map_function(layout.total);
    if (mapping == nullptr) {
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    CanRecordRing ring;
    ring.mapping_ = mapping;
    ring.mapped_bytes_ = layout.total;
    ring.capacity_ = capacity;
    auto* const base = static_cast<unsigned char*>(mapping);
    ring.messages_ = reinterpret_cast<nexigen::hw::can::CanMessage*>(base + layout.messages);
    ring.deltas_ = reinterpret_cast<std::int64_t*>(base + layout.deltas);
    ring.nodes_ = reinterpret_cast<std::uint16_t*>(base + layout.nodes);
    ring.metas_ = base + layout.metas;
    ring.epochs_ = reinterpret_cast<std::uint16_t*>(base + layout.epochs);
    ring.map_epochs_ = base + layout.map_epochs;
    return ring;
}

nexigen::core::Result<CanRecordRing> CanRecordRing::create_for_budget(std::uint64_t max_memory_mb,
                                                                      MapFunction map_function) {
    return create(capacity_for_budget(max_memory_mb), map_function);
}

CanRecordRing::CanRecordRing(CanRecordRing&& other) noexcept
    : mapping_(other.mapping_), mapped_bytes_(other.mapped_bytes_), capacity_(other.capacity_),
      head_(other.head_), tail_(other.tail_), evictions_(other.evictions_),
      messages_(other.messages_), deltas_(other.deltas_), nodes_(other.nodes_),
      metas_(other.metas_), epochs_(other.epochs_), map_epochs_(other.map_epochs_) {
    other.mapping_ = nullptr;
    other.mapped_bytes_ = 0;
    other.capacity_ = 0;
    other.messages_ = nullptr;
    other.deltas_ = nullptr;
    other.nodes_ = nullptr;
    other.metas_ = nullptr;
    other.epochs_ = nullptr;
    other.map_epochs_ = nullptr;
}

CanRecordRing& CanRecordRing::operator=(CanRecordRing&& other) noexcept {
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
    messages_ = other.messages_;
    deltas_ = other.deltas_;
    nodes_ = other.nodes_;
    metas_ = other.metas_;
    epochs_ = other.epochs_;
    map_epochs_ = other.map_epochs_;
    other.mapping_ = nullptr;
    other.mapped_bytes_ = 0;
    other.capacity_ = 0;
    other.messages_ = nullptr;
    other.deltas_ = nullptr;
    other.nodes_ = nullptr;
    other.metas_ = nullptr;
    other.epochs_ = nullptr;
    other.map_epochs_ = nullptr;
    return *this;
}

CanRecordRing::~CanRecordRing() {
    if (mapping_ != nullptr) {
        static_cast<void>(::munmap(mapping_, mapped_bytes_));
    }
}

void CanRecordRing::evict_one() noexcept {
    if (head_ >= tail_) {
        return;
    }
    ++head_;
    ++evictions_;
}

std::uint64_t CanRecordRing::push(const nexigen::hw::can::CanMessage& message,
                                  std::int64_t delta_same_id_ns, std::uint16_t co_node_index,
                                  std::uint8_t meta, std::uint16_t time_epoch,
                                  std::uint8_t co_map_epoch) noexcept {
    if (tail_ - head_ == capacity_) {
        evict_one();
    }
    const std::uint64_t seq = tail_;
    const std::size_t index = seq & (capacity_ - 1U);
    messages_[index] = message;
    deltas_[index] = delta_same_id_ns;
    nodes_[index] = co_node_index;
    metas_[index] = meta;
    epochs_[index] = time_epoch;
    map_epochs_[index] = co_map_epoch;
    ++tail_;
    return seq;
}

bool CanRecordRing::set_co_node_index(std::uint64_t seq, std::uint16_t co_node_index) noexcept {
    if (!resident(seq)) {
        return false;
    }
    nodes_[seq & (capacity_ - 1U)] = co_node_index;
    return true;
}

std::optional<CanRecordView> CanRecordRing::find(std::uint64_t seq) const noexcept {
    if (!resident(seq)) {
        return std::nullopt;
    }
    const std::size_t index = seq & (capacity_ - 1U);
    CanRecordView view{};
    view.seq = seq;
    view.message = &messages_[index];
    view.delta_same_id_ns = deltas_[index];
    view.co_node_index = nodes_[index];
    view.meta = metas_[index];
    view.time_epoch = epochs_[index];
    view.co_map_epoch = map_epochs_[index];
    return view;
}

void CanRecordRing::clear() noexcept {
    head_ = 0;
    tail_ = 0;
    evictions_ = 0;
}

std::string CanRecordRing::to_string() const {
    return std::format("CanRecordRing(capacity={}, size={}, head={}, tail={}, evictions={}, "
                       "bytes={})",
                       capacity_, size(), head_, tail_, evictions_, mapped_bytes_);
}

} // namespace nexigen::canview
