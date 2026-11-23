#include "app_registry.hpp"

#include <cstring>

#include <format>

namespace nexigen::logview {

namespace {
const AppSlot kEmptySlot{};
} // namespace

std::string AppSlot::to_string() const {
    return std::format("AppSlot(id={}, records={}, last_ns={}, color={})", text(), record_count,
                       last_stamp_ns, color_index);
}

void AppRegistry::clear() noexcept {
    slots_.fill(AppSlot{});
    used_ = 0;
    next_color_ = 0;
}

std::uint64_t AppRegistry::hash_of(std::string_view app) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    const std::size_t n = app.size() < 16U ? app.size() : 16U;
    for (std::size_t i = 0; i < n; ++i) {
        hash ^= static_cast<unsigned char>(app[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::size_t AppRegistry::find(std::string_view app) const noexcept {
    const std::uint64_t hash = hash_of(app);
    std::size_t probe = static_cast<std::size_t>(hash) & (capacity - 1U);
    for (std::size_t step = 0; step < capacity; ++step) {
        const AppSlot& slot = slots_[probe];
        if (!slot.used) {
            return capacity;
        }
        if (slot.hash == hash && slot.text() == app.substr(0, 16)) {
            return probe;
        }
        probe = (probe + 1U) & (capacity - 1U);
    }
    return capacity;
}

std::size_t AppRegistry::observe(std::string_view app, std::uint64_t stamp_ns) noexcept {
    const std::uint64_t hash = hash_of(app);
    std::size_t probe = static_cast<std::size_t>(hash) & (capacity - 1U);
    for (std::size_t step = 0; step < capacity; ++step) {
        AppSlot& slot = slots_[probe];
        if (!slot.used) {
            slot.used = true;
            slot.hash = hash;
            slot.record_count = 1;
            slot.last_stamp_ns = stamp_ns;
            slot.color_index = next_color_;
            next_color_ = static_cast<std::uint16_t>((next_color_ + 1U) % 256U);
            const std::size_t n = app.size() < 16U ? app.size() : 16U;
            std::memcpy(slot.id.data(), app.data(), n);
            ++used_;
            return probe;
        }
        if (slot.hash == hash && slot.text() == app.substr(0, 16)) {
            ++slot.record_count;
            slot.last_stamp_ns = stamp_ns;
            return probe;
        }
        probe = (probe + 1U) & (capacity - 1U);
    }
    return capacity;
}

void AppRegistry::on_evict(std::size_t slot) noexcept {
    if (slot >= capacity) {
        return;
    }
    if (slots_[slot].record_count > 0U) {
        --slots_[slot].record_count;
    }
}

const AppSlot& AppRegistry::at(std::size_t slot) const noexcept {
    return slot < capacity ? slots_[slot] : kEmptySlot;
}

std::uint16_t AppRegistry::color_index(std::size_t slot) const noexcept {
    return slot < capacity ? slots_[slot].color_index : 0U;
}

std::string AppRegistry::to_string() const {
    return std::format("AppRegistry(size={}, capacity={})", used_, capacity);
}

std::string to_string(const AppRegistry& registry) {
    return registry.to_string();
}

} // namespace nexigen::logview
