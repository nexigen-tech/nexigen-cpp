#include <nexigen/canopen/cob_id_conflict.hpp>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::canopen {

void to_json(nlohmann::json& j, const CobIdConflict& conflict) {
    j = nlohmann::json{{"channel_id", conflict.channel_id},
                       {"can_id", conflict.can_id},
                       {"kept_slot", conflict.kept_slot},
                       {"rejected_slot", conflict.rejected_slot},
                       {"service", conflict.service}};
}

void from_json(const nlohmann::json& j, CobIdConflict& conflict) noexcept {
    if (!j.is_object()) {
        return;
    }
    static_cast<void>(core::detail::read_unsigned(j, "channel_id", conflict.channel_id));
    static_cast<void>(core::detail::read_unsigned(j, "can_id", conflict.can_id));
    static_cast<void>(core::detail::read_unsigned(j, "kept_slot", conflict.kept_slot));
    static_cast<void>(core::detail::read_unsigned(j, "rejected_slot", conflict.rejected_slot));
    core::mandate::read_enum(j, "service", conflict.service);
}

} // namespace nexigen::canopen
