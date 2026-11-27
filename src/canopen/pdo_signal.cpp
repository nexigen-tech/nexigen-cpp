#include <nexigen/canopen/pdo_signal.hpp>

#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/core/enum_traits.hpp>

namespace nexigen::canopen {

void to_json(nlohmann::json& j, const PdoSignal& signal) {
    j = nlohmann::json{{"index", signal.index},
                       {"subindex", signal.subindex},
                       {"bits", signal.bits},
                       {"kind", signal.kind},
                       {"raw", signal.raw},
                       {"signed_value", signal.signed_value},
                       {"real", signal.real},
                       {"valid", signal.valid},
                       {"unknown_object", signal.unknown_object},
                       {"string_value", signal.string_value},
                       {"byte_count", signal.byte_count},
                       {"bytes", signal.bytes}};
}

void from_json(const nlohmann::json& j, PdoSignal& signal) noexcept {
    if (!j.is_object()) {
        return;
    }
    static_cast<void>(core::detail::read_unsigned(j, "index", signal.index));
    static_cast<void>(core::detail::read_unsigned(j, "subindex", signal.subindex));
    static_cast<void>(core::detail::read_unsigned(j, "bits", signal.bits));
    core::mandate::read_enum(j, "kind", signal.kind);
    static_cast<void>(core::detail::read_unsigned(j, "raw", signal.raw));
    static_cast<void>(core::detail::read_signed(j, "signed_value", signal.signed_value));
    static_cast<void>(core::detail::read_number(j, "real", signal.real));
    static_cast<void>(core::detail::read_bool(j, "valid", signal.valid));
    static_cast<void>(core::detail::read_bool(j, "unknown_object", signal.unknown_object));
    static_cast<void>(core::detail::read_bool(j, "string_value", signal.string_value));
    static_cast<void>(core::detail::read_unsigned(j, "byte_count", signal.byte_count));
    const auto bytes = j.find("bytes");
    if (bytes == j.end() || !bytes->is_array()) {
        return;
    }
    std::size_t index = 0U;
    for (const auto& value : *bytes) {
        if (index >= signal.bytes.size()) {
            break;
        }
        std::uint8_t parsed = 0U;
        if (core::detail::to_unsigned(value, parsed)) {
            signal.bytes[index++] = parsed;
        }
    }
}

} // namespace nexigen::canopen
