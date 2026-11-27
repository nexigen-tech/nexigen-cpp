#include <nexigen/canopen/pdo_decode_result.hpp>

#include <utility>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::canopen {

void to_json(nlohmann::json& j, const PdoDecodeResult& result) {
    nlohmann::json signals = nlohmann::json::array();
    const std::uint8_t count = result.count < result.signals.size()
                                   ? result.count
                                   : static_cast<std::uint8_t>(result.signals.size());
    for (std::uint8_t index = 0U; index < count; ++index) {
        signals.push_back(result.signals[index]);
    }
    j = nlohmann::json{
        {"status", result.status}, {"count", result.count}, {"signals", std::move(signals)}};
}

void from_json(const nlohmann::json& j, PdoDecodeResult& result) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::mandate::read_enum(j, "status", result.status);
    static_cast<void>(core::detail::read_unsigned(j, "count", result.count));
    if (result.count > result.signals.size()) {
        result.count = static_cast<std::uint8_t>(result.signals.size());
    }
    const auto signals = j.find("signals");
    if (signals == j.end() || !signals->is_array()) {
        return;
    }
    std::size_t index = 0U;
    for (const auto& value : *signals) {
        if (index >= result.count) {
            break;
        }
        from_json(value, result.signals[index]);
        ++index;
    }
}

} // namespace nexigen::canopen
