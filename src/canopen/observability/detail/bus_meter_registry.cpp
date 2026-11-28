// nexigen::canopen::observability::detail — bus name reservation (change
// add-canopen-observability; design D2).
#include <nexigen/canopen/observability/detail/bus_meter_registry.hpp>

#include <format>
#include <string>
#include <string_view>

#include <nexigen/metrics/metric_identity.hpp>

namespace nexigen::canopen::observability::detail {

namespace {

/// @brief Prefix shared by every CANopen bus meter name.
constexpr std::string_view meter_prefix = "canopen.";

/// @brief Whether a bus name fits the documented range.
///
/// @param bus Candidate name.
/// @return `true` when non-empty and at most `max_bus_name_length` characters.
[[nodiscard]] bool valid_bus_name(std::string_view bus) noexcept {
    return !bus.empty() && bus.size() <= BusMeterRegistry::max_bus_name_length;
}

/// @brief Whether a meter version fits the provider's name limit.
///
/// @param version Candidate version.
/// @return `true` when non-empty and at most `metrics::max_name_length`.
[[nodiscard]] bool valid_version(std::string_view version) noexcept {
    return !version.empty() && version.size() <= metrics::max_name_length;
}

} // namespace

core::Result<BusMeterIdentity> BusMeterRegistry::reserve(std::string_view bus,
                                                         std::string_view version) {
    if (!valid_bus_name(bus) || !valid_version(version)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    for (const std::string& reserved : reserved_) {
        if (std::string_view(reserved) == bus) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
    }
    for (std::string& slot : reserved_) {
        if (!slot.empty()) {
            continue;
        }
        slot.assign(bus);
        BusMeterIdentity identity{};
        identity.name = std::string(meter_prefix) + slot;
        identity.version = std::string(version);
        return identity;
    }
    return std::unexpected(core::ErrorCode::ResourceExhausted);
}

bool BusMeterRegistry::release(std::string_view bus) noexcept {
    for (std::string& slot : reserved_) {
        if (std::string_view(slot) == bus) {
            slot.clear();
            return true;
        }
    }
    return false;
}

std::size_t BusMeterRegistry::reserved_count() const noexcept {
    std::size_t count = 0U;
    for (const std::string& slot : reserved_) {
        if (!slot.empty()) {
            ++count;
        }
    }
    return count;
}

std::string BusMeterRegistry::to_string() const {
    std::string text = std::format("BusMeterRegistry(count={}, buses=[", reserved_count());
    bool first = true;
    for (const std::string& slot : reserved_) {
        if (slot.empty()) {
            continue;
        }
        if (!first) {
            text += ", ";
        }
        text += std::string(meter_prefix) + slot;
        first = false;
    }
    text += "])";
    return text;
}

} // namespace nexigen::canopen::observability::detail
