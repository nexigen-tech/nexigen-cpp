// nexigen::chrono — cold-path provider config serialization (change
// add-chrono-clock-service; spec chrono/chrono-provider; extended with `kind`
// by change add-clock-authority-core).
#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/provider_config.hpp>

namespace nexigen::chrono {

std::string ProviderConfig::to_string() const {
    return std::format("ProviderConfig(collect_interval_ns={}, worker_thread={}, kind={})",
                       collect_interval.count(), worker_thread.to_string(),
                       nexigen::chrono::to_string(kind));
}

std::string to_string(const ProviderConfig& config) {
    return config.to_string();
}

void to_json(nlohmann::json& j, const ProviderConfig& config) {
    j = nlohmann::json{
        {"collect_interval_ns", config.collect_interval.count()},
        {"worker_thread", config.worker_thread},
        {"kind", config.kind},
    };
}

void from_json(const nlohmann::json& j, ProviderConfig& out) noexcept {
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
    const auto kind_it = j.find("kind");
    if (kind_it != j.end() && kind_it->is_string()) {
        const auto* const name = kind_it->get_ptr<const nlohmann::json::string_t*>();
        if (name != nullptr) {
            static_cast<void>(from_string(std::string_view(*name), out.kind));
        }
    }
}

} // namespace nexigen::chrono
