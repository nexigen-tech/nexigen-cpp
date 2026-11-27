#include <nexigen/canopen/cob_id_route.hpp>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::canopen {

void to_json(nlohmann::json& j, const CobIdRoute& route) {
    j = nlohmann::json{{"slot", route.slot},
                       {"node_id", route.node_id},
                       {"service", route.service},
                       {"pdo", route.pdo},
                       {"custom", route.custom}};
}

void from_json(const nlohmann::json& j, CobIdRoute& route) noexcept {
    if (!j.is_object()) {
        return;
    }
    static_cast<void>(core::detail::read_unsigned(j, "slot", route.slot));
    static_cast<void>(core::detail::read_unsigned(j, "node_id", route.node_id));
    core::mandate::read_enum(j, "service", route.service);
    static_cast<void>(core::detail::read_unsigned(j, "pdo", route.pdo));
    static_cast<void>(core::detail::read_bool(j, "custom", route.custom));
}

} // namespace nexigen::canopen
