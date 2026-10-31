// nexigen::chrono — cold-path snapshot serialization (change
// add-chrono-clock-service; spec chrono/chrono-snapshot).
//
// Bodies for the to_string / to_json / from_json contract declared in
// include/nexigen/chrono/chrono_snapshot.hpp. Everything here allocates
// (std::string, nlohmann::json) and is diagnostics-only: the snapshot
// sampling and delivery paths never call into this unit.
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/chrono_snapshot.hpp>
#include <nexigen/chrono/timestamp.hpp>

namespace nexigen::chrono {

std::string Chrono_snapshot::to_string() const {
    return "Chrono_snapshot(stamp_ns=" + std::to_string(stamp.nanoseconds()) +
           ", realtime_offset_ns=" + std::to_string(realtime_offset_ns) + ")";
}

std::string to_string(const Chrono_snapshot& snapshot) { return snapshot.to_string(); }

void to_json(nlohmann::json& j, const Chrono_snapshot& snapshot) {
    j = nlohmann::json{
        {"stamp_ns", snapshot.stamp.nanoseconds()},
        {"realtime_offset_ns", snapshot.realtime_offset_ns},
    };
}

void from_json(const nlohmann::json& j, Chrono_snapshot& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto stamp_it = j.find("stamp_ns");
    if (stamp_it != j.end() && stamp_it->is_number_unsigned()) {
        out.stamp = chrono::Timestamp{stamp_it->get<std::uint64_t>()};
    }
    const auto offset_it = j.find("realtime_offset_ns");
    if (offset_it != j.end() && offset_it->is_number_integer()) {
        out.realtime_offset_ns = offset_it->get<std::int64_t>();
    }
}

} // namespace nexigen::chrono
