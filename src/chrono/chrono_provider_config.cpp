// nexigen::chrono — cold-path provider config serialization (change
// add-chrono-clock-service; spec chrono/chrono-provider).
#include <chrono>
#include <cstdint>
#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/chrono_provider_config.hpp>

namespace nexigen::chrono {

std::string ChronoProviderConfig::to_string() const {
    return std::format("ChronoProviderConfig(collect_interval_ns={}, "
                       "worker_thread={})",
                       collect_interval.count(), worker_thread.to_string());
}

std::string to_string(const ChronoProviderConfig& config) {
    return config.to_string();
}

void to_json(nlohmann::json& j, const ChronoProviderConfig& config) {
    j = nlohmann::json{
        {"collect_interval_ns", config.collect_interval.count()},
        {"worker_thread", config.worker_thread},
    };
}

void from_json(const nlohmann::json& j, ChronoProviderConfig& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto interval_it = j.find("collect_interval_ns");
    if (interval_it != j.end() && interval_it->is_number_integer()) {
        const std::int64_t interval = interval_it->get<std::int64_t>();
        if (interval >= 0) {
            out.collect_interval = std::chrono::nanoseconds{interval};
        }
    }
    const auto mask_it = j.find("core_mask");
    if (mask_it != j.end() && mask_it->is_number_unsigned()) {
        out.worker_thread.set_cpus(core::CpuSet::from_core_mask(mask_it->get<std::uint64_t>()));
    }
    const auto worker_it = j.find("worker_thread");
    if (worker_it != j.end()) {
        static_cast<void>(core::parse_thread_spec_json(*worker_it, out.worker_thread));
    }
}

} // namespace nexigen::chrono
