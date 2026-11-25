#include "overwrite_key.hpp"

#include <format>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::canview {

std::string OverwriteKey::to_string() const {
    return std::format("OverwriteKey(channel={}, id=0x{:x}, extended={}, kind={})", channel_id,
                       can_id, extended, kind);
}

void to_json(nlohmann::json& j, const OverwriteKey& key) {
    j = nlohmann::json{{"channel_id", key.channel_id},
                       {"can_id", key.can_id},
                       {"extended", key.extended},
                       {"kind", key.kind}};
}

void from_json(const nlohmann::json& j, OverwriteKey& key) noexcept {
    if (!j.is_object()) {
        return;
    }
    static_cast<void>(core::detail::read_unsigned(j, "channel_id", key.channel_id));
    static_cast<void>(core::detail::read_unsigned(j, "can_id", key.can_id));
    static_cast<void>(core::detail::read_bool(j, "extended", key.extended));
    if (const auto it = j.find("kind"); it != j.end()) {
        from_json(*it, key.kind);
    }
}

} // namespace nexigen::canview
