// nexigen::com — cold-path service manifest serialization (change
// add-com-module). Compiles only when NEXIGEN_ENABLE_COM=ON.
#include <nexigen/com/service_manifest.hpp>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::com {

namespace {

// Optional e2e section of one service row. An unknown profile name is recorded
// as a manifest diagnostic; the config itself stays a plain value type.
void parse_e2e(const nlohmann::json& item, E2eConfig& config, std::string_view row,
               std::string_view service_name, std::vector<std::string>& diagnostics) noexcept {
    core::detail::read_object(item, "e2e", [&](const nlohmann::json& value) noexcept {
        std::string name;
        if (core::detail::read_string(value, "profile", name)) {
            if (!nexigen::com::from_string(name, config.profile)) {
                config.profile = E2eProfile::None;
                diagnostics.push_back(std::format("{}.e2e.profile: unknown profile '{}' for '{}'",
                                                  row, name, service_name));
                return;
            }
        }
        core::detail::read_unsigned(value, "data_id", config.data_id);
        core::detail::read_unsigned(value, "max_delta_counter", config.max_delta_counter);
    });
}

// One provided-service row. Missing or wrong-typed fields keep their defaults;
// a non-object row rejects the whole collection (see from_json below).
void parse_provided_service(const nlohmann::json& item, ProvidedService& service,
                            std::vector<std::string>& diagnostics) noexcept {
    core::detail::read_string(item, "service_name", service.service_name);
    core::detail::read_unsigned(item, "max_subscribers", service.max_subscribers);
    core::detail::read_unsigned(item, "history_size", service.history_size);
    parse_e2e(item, service.e2e, "provided_services", service.service_name, diagnostics);
}

// One required-service row.
void parse_required_service(const nlohmann::json& item, RequiredService& service,
                            std::vector<std::string>& diagnostics) noexcept {
    core::detail::read_string(item, "service_name", service.service_name);
    core::detail::read_unsigned(item, "buffer_capacity", service.buffer_capacity);
    core::detail::read_bool(item, "report_overflow_to_phm", service.report_overflow_to_phm);
    parse_e2e(item, service.e2e, "required_services", service.service_name, diagnostics);
}

} // namespace

std::string ProvidedService::to_string() const {
    return std::format("ProvidedService(service_name={}, max_subscribers={}, history_size={}, "
                       "e2e={})",
                       service_name, max_subscribers, history_size, e2e.to_string());
}

std::string RequiredService::to_string() const {
    return std::format("RequiredService(service_name={}, buffer_capacity={}, "
                       "report_overflow_to_phm={}, e2e={})",
                       service_name, buffer_capacity, report_overflow_to_phm, e2e.to_string());
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
        nlohmann::json row{{"service_name", service.service_name},
                           {"max_subscribers", service.max_subscribers},
                           {"history_size", service.history_size}};
        if (service.e2e.profile != E2eProfile::None) {
            row["e2e"] = service.e2e;
        }
        provided.push_back(std::move(row));
    }
    j["provided_services"] = std::move(provided);
    auto required = nlohmann::json::array();
    for (const RequiredService& service : manifest.required_services) {
        nlohmann::json row{{"service_name", service.service_name},
                           {"buffer_capacity", service.buffer_capacity},
                           {"report_overflow_to_phm", service.report_overflow_to_phm}};
        if (service.e2e.profile != E2eProfile::None) {
            row["e2e"] = service.e2e;
        }
        required.push_back(std::move(row));
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
    if (!core::detail::collect_object_array(
            j, "provided_services", manifest.provided_services,
            [&manifest](const nlohmann::json& item, ProvidedService& service) noexcept {
                parse_provided_service(item, service, manifest.diagnostics);
            })) {
        return;
    }
    static_cast<void>(core::detail::collect_object_array(
        j, "required_services", manifest.required_services,
        [&manifest](const nlohmann::json& item, RequiredService& service) noexcept {
            parse_required_service(item, service, manifest.diagnostics);
        }));
}

bool valid_service_manifest(const ServiceManifest& manifest) noexcept {
    std::string error;
    return valid_service_manifest(manifest, error);
}

bool valid_service_manifest(const ServiceManifest& manifest, std::string& error) noexcept {
    error.clear();
    if (!manifest.diagnostics.empty()) {
        error = manifest.diagnostics.front();
        return false;
    }
    for (const ProvidedService& service : manifest.provided_services) {
        if (!valid_e2e_config(service.e2e)) {
            error = std::format("provided service '{}': e2e requires a data id and a positive "
                                "max_delta_counter",
                                service.service_name);
            return false;
        }
    }
    for (const RequiredService& service : manifest.required_services) {
        if (!valid_e2e_config(service.e2e)) {
            error = std::format("required service '{}': e2e requires a data id and a positive "
                                "max_delta_counter",
                                service.service_name);
            return false;
        }
    }
    return true;
}

} // namespace nexigen::com
