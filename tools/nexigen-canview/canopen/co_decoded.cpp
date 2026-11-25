#include "co_decoded.hpp"

#include <cstdio>
#include <cstring>
#include <format>

#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/core/enum_traits.hpp>

#include "canopen_registry.hpp"

namespace nexigen::canview {

bool CoDecoded::add_signal(const CoSignal& signal) noexcept {
    if (signal_count >= signals.size()) {
        return false;
    }
    signals[signal_count++] = signal;
    return true;
}

void CoDecoded::set_text(std::string_view value) noexcept {
    const std::size_t count = value.size() < text.size() - 1U ? value.size() : text.size() - 1U;
    if (count > 0U) {
        std::memcpy(text.data(), value.data(), count);
    }
    text[count] = '\0';
}

std::string CoSignal::to_string() const {
    return std::format("CoSignal(name={}, bits={}, valid={}, text='{}')",
                       name != nullptr ? name : "?", bits, valid, text.data());
}

void to_json(nlohmann::json& j, const CoSignal& signal) {
    j = nlohmann::json{{"name", signal.name != nullptr ? signal.name : ""},
                       {"kind", signal.kind},
                       {"valid", signal.valid},
                       {"bits", signal.bits},
                       {"raw", signal.raw},
                       {"signed_value", signal.signed_value},
                       {"real", signal.real},
                       {"text", signal.text.data()}};
}

void from_json(const nlohmann::json& j, CoSignal& signal) noexcept {
    if (!j.is_object()) {
        return;
    }
    std::string name;
    static_cast<void>(core::detail::read_string(j, "name", name));
    const std::size_t count =
        name.size() < signal.name_storage.size() ? name.size() : signal.name_storage.size() - 1U;
    if (count > 0U) {
        std::memcpy(signal.name_storage.data(), name.data(), count);
    }
    signal.name_storage[count] = '\0';
    signal.name = signal.name_storage.data();
    core::mandate::read_enum(j, "kind", signal.kind);
    static_cast<void>(core::detail::read_bool(j, "valid", signal.valid));
    static_cast<void>(core::detail::read_unsigned(j, "bits", signal.bits));
    static_cast<void>(core::detail::read_unsigned(j, "raw", signal.raw));
    static_cast<void>(core::detail::read_signed(j, "signed_value", signal.signed_value));
    static_cast<void>(core::detail::read_number(j, "real", signal.real));
    std::string text;
    if (core::detail::read_string(j, "text", text)) {
        std::snprintf(signal.text.data(), signal.text.size(), "%s", text.c_str());
    }
}

void to_json(nlohmann::json& j, const CoDecoded& decoded) {
    nlohmann::json signals = nlohmann::json::array();
    for (std::size_t index = 0; index < decoded.signal_count; ++index) {
        signals.push_back(decoded.signals[index]);
    }
    j = nlohmann::json{{"service", decoded.service},
                       {"status", decoded.status},
                       {"slot", decoded.slot},
                       {"node_id", decoded.node_id},
                       {"index", decoded.index},
                       {"subindex", decoded.subindex},
                       {"abort_code", decoded.abort_code},
                       {"error", decoded.error},
                       {"mapping_stale", decoded.mapping_stale},
                       {"map_epoch", decoded.map_epoch},
                       {"nmt_state", decoded.nmt_state},
                       {"signals", std::move(signals)},
                       {"text", decoded.text.data()}};
}

void from_json(const nlohmann::json& j, CoDecoded& decoded) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::mandate::read_enum(j, "service", decoded.service);
    core::mandate::read_enum(j, "status", decoded.status);
    core::mandate::read_enum(j, "nmt_state", decoded.nmt_state);
    static_cast<void>(core::detail::read_unsigned(j, "slot", decoded.slot));
    static_cast<void>(core::detail::read_unsigned(j, "node_id", decoded.node_id));
    static_cast<void>(core::detail::read_unsigned(j, "index", decoded.index));
    static_cast<void>(core::detail::read_unsigned(j, "subindex", decoded.subindex));
    static_cast<void>(core::detail::read_unsigned(j, "abort_code", decoded.abort_code));
    static_cast<void>(core::detail::read_bool(j, "error", decoded.error));
    static_cast<void>(core::detail::read_bool(j, "mapping_stale", decoded.mapping_stale));
    static_cast<void>(core::detail::read_unsigned(j, "map_epoch", decoded.map_epoch));
    decoded.signal_count = 0U;
    if (const auto it = j.find("signals"); it != j.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (decoded.signal_count >= decoded.signals.size()) {
                break;
            }
            from_json(entry, decoded.signals[decoded.signal_count]);
            ++decoded.signal_count;
        }
    }
    std::string text;
    if (core::detail::read_string(j, "text", text)) {
        decoded.set_text(text);
    }
}

std::string CoDecoded::to_string() const {
    return std::format("CoDecoded(service={}, status={}, node={}, index=0x{:04x}/{}, signals={}, "
                       "text='{}')",
                       service, status, node_id, index, subindex, signal_count, text.data());
}

std::uint32_t co_colour(const CoDecoded& decoded, const NodeBinding* binding,
                        NodeColourMode mode) noexcept {
    constexpr std::uint32_t error_colour{0xFFE53935U};
    if (mode == NodeColourMode::Criticality &&
        (decoded.error || decoded.service == CoService::Emcy ||
         decoded.status == CoDecodeStatus::Aborted)) {
        return error_colour;
    }
    if (binding == nullptr) {
        return 0xFF9E9E9EU;
    }
    return binding->colour;
}

std::string co_inspector_text(const CoDecoded& decoded) {
    std::string out =
        std::format("{} node {} ({})", decoded.service, decoded.node_id, to_string(decoded.status));
    if (decoded.index != 0U || decoded.subindex != 0U) {
        out += std::format(" 0x{:04x}:{}", decoded.index, decoded.subindex);
    }
    if (decoded.status == CoDecodeStatus::Aborted) {
        out += std::format(" abort=0x{:08x}", decoded.abort_code);
    }
    for (std::size_t index = 0; index < decoded.signal_count; ++index) {
        const CoSignal& signal = decoded.signals[index];
        out += std::format("\n  {} = {} ({} bits)", signal.name != nullptr ? signal.name : "?",
                           signal.text.data(), signal.bits);
    }
    return out;
}

} // namespace nexigen::canview
