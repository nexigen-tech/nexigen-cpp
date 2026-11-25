#include "pdo_decoder.hpp"

#include <bit>
#include <cstdio>
#include <cstring>

namespace nexigen::canview {

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

void render_numeric(CoSignal& signal, bool is_signed) noexcept {
    if (is_signed) {
        signal.signed_value = sign_extend(signal.raw, signal.bits);
        std::snprintf(signal.text.data(), signal.text.size(), "%lld",
                      static_cast<long long>(signal.signed_value));
        return;
    }
    if (signal.kind == EdsDataKind::Boolean) {
        std::snprintf(signal.text.data(), signal.text.size(), "%s",
                      signal.raw != 0U ? "true" : "false");
        return;
    }
    std::snprintf(signal.text.data(), signal.text.size(), "%llu",
                  static_cast<unsigned long long>(signal.raw));
}

void add_unknown(CoSignal signal, const EdsMappingEntry& entry, CoDecoded& out) {
    signal.name = nullptr;
    signal.kind = EdsDataKind::Unknown;
    std::snprintf(signal.text.data(), signal.text.size(), "0x%04x:%u", entry.index, entry.subindex);
    const std::size_t position = out.signal_count;
    if (out.add_signal(signal)) {
        CoSignal& stored = out.signals[position];
        std::snprintf(stored.name_storage.data(), stored.name_storage.size(), "0x%04x:%u (unknown)",
                      entry.index, entry.subindex);
        stored.name = stored.name_storage.data();
    }
}

CoDecodeStatus decode_signal(const EdsMappingEntry& entry,
                             const nexigen::hw::can::CanMessage& message, std::size_t offset,
                             const EdsDictionary* dictionary, CoDecoded& out) noexcept {
    CoSignal signal{};
    signal.bits = entry.bits;
    signal.valid = true;
    signal.raw = read_bits(message.data, offset, entry.bits);
    const EdsObject* object =
        dictionary != nullptr ? dictionary->find(entry.index, entry.subindex) : nullptr;
    if (object == nullptr) {
        add_unknown(signal, entry, out);
        return CoDecodeStatus::UnknownObject;
    }
    signal.name = object->name.c_str();
    const EdsDataTypeInfo& type = eds_data_type(object->data_type);
    signal.kind = type.kind;
    if (type.kind == EdsDataKind::Real) {
        if (entry.bits == 32U) {
            signal.real =
                static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(signal.raw)));
        } else if (entry.bits == 64U) {
            signal.real = std::bit_cast<double>(signal.raw);
        }
        std::snprintf(signal.text.data(), signal.text.size(), "%g", signal.real);
    } else if (type.kind == EdsDataKind::String) {
        if (offset % 8U != 0U || entry.bits % 8U != 0U) {
            return CoDecodeStatus::Partial;
        }
        const std::size_t bytes = entry.bits / 8U;
        const std::size_t byte_offset = offset / 8U;
        std::size_t written = 0U;
        for (std::size_t byte = 0U;
             byte < bytes && byte_offset + byte < message.dlc && written + 1U < signal.text.size();
             ++byte) {
            const auto value = message.data[byte_offset + byte];
            if (value >= 0x20U && value <= 0x7EU) {
                signal.text[written++] = static_cast<char>(value);
            }
        }
        signal.text[written] = '\0';
    } else {
        render_numeric(signal, type.signed_value);
    }
    static_cast<void>(out.add_signal(signal));
    return CoDecodeStatus::Ok;
}

} // namespace

bool decode_pdo(const nexigen::hw::can::CanMessage& message, const EdsPdoMapping& mapping,
                const EdsDictionary* dictionary, CoDecoded& out) noexcept {
    if (mapping.count == 0U) {
        out.status = CoDecodeStatus::Ok;
        out.set_text("PDO without mapping");
        return false;
    }
    std::size_t offset = 0U;
    const std::size_t available = static_cast<std::size_t>(message.dlc) * 8U;
    bool decoded_any = false;
    out.status = CoDecodeStatus::Ok;
    for (std::uint8_t index = 0U; index < mapping.count; ++index) {
        const EdsMappingEntry& entry = mapping.entries[index];
        if (entry.bits == 0U) {
            continue;
        }
        if (offset + entry.bits > available || entry.bits > 64U) {
            out.status = CoDecodeStatus::Partial;
            break;
        }
        const CoDecodeStatus status = decode_signal(entry, message, offset, dictionary, out);
        if (status == CoDecodeStatus::Partial) {
            out.status = CoDecodeStatus::Partial;
            break;
        }
        if (status == CoDecodeStatus::UnknownObject) {
            out.status = CoDecodeStatus::UnknownObject;
        }
        offset += entry.bits;
        decoded_any = true;
    }
    return decoded_any;
}

} // namespace nexigen::canview
