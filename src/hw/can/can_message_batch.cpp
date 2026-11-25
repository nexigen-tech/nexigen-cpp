#include <nexigen/hw/can/can_message_batch.hpp>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::hw::can {

core::Status CanMessageBatch::validate() const noexcept {
    if (count == 0U || count > max_messages) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (reserved != 0U || first_sequence > UINT64_MAX - count) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (messages[index].channel_id != channel_id) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (const core::Status valid = messages[index].validate(); !valid) {
            return valid;
        }
    }
    return {};
}

void CanMessageBatch::clear() noexcept {
    first_sequence = 0U;
    channel_id = 0U;
    count = 0U;
    reserved = 0U;
    messages = {};
}

std::string CanMessageBatch::to_string() const {
    return std::format("CanMessageBatch(channel={}, count={}, first_sequence={}, max={})",
                       channel_id, count, first_sequence, max_messages);
}

void to_json(nlohmann::json& j, const CanMessageBatch& batch) {
    nlohmann::json messages = nlohmann::json::array();
    for (std::size_t index = 0; index < batch.count; ++index) {
        messages.push_back(batch.messages[index]);
    }
    j = nlohmann::json{{"first_sequence", batch.first_sequence},
                       {"channel_id", batch.channel_id},
                       {"count", batch.count},
                       {"reserved", batch.reserved},
                       {"messages", std::move(messages)}};
}

void from_json(const nlohmann::json& j, CanMessageBatch& batch) noexcept {
    if (!j.is_object() || !j.contains("count") || !j.contains("messages")) {
        return;
    }
    CanMessageBatch parsed{};
    std::uint32_t count = 0U;
    static_cast<void>(core::detail::read_unsigned(j, "first_sequence", parsed.first_sequence));
    static_cast<void>(core::detail::read_unsigned(j, "channel_id", parsed.channel_id));
    static_cast<void>(core::detail::read_unsigned(j, "reserved", parsed.reserved));
    if (!core::detail::read_unsigned(j, "count", count) || count > CanMessageBatch::max_messages) {
        return;
    }
    parsed.count = static_cast<std::uint16_t>(count);
    const auto messages = j.find("messages");
    if (!messages->is_array() || messages->size() < parsed.count) {
        return;
    }
    for (std::size_t index = 0; index < parsed.count; ++index) {
        from_json((*messages)[index], parsed.messages[index]);
    }
    batch = parsed;
}

} // namespace nexigen::hw::can
