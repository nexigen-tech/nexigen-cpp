#include <nexigen/canopen/sdo_completed_write.hpp>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::canopen {

void to_json(nlohmann::json& j, const SdoCompletedWrite& write) {
    j = nlohmann::json{{"index", write.index},
                       {"subindex", write.subindex},
                       {"size", write.size},
                       {"data", write.data}};
}

void from_json(const nlohmann::json& j, SdoCompletedWrite& write) noexcept {
    if (!j.is_object()) {
        return;
    }
    static_cast<void>(core::detail::read_unsigned(j, "index", write.index));
    static_cast<void>(core::detail::read_unsigned(j, "subindex", write.subindex));
    static_cast<void>(core::detail::read_unsigned(j, "size", write.size));
    if (write.size > write.data.size()) {
        write.size = write.data.size();
    }
    const auto data = j.find("data");
    if (data == j.end() || !data->is_array()) {
        return;
    }
    std::size_t index = 0U;
    for (const auto& value : *data) {
        if (index >= write.data.size()) {
            break;
        }
        std::uint8_t parsed = 0U;
        if (core::detail::to_unsigned(value, parsed)) {
            write.data[index++] = parsed;
        }
    }
}

} // namespace nexigen::canopen
