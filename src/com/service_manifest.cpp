// nexigen::com — cold-path service manifest serialization (change
// add-com-module). Compiles only when NEXIGEN_ENABLE_COM=ON.
#include <nexigen/com/service_manifest.hpp>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::com {

namespace {

// One provided-service row. Missing or wrong-typed fields keep their defaults;
// a non-object row rejects the whole collection (see from_json below).
void parse_provided_service(const nlohmann::json& item, ProvidedService& service) noexcept {
    core::detail::read_string(item, "service_name", service.service_name);
    core::detail::read_unsigned(item, "max_subscribers", service.max_subscribers);
    core::detail::read_unsigned(item, "history_size", service.history_size);
}

// One required-service row.
void parse_required_service(const nlohmann::json& item, RequiredService& service) noexcept {
    core::detail::read_string(item, "service_name", service.service_name);
    core::detail::read_unsigned(item, "buffer_capacity", service.buffer_capacity);
    core::detail::read_bool(item, "report_overflow_to_phm", service.report_overflow_to_phm);
}

} // namespace

std::string ProvidedService::to_string() const {
    return std::format("ProvidedService(service_name={}, max_subscribers={}, history_size={})",
                       service_name, max_subscribers, history_size);
}

std::string RequiredService::to_string() const {
    return std::format(
        "RequiredService(service_name={}, buffer_capacity={}, report_overflow_to_phm={})",
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
        required.push_back(
            nlohmann::json{{"service_name", service.service_name},
                           {"buffer_capacity", service.buffer_capacity},
                           {"report_overflow_to_phm", service.report_overflow_to_phm}});
    }
    j["required_services"] = std::move(required);
}

void from_json(const nlohmann::json& j, ServiceManifest& manifest) noexcept {
    if (!j.is_object()) {
        return;
    }
    // A present array with a non-object row rejects the document; an absent or
    // non-array value leaves the destination untouched (pre-refactor behavior).
    // A rejected second collection keeps the first one assigned, as before.
    if (!core::detail::collect_object_array(j, "provided_services", manifest.provided_services,
                                            parse_provided_service)) {
        return;
    }
    static_cast<void>(core::detail::collect_object_array(
        j, "required_services", manifest.required_services, parse_required_service));
}

} // namespace nexigen::com
