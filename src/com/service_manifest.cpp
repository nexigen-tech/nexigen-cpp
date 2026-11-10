// nexigen::com — cold-path service manifest serialization (change
// add-com-module). Compiles only when NEXIGEN_ENABLE_COM=ON.
#include <nexigen/com/service_manifest.hpp>

namespace nexigen::com {

std::string ProvidedService::to_string() const {
    return std::format("ProvidedService(service_name={}, max_subscribers={}, history_size={})",
                       service_name, max_subscribers, history_size);
}

std::string RequiredService::to_string() const {
    return std::format("RequiredService(service_name={}, buffer_capacity={}, report_overflow_to_phm={})",
                       service_name, buffer_capacity, report_overflow_to_phm);
}

std::string ServiceManifest::to_string() const {
    std::string out = "ServiceManifest(provided=[";
    for (std::size_t i = 0; i < provided_services.size(); ++i) {
        if (i != 0U) {
            out += ", ";
        }
        out += provided_services[i].to_string();
    }
    out += "], required=[";
    for (std::size_t i = 0; i < required_services.size(); ++i) {
        if (i != 0U) {
            out += ", ";
        }
        out += required_services[i].to_string();
    }
    out += "])";
    return out;
}

void to_json(nlohmann::json& j, const ServiceManifest& manifest) {
    j = nlohmann::json::object();
    auto provided = nlohmann::json::array();
    for (const ProvidedService& service : manifest.provided_services) {
        provided.push_back(nlohmann::json{{"service_name", service.service_name},
                                          {"max_subscribers", service.max_subscribers},
                                          {"history_size", service.history_size}});
    }
    j["provided_services"] = std::move(provided);
    auto required = nlohmann::json::array();
    for (const RequiredService& service : manifest.required_services) {
        required.push_back(nlohmann::json{{"service_name", service.service_name},
                                          {"buffer_capacity", service.buffer_capacity},
                                          {"report_overflow_to_phm", service.report_overflow_to_phm}});
    }
    j["required_services"] = std::move(required);
}

void from_json(const nlohmann::json& j, ServiceManifest& manifest) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto provided = j.find("provided_services");
    if (provided != j.end() && provided->is_array()) {
        std::vector<ProvidedService> parsed;
        for (const auto& item : *provided) {
            if (!item.is_object()) {
                return;
            }
            ProvidedService service;
            const auto name = item.find("service_name");
            if (name != item.end() && name->is_string()) {
                service.service_name = name->get<std::string>();
            }
            const auto subscribers = item.find("max_subscribers");
            if (subscribers != item.end() && subscribers->is_number_unsigned()) {
                service.max_subscribers = subscribers->get<std::uint64_t>();
            }
            const auto history = item.find("history_size");
            if (history != item.end() && history->is_number_unsigned()) {
                service.history_size = history->get<std::uint64_t>();
            }
            parsed.push_back(std::move(service));
        }
        manifest.provided_services = std::move(parsed);
    }
    const auto required = j.find("required_services");
    if (required != j.end() && required->is_array()) {
        std::vector<RequiredService> parsed;
        for (const auto& item : *required) {
            if (!item.is_object()) {
                return;
            }
            RequiredService service;
            const auto name = item.find("service_name");
            if (name != item.end() && name->is_string()) {
                service.service_name = name->get<std::string>();
            }
            const auto capacity = item.find("buffer_capacity");
            if (capacity != item.end() && capacity->is_number_unsigned()) {
                service.buffer_capacity = capacity->get<std::uint64_t>();
            }
            const auto report = item.find("report_overflow_to_phm");
            if (report != item.end() && report->is_boolean()) {
                service.report_overflow_to_phm = report->get<bool>();
            }
            parsed.push_back(std::move(service));
        }
        manifest.required_services = std::move(parsed);
    }
}

} // namespace nexigen::com
