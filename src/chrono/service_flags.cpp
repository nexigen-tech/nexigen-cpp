// nexigen::chrono — ServiceFlags cold-path rendering and JSON.
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/service_flags.hpp>

namespace nexigen::chrono {
namespace {

[[nodiscard]] bool parse_name(std::string_view name, ServiceFlag& out) noexcept {
    return from_string(name, out);
}

} // namespace

std::string to_string(const ServiceFlags& flags) {
    std::string out;
    for (const auto& entry : core::EnumTraits<ServiceFlag>::entries) {
        if (!flags.has(entry.value)) {
            continue;
        }
        if (!out.empty()) {
            out += '|';
        }
        out += entry.name;
    }
    return out.empty() ? std::string{"none"} : out;
}

void to_json(nlohmann::json& j, const ServiceFlags& flags) {
    nlohmann::json names = nlohmann::json::array();
    for (const auto& entry : core::EnumTraits<ServiceFlag>::entries) {
        if (flags.has(entry.value)) {
            names.push_back(std::string(entry.name));
        }
    }
    j = std::move(names);
}

void from_json(const nlohmann::json& j, ServiceFlags& flags) noexcept {
    ServiceFlags parsed{};
    if (j.is_number_unsigned() || j.is_number_integer()) {
        parsed.bits = static_cast<std::uint32_t>(j.get<std::uint64_t>());
        flags = parsed;
        return;
    }
    if (j.is_array()) {
        for (const auto& item : j) {
            if (!item.is_string()) {
                continue;
            }
            const auto* const text = item.get_ptr<const nlohmann::json::string_t*>();
            if (text == nullptr) {
                continue;
            }
            ServiceFlag flag{};
            if (parse_name(*text, flag)) {
                parsed.set(flag);
            }
        }
        flags = parsed;
        return;
    }
    if (j.is_string()) {
        const auto* const text = j.get_ptr<const nlohmann::json::string_t*>();
        if (text == nullptr) {
            return;
        }
        std::string_view rest{*text};
        while (!rest.empty()) {
            const std::size_t separator = rest.find_first_of("|, ");
            const std::string_view token = rest.substr(0, separator);
            ServiceFlag flag{};
            if (!token.empty() && parse_name(token, flag)) {
                parsed.set(flag);
            }
            if (separator == std::string_view::npos) {
                break;
            }
            rest.remove_prefix(separator + 1U);
        }
        flags = parsed;
    }
}

} // namespace nexigen::chrono
