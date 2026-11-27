#include <nexigen/canopen/sdo_assembler.hpp>

#include <cstring>

namespace nexigen::canopen {

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

} // namespace

std::size_t SdoAssembler::node_index(const nexigen::hw::can::CanMessage& message) noexcept {
    if (message.can_id >= 0x580U && message.can_id <= 0x5FFU) {
        return static_cast<std::size_t>(message.can_id - 0x580U);
    }
    if (message.can_id >= 0x600U && message.can_id <= 0x67FU) {
        return static_cast<std::size_t>(message.can_id - 0x600U);
    }
    return 0U;
}

bool SdoAssembler::append_segment(Assembly& assembly,
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

void SdoAssembler::maybe_complete(std::size_t node, SdoEvent& out) {
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

    out.kind = SdoEventKind::CompletedWrite;
    out.index = pending.index;
    out.subindex = pending.subindex;
    out.size = pending.length;
    pending = PendingWrite{};
}

void SdoAssembler::handle_request(std::size_t node, const nexigen::hw::can::CanMessage& message,
                                  std::uint8_t command, SdoEvent& out, std::uint64_t now_ns) {
    out.index = static_cast<std::uint16_t>(message.data[1]) |
                static_cast<std::uint16_t>(static_cast<std::uint16_t>(message.data[2]) << 8U);
    out.subindex = message.data[3];
    out.response = false;
    const std::uint8_t ccs = static_cast<std::uint8_t>(command & 0xE0U);
    if (command == 0x40U) {
        out.kind = SdoEventKind::UploadRequest;
        return;
    }
    if (ccs != 0x20U || (command & 0x01U) == 0U) {
        return;
    }
    if ((command & 0x02U) != 0U) {
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
        out.kind = SdoEventKind::ExpeditedWriteRequest;
        return;
    }
    const std::size_t size = message.dlc >= 8U ? read_le32(&message.data[4]) : 0U;
    out.kind = SdoEventKind::SegmentedDownloadRequest;
    out.segmented = true;
    if (size > max_transfer_bytes) {
        out.kind = SdoEventKind::TooLarge;
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

void SdoAssembler::handle_response(std::size_t node, const nexigen::hw::can::CanMessage& message,
                                   std::uint8_t command, SdoEvent& out, std::uint64_t now_ns) {
    out.index = static_cast<std::uint16_t>(message.data[1]) |
                static_cast<std::uint16_t>(static_cast<std::uint16_t>(message.data[2]) << 8U);
    out.subindex = message.data[3];
    out.response = true;
    if (command == 0x60U) {
        pending_[node].response_ok = true;
        maybe_complete(node, out);
        if (out.kind == SdoEventKind::None) {
            out.kind = SdoEventKind::DownloadResponse;
        }
        return;
    }
    const std::uint8_t scs = static_cast<std::uint8_t>(command & 0xE0U);
    if (scs != 0x40U || (command & 0x01U) == 0U) {
        return;
    }
    if ((command & 0x02U) == 0U) {
        const std::size_t size = message.dlc >= 8U ? read_le32(&message.data[4]) : 0U;
        if (size > max_transfer_bytes) {
            out.kind = SdoEventKind::TooLarge;
            out.segmented = true;
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
        out.kind = SdoEventKind::UploadResponse;
        out.segmented = true;
        return;
    }
    const std::size_t bytes = 4U - ((command >> 2U) & 0x03U);
    out.kind = SdoEventKind::UploadResponse;
    out.segmented = false;
    out.value_bytes = static_cast<std::uint8_t>(bytes);
    out.value = read_value(message, bytes);
}

void SdoAssembler::handle_abort(std::size_t node, const nexigen::hw::can::CanMessage& message,
                                SdoEvent& out) noexcept {
    out.kind = SdoEventKind::Abort;
    out.response = true;
    out.index = static_cast<std::uint16_t>(message.data[1]) |
                static_cast<std::uint16_t>(static_cast<std::uint16_t>(message.data[2]) << 8U);
    out.subindex = message.data[3];
    out.abort_code = message.dlc >= 8U ? read_le32(&message.data[4]) : 0U;
    assemblies_[node] = Assembly{};
    pending_[node] = PendingWrite{};
}

bool SdoAssembler::handle_download_segment(std::size_t node,
                                           const nexigen::hw::can::CanMessage& message,
                                           SdoEvent& out) {
    Assembly& assembly = assemblies_[node];
    if (!assembly.active || !assembly.request) {
        return false;
    }
    if (!append_segment(assembly, message)) {
        out.kind = SdoEventKind::TooLarge;
        out.segmented = true;
        assembly = Assembly{};
        return true;
    }
    out.kind = SdoEventKind::DownloadSegment;
    out.segmented = true;
    out.response = false;
    out.index = assembly.index;
    out.subindex = assembly.subindex;
    if ((message.data[0] & 0x02U) == 0U) {
        out.last = true;
        PendingWrite& pending = pending_[node];
        pending.active = true;
        pending.index = assembly.index;
        pending.subindex = assembly.subindex;
        pending.length = assembly.length;
        std::memcpy(pending.data.data(), assembly.data.data(), assembly.length);
        pending.complete = true;
        assembly = Assembly{};
        maybe_complete(node, out);
    }
    return true;
}

bool SdoAssembler::handle_upload_segment(std::size_t node,
                                         const nexigen::hw::can::CanMessage& message,
                                         SdoEvent& out) {
    Assembly& assembly = assemblies_[node];
    if (!assembly.active || assembly.request) {
        return false;
    }
    if (!append_segment(assembly, message)) {
        out.kind = SdoEventKind::TooLarge;
        out.segmented = true;
        assembly = Assembly{};
        return true;
    }
    out.kind = SdoEventKind::UploadSegment;
    out.segmented = true;
    out.response = true;
    out.index = assembly.index;
    out.subindex = assembly.subindex;
    if ((message.data[0] & 0x02U) == 0U) {
        out.last = true;
        assembly = Assembly{};
    }
    return true;
}

bool SdoAssembler::feed(const nexigen::hw::can::CanMessage& message, bool response,
                        std::uint64_t now_ns, SdoEvent& out) noexcept {
    out = SdoEvent{};
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
        handle_response(node, message, command, out, now_ns);
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

std::optional<SdoCompletedWrite> SdoAssembler::take_completed() {
    if (completed_.empty()) {
        return std::nullopt;
    }
    SdoCompletedWrite write = completed_.front();
    completed_.erase(completed_.begin());
    return write;
}

void SdoAssembler::expire(std::uint64_t now_ns) noexcept {
    for (auto& assembly : assemblies_) {
        if (assembly.active && now_ns > assembly.started &&
            now_ns - assembly.started > timeout_ns) {
            assembly = Assembly{};
        }
    }
}

void SdoAssembler::reset() noexcept {
    assemblies_ = {};
    pending_ = {};
    completed_.clear();
}

std::size_t SdoAssembler::active_assemblies() const noexcept {
    std::size_t count = 0U;
    for (const auto& assembly : assemblies_) {
        if (assembly.active) {
            ++count;
        }
    }
    return count;
}

} // namespace nexigen::canopen
