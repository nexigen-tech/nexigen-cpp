#include <nexigen/canopen/pdo_decode.hpp>

#include <bit>

#include <nexigen/canopen/eds_data_type_info.hpp>

namespace nexigen::canopen {

namespace {

std::uint64_t read_bits(const std::array<std::uint8_t, 64>& data, std::size_t offset,
                        std::uint8_t bits) noexcept {
    std::uint64_t value = 0;
    for (std::uint8_t index = 0; index < bits; ++index) {
        const std::size_t bit = offset + index;
        if (((static_cast<unsigned>(data[bit / 8U]) >> (bit % 8U)) & 1U) != 0U) {
            value |= 1ULL << index;
        }
    }
    return value;
}

std::int64_t sign_extend(std::uint64_t value, std::uint8_t bits) noexcept {
    if (bits == 0U || bits >= 64U) {
        return static_cast<std::int64_t>(value);
    }
    const std::uint64_t sign = 1ULL << (bits - 1U);
    if ((value & sign) == 0U) {
        return static_cast<std::int64_t>(value);
    }
    return static_cast<std::int64_t>(value | ~((1ULL << bits) - 1U));
}

void extract_string(const nexigen::hw::can::CanMessage& message, std::size_t offset,
                    std::uint8_t bits, PdoSignal& signal, PdoDecodeStatus& status) noexcept {
    if (offset % 8U != 0U || bits % 8U != 0U) {
        status = PdoDecodeStatus::Partial;
        return;
    }
    const std::size_t bytes = bits / 8U;
    const std::size_t byte_offset = offset / 8U;
    signal.string_value = true;
    for (std::size_t byte = 0U;
         byte < bytes && byte_offset + byte < message.dlc && byte < signal.bytes.size(); ++byte) {
        signal.bytes[byte] = message.data[byte_offset + byte];
        ++signal.byte_count;
    }
}

void extract_real(PdoSignal& signal) noexcept {
    if (signal.bits == 32U) {
        signal.real =
            static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(signal.raw)));
    } else if (signal.bits == 64U) {
        signal.real = std::bit_cast<double>(signal.raw);
    }
}

bool extract_signal(const EdsMappingEntry& entry, const nexigen::hw::can::CanMessage& message,
                    std::size_t offset, const EdsDictionary* dictionary, PdoSignal& signal,
                    PdoDecodeStatus& status) noexcept {
    signal.index = entry.index;
    signal.subindex = entry.subindex;
    signal.bits = entry.bits;
    signal.valid = true;
    signal.raw = read_bits(message.data, offset, entry.bits);

    const EdsObject* object =
        dictionary != nullptr ? dictionary->find(entry.index, entry.subindex) : nullptr;
    if (object == nullptr) {
        signal.kind = EdsDataKind::Unknown;
        signal.unknown_object = true;
        status = PdoDecodeStatus::UnknownObject;
        return true;
    }

    const EdsDataTypeInfo& type = eds_data_type(object->data_type);
    signal.kind = type.kind;
    if (type.kind == EdsDataKind::Real) {
        extract_real(signal);
        return true;
    }
    if (type.kind == EdsDataKind::String) {
        extract_string(message, offset, entry.bits, signal, status);
        return status != PdoDecodeStatus::Partial;
    }
    signal.signed_value = type.signed_value ? sign_extend(signal.raw, signal.bits)
                                            : static_cast<std::int64_t>(signal.raw);
    return true;
}

} // namespace

PdoDecodeResult decode_pdo(const nexigen::hw::can::CanMessage& message,
                           const EdsPdoMapping& mapping, const EdsDictionary* dictionary) noexcept {
    PdoDecodeResult result{};
    result.status = PdoDecodeStatus::Ok;
    if (mapping.count == 0U) {
        return result;
    }

    std::size_t offset = 0U;
    const std::size_t available = static_cast<std::size_t>(message.dlc) * 8U;
    for (std::uint8_t index = 0U; index < mapping.count; ++index) {
        const EdsMappingEntry& entry = mapping.entries[index];
        if (entry.bits == 0U) {
            continue;
        }
        if (offset + entry.bits > available || entry.bits > 64U) {
            result.status = PdoDecodeStatus::Partial;
            break;
        }
        if (result.count >= PdoDecodeResult::max_signals) {
            result.status = PdoDecodeStatus::Partial;
            break;
        }
        PdoSignal& signal = result.signals[result.count];
        if (!extract_signal(entry, message, offset, dictionary, signal, result.status)) {
            break;
        }
        ++result.count;
        offset += entry.bits;
    }

    return result;
}

} // namespace nexigen::canopen
