#include "sdo_decoder.hpp"

#include "eds_dictionary.hpp"

#include <cstdio>
#include <cstring>

namespace nexigen::canview {

namespace {

std::uint32_t read_le32(const std::uint8_t* data) noexcept {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

std::uint32_t read_value(const nexigen::hw::can::CanMessage& message, std::size_t bytes) noexcept {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < bytes && 4U + index < message.dlc; ++index) {
        value |= static_cast<std::uint32_t>(message.data[4U + index]) << (8U * index);
    }
    return value;
}

const char* abort_name(std::uint32_t code) noexcept {
    switch (code) {
        case 0x05030000U: return "toggle bit not alternated";
        case 0x05040000U: return "SDO protocol timeout";
        case 0x06010000U: return "unsupported access";
        case 0x06020000U: return "object does not exist";
        case 0x06040041U: return "object not mappable";
        case 0x06040042U: return "PDO length exceeded";
        case 0x06090011U: return "subindex does not exist";
        case 0x060A0023U: return "resource not available";
        case 0x08000020U: return "data cannot be transferred";
        case 0x08000022U: return "data cannot be transferred in this state";
        default:          return "unknown abort";
    }
}

} // namespace

std::size_t SdoDecoder::node_index(const nexigen::hw::can::CanMessage& message) noexcept {
    if (message.can_id >= 0x580U && message.can_id <= 0x5FFU) {
        return static_cast<std::size_t>(message.can_id - 0x580U);
    }
    if (message.can_id >= 0x600U && message.can_id <= 0x67FU) {
        return static_cast<std::size_t>(message.can_id - 0x600U);
    }
    return 0U;
}

bool SdoDecoder::append_segment(Assembly& assembly,
                                const nexigen::hw::can::CanMessage& message) noexcept {
    if (!assembly.active) {
        return false;
    }
    for (std::size_t index = 1U; index < message.dlc && index <= 7U; ++index) {
        if (assembly.length >= max_transfer_bytes) {
            return false;
        }
        assembly.data[assembly.length] = message.data[index];
        ++assembly.length;
        if (assembly.expected != 0U && assembly.length >= assembly.expected) {
            break;
        }
    }
    return true;
}

void SdoDecoder::maybe_complete(std::size_t node, CoDecoded* out) {
    PendingWrite& pending = pending_[node];
    if (!pending.active || !pending.complete || !pending.response_ok) {
        return;
    }
    SdoCompletedWrite write{};
    write.index = pending.index;
    write.subindex = pending.subindex;
    write.size = pending.length;
    std::memcpy(write.data.data(), pending.data.data(), pending.length);
    completed_.push_back(write);
    if (out != nullptr) {
        out->index = pending.index;
        out->subindex = pending.subindex;
        out->status = CoDecodeStatus::Ok;
        char text[96]{};
        std::snprintf(text, sizeof(text), "SDO write 0x%04x:%u (%zu bytes)", pending.index,
                      pending.subindex, pending.length);
        out->set_text(text);
    }
    pending = PendingWrite{};
}

void SdoDecoder::handle_request(std::size_t node, const nexigen::hw::can::CanMessage& message,
                                std::uint8_t command, CoDecoded& out, std::uint64_t now_ns) {
    out.index = static_cast<std::uint16_t>(message.data[1]) |
                static_cast<std::uint16_t>(static_cast<std::uint16_t>(message.data[2]) << 8U);
    out.subindex = message.data[3];
    const std::uint8_t ccs = static_cast<std::uint8_t>(command & 0xE0U);
    if (command == 0x40U) {
        out.status = CoDecodeStatus::Ok;
        out.set_text("SDO upload request");
        return;
    }
    if (ccs != 0x20U || (command & 0x01U) == 0U) {
        return;
    }
    if ((command & 0x02U) != 0U) {
        // Expedited download request: value bytes are 4 - n.
        const std::size_t bytes = 4U - ((command >> 2U) & 0x03U);
        PendingWrite& pending = pending_[node];
        pending = PendingWrite{};
        pending.active = true;
        pending.index = out.index;
        pending.subindex = out.subindex;
        pending.complete = true;
        pending.length = bytes;
        for (std::size_t index = 0; index < bytes && 4U + index < message.dlc; ++index) {
            pending.data[index] = message.data[4U + index];
        }
        out.status = CoDecodeStatus::Ok;
        out.set_text("SDO expedited write request");
        return;
    }
    // Segmented download initiate (0x21).
    const std::size_t size = message.dlc >= 8U ? read_le32(&message.data[4]) : 0U;
    out.status = CoDecodeStatus::Ok;
    out.set_text("SDO segmented download request");
    if (size > max_transfer_bytes) {
        out.status = CoDecodeStatus::TooLarge;
        out.set_text("SDO segmented transfer exceeds 4096 bytes");
        return;
    }
    PendingWrite& pending = pending_[node];
    pending = PendingWrite{};
    pending.active = true;
    pending.index = out.index;
    pending.subindex = out.subindex;
    pending.complete = false;
    Assembly& assembly = assemblies_[node];
    assembly = Assembly{};
    assembly.active = true;
    assembly.request = true;
    assembly.index = out.index;
    assembly.subindex = out.subindex;
    assembly.expected = size;
    assembly.started = now_ns;
}

void SdoDecoder::handle_response(std::size_t node, const nexigen::hw::can::CanMessage& message,
                                 std::uint8_t command, CoDecoded& out, std::uint64_t now_ns,
                                 const EdsDictionary* dictionary) {
    out.index = static_cast<std::uint16_t>(message.data[1]) |
                static_cast<std::uint16_t>(static_cast<std::uint16_t>(message.data[2]) << 8U);
    out.subindex = message.data[3];
    if (command == 0x60U) {
        pending_[node].response_ok = true;
        maybe_complete(node, &out);
        return;
    }
    const std::uint8_t scs = static_cast<std::uint8_t>(command & 0xE0U);
    if (scs != 0x40U || (command & 0x01U) == 0U) {
        return;
    }
    if ((command & 0x02U) == 0U) {
        // Segmented upload initiate (0x41).
        const std::size_t size = message.dlc >= 8U ? read_le32(&message.data[4]) : 0U;
        if (size > max_transfer_bytes) {
            out.status = CoDecodeStatus::TooLarge;
            out.set_text("SDO segmented transfer exceeds 4096 bytes");
            return;
        }
        Assembly& assembly = assemblies_[node];
        assembly = Assembly{};
        assembly.active = true;
        assembly.request = false;
        assembly.index = out.index;
        assembly.subindex = out.subindex;
        assembly.expected = size;
        assembly.started = now_ns;
        out.status = CoDecodeStatus::Ok;
        out.set_text("SDO segmented upload response");
        return;
    }
    const std::size_t bytes = 4U - ((command >> 2U) & 0x03U);
    const std::uint32_t value = read_value(message, bytes);
    out.status = CoDecodeStatus::Ok;
    const EdsObject* object =
        dictionary != nullptr ? dictionary->find(out.index, out.subindex) : nullptr;
    CoSignal signal{};
    signal.name = object != nullptr ? object->name.c_str() : "value";
    signal.kind = EdsDataKind::Unsigned;
    signal.bits = static_cast<std::uint8_t>(bytes * 8U);
    signal.valid = true;
    signal.raw = value;
    std::snprintf(signal.text.data(), signal.text.size(), "0x%x", value);
    static_cast<void>(out.add_signal(signal));
    char text[96]{};
    std::snprintf(text, sizeof(text), "SDO read %s0x%04x:%u = 0x%x",
                  object != nullptr ? (object->name + " ").c_str() : "", out.index, out.subindex,
                  value);
    out.set_text(text);
}

void SdoDecoder::handle_abort(std::size_t node, const nexigen::hw::can::CanMessage& message,
                              CoDecoded& out) noexcept {
    out.status = CoDecodeStatus::Aborted;
    out.error = true;
    out.index = static_cast<std::uint16_t>(message.data[1]) |
                static_cast<std::uint16_t>(static_cast<std::uint16_t>(message.data[2]) << 8U);
    out.subindex = message.data[3];
    out.abort_code = message.dlc >= 8U ? read_le32(&message.data[4]) : 0U;
    assemblies_[node] = Assembly{};
    pending_[node] = PendingWrite{};
    char text[128]{};
    std::snprintf(text, sizeof(text), "SDO abort 0x%08x: %s", out.abort_code,
                  abort_name(out.abort_code));
    out.set_text(text);
}

bool SdoDecoder::handle_download_segment(std::size_t node,
                                         const nexigen::hw::can::CanMessage& message,
                                         CoDecoded& out) {
    Assembly& assembly = assemblies_[node];
    if (!assembly.active || !assembly.request) {
        return false;
    }
    if (!append_segment(assembly, message)) {
        out.status = CoDecodeStatus::TooLarge;
        out.set_text("SDO segmented transfer exceeds 4096 bytes");
        assembly = Assembly{};
        return true;
    }
    out.status = CoDecodeStatus::Ok;
    out.index = assembly.index;
    out.subindex = assembly.subindex;
    out.set_text("SDO segmented download segment");
    if ((message.data[0] & 0x02U) == 0U) {
        PendingWrite& pending = pending_[node];
        pending.active = true;
        pending.index = assembly.index;
        pending.subindex = assembly.subindex;
        pending.length = assembly.length;
        std::memcpy(pending.data.data(), assembly.data.data(), assembly.length);
        pending.complete = true;
        assembly = Assembly{};
        maybe_complete(node, &out);
    }
    return true;
}

bool SdoDecoder::handle_upload_segment(std::size_t node,
                                       const nexigen::hw::can::CanMessage& message,
                                       CoDecoded& out) {
    Assembly& assembly = assemblies_[node];
    if (!assembly.active || assembly.request) {
        return false;
    }
    if (!append_segment(assembly, message)) {
        out.status = CoDecodeStatus::TooLarge;
        out.set_text("SDO segmented transfer exceeds 4096 bytes");
        assembly = Assembly{};
        return true;
    }
    out.status = CoDecodeStatus::Ok;
    out.index = assembly.index;
    out.subindex = assembly.subindex;
    out.set_text("SDO segmented upload segment");
    if ((message.data[0] & 0x02U) == 0U) {
        assembly = Assembly{};
        out.set_text("SDO segmented upload complete");
    }
    return true;
}

bool SdoDecoder::feed(const nexigen::hw::can::CanMessage& message, bool response, CoDecoded& out,
                      std::uint64_t now_ns, const EdsDictionary* dictionary) {
    if (message.dlc < 4U) {
        return false;
    }
    const std::uint8_t command = message.data[0];
    const std::size_t node = node_index(message);
    if (command == 0x80U) {
        handle_abort(node, message, out);
        return true;
    }
    if (response && (command & 0xE0U) == 0x00U) {
        return handle_upload_segment(node, message, out);
    }
    if (response) {
        handle_response(node, message, command, out, now_ns, dictionary);
        return true;
    }
    if ((command & 0xE0U) == 0x00U) {
        return handle_download_segment(node, message, out);
    }
    if ((command & 0xE0U) == 0x20U || command == 0x40U) {
        handle_request(node, message, command, out, now_ns);
        return true;
    }
    return false;
}

std::optional<SdoCompletedWrite> SdoDecoder::take_completed() {
    if (completed_.empty()) {
        return std::nullopt;
    }
    SdoCompletedWrite write = completed_.front();
    completed_.erase(completed_.begin());
    return write;
}

void SdoDecoder::expire(std::uint64_t now_ns) noexcept {
    for (auto& assembly : assemblies_) {
        if (assembly.active && now_ns > assembly.started &&
            now_ns - assembly.started > timeout_ns) {
            assembly = Assembly{};
        }
    }
}

void SdoDecoder::reset() noexcept {
    assemblies_ = {};
    pending_ = {};
    completed_.clear();
}

std::size_t SdoDecoder::active_assemblies() const noexcept {
    std::size_t count = 0U;
    for (const auto& assembly : assemblies_) {
        if (assembly.active) {
            ++count;
        }
    }
    return count;
}

} // namespace nexigen::canview
