#include <nexigen/canopen/sdo_event.hpp>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::canopen {

void to_json(nlohmann::json& j, const SdoEvent& event) {
    j = nlohmann::json{{"kind", event.kind},
                       {"response", event.response},
                       {"segmented", event.segmented},
                       {"last", event.last},
                       {"index", event.index},
                       {"subindex", event.subindex},
                       {"abort_code", event.abort_code},
                       {"value", event.value},
                       {"value_bytes", event.value_bytes},
                       {"size", event.size}};
}

void from_json(const nlohmann::json& j, SdoEvent& event) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::mandate::read_enum(j, "kind", event.kind);
    static_cast<void>(core::detail::read_bool(j, "response", event.response));
    static_cast<void>(core::detail::read_bool(j, "segmented", event.segmented));
    static_cast<void>(core::detail::read_bool(j, "last", event.last));
    static_cast<void>(core::detail::read_unsigned(j, "index", event.index));
    static_cast<void>(core::detail::read_unsigned(j, "subindex", event.subindex));
    static_cast<void>(core::detail::read_unsigned(j, "abort_code", event.abort_code));
    static_cast<void>(core::detail::read_unsigned(j, "value", event.value));
    static_cast<void>(core::detail::read_unsigned(j, "value_bytes", event.value_bytes));
    static_cast<void>(core::detail::read_unsigned(j, "size", event.size));
}

} // namespace nexigen::canopen
