// nexigen::canopen::observability::detail — bus meter identity rendering
// (change add-canopen-observability; design D2).
#include <nexigen/canopen/observability/detail/bus_meter_identity.hpp>

#include <format>
#include <string>

namespace nexigen::canopen::observability::detail {

std::string BusMeterIdentity::to_string() const {
    return std::format("BusMeterIdentity(name={}, version={})", name, version);
}

} // namespace nexigen::canopen::observability::detail
