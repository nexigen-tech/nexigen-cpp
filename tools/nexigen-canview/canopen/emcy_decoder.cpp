#include "emcy_decoder.hpp"

#include <cstdio>

namespace nexigen::canview {

std::string_view emcy_code_name(std::uint16_t code) noexcept {
    struct Entry {
        std::uint16_t code;
        std::string_view name;
    };
    static constexpr Entry names[] = {
        {0x1000U, "Generic error"},
        {0x2310U, "Current control error"},
        {0x2320U, "Current, output stage"},
        {0x3100U, "Mains voltage"},
        {0x3200U, "Internal voltage"},
        {0x3210U, "Internal voltage, 5V"},
        {0x3300U, "Input voltage"},
        {0x4210U, "Device temperature"},
        {0x5000U, "Device hardware"},
        {0x6000U, "Software error"},
        {0x6100U, "Internal software"},
        {0x6200U, "User software"},
        {0x6300U, "Data set"},
        {0x8000U, "Monitoring"},
        {0x8100U, "Communication"},
        {0x8110U, "CAN overrun"},
        {0x8120U, "CAN error passive"},
        {0x8130U, "Life guard/heartbeat"},
        {0x8140U, "Recovered from bus-off"},
        {0x8150U, "CAN ID collision"},
        {0x8200U, "Protocol error"},
        {0x8210U, "PDO not processed"},
        {0x8220U, "PDO length exceeded"},
        {0x9000U, "External error"},
        {0xF000U, "Additional functions"},
    };
    for (const Entry& entry : names) {
        if (entry.code == code) {
            return entry.name;
        }
    }
    return {};
}

bool decode_emcy(const nexigen::hw::can::CanMessage& message, CoDecoded& out) noexcept {
    if (message.can_id <= 0x080U || message.can_id > 0x0FFU || message.dlc < 3U) {
        return false;
    }
    const auto node_id = static_cast<std::uint8_t>(message.can_id - 0x080U);
    const auto code = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(message.data[0]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(message.data[1]) << 8U));
    const std::uint8_t error_register = message.data[2];

    out.service = CoService::Emcy;
    out.status = CoDecodeStatus::Ok;
    out.error = true;
    out.node_id = node_id;
    out.index = code;
    out.subindex = error_register;

    CoSignal signal{};
    signal.name = "error_code";
    signal.kind = EdsDataKind::Unsigned;
    signal.bits = 16U;
    signal.valid = true;
    signal.raw = static_cast<std::uint64_t>(code);
    std::snprintf(signal.text.data(), signal.text.size(), "0x%04x", code);
    static_cast<void>(out.add_signal(signal));

    signal = {};
    signal.name = "error_register";
    signal.kind = EdsDataKind::Unsigned;
    signal.bits = 8U;
    signal.valid = true;
    signal.raw = error_register;
    std::snprintf(signal.text.data(), signal.text.size(), "0x%02x", error_register);
    static_cast<void>(out.add_signal(signal));

    if (message.dlc > 3U) {
        signal = {};
        signal.name = "manufacturer_data";
        signal.kind = EdsDataKind::String;
        signal.bits = static_cast<std::uint8_t>((message.dlc - 3U) * 8U);
        signal.valid = true;
        std::size_t written = 0U;
        for (std::size_t index = 3U; index < message.dlc && written + 3U < signal.text.size();
             ++index) {
            const int count =
                std::snprintf(signal.text.data() + written, signal.text.size() - written,
                              written == 0U ? "%02x" : " %02x", message.data[index]);
            if (count > 0) {
                written += static_cast<std::size_t>(count);
            }
        }
        static_cast<void>(out.add_signal(signal));
    }

    char text[128]{};
    const std::string_view name = emcy_code_name(code);
    std::snprintf(text, sizeof(text), "EMCY node %u: 0x%04x %.*s (register 0x%02x)", node_id, code,
                  static_cast<int>(name.size()), name.data(), error_register);
    out.set_text(text);
    return true;
}

} // namespace nexigen::canview
