#include "pdo_decoder.hpp"

#include <cstdio>

#include <nexigen/canopen/pdo_decode.hpp>

namespace nexigen::canview {

namespace {

CoDecodeStatus map_status(canopen::PdoDecodeStatus status) noexcept {
    switch (status) {
        case canopen::PdoDecodeStatus::Ok:            return CoDecodeStatus::Ok;
        case canopen::PdoDecodeStatus::Partial:       return CoDecodeStatus::Partial;
        case canopen::PdoDecodeStatus::UnknownObject: return CoDecodeStatus::UnknownObject;
        case canopen::PdoDecodeStatus::Invalid:
        default:                                      return CoDecodeStatus::Invalid;
    }
}

CoSignal convert_signal(const canopen::PdoSignal& source, const EdsDictionary* dictionary) {
    CoSignal signal{};
    signal.bits = source.bits;
    signal.valid = source.valid;
    signal.raw = source.raw;
    signal.signed_value = source.signed_value;
    signal.real = source.real;
    signal.kind = source.kind;

    const EdsObject* object =
        dictionary != nullptr ? dictionary->find(source.index, source.subindex) : nullptr;
    if (source.unknown_object || object == nullptr) {
        signal.name = nullptr;
        signal.kind = EdsDataKind::Unknown;
        std::snprintf(signal.text.data(), signal.text.size(), "0x%04x:%u", source.index,
                      source.subindex);
        std::snprintf(signal.name_storage.data(), signal.name_storage.size(), "0x%04x:%u (unknown)",
                      source.index, source.subindex);
        signal.name = signal.name_storage.data();
        return signal;
    }

    signal.name = object->name.c_str();
    const EdsDataTypeInfo& type = eds_data_type(object->data_type);
    signal.kind = type.kind;
    if (type.kind == EdsDataKind::Real) {
        std::snprintf(signal.text.data(), signal.text.size(), "%g", signal.real);
    } else if (type.kind == EdsDataKind::String) {
        std::size_t written = 0U;
        for (std::size_t byte = 0U; byte < source.byte_count && written + 1U < signal.text.size();
             ++byte) {
            const auto value = source.bytes[byte];
            if (value >= 0x20U && value <= 0x7EU) {
                signal.text[written++] = static_cast<char>(value);
            }
        }
        signal.text[written] = '\0';
    } else if (type.signed_value) {
        std::snprintf(signal.text.data(), signal.text.size(), "%lld",
                      static_cast<long long>(signal.signed_value));
    } else if (type.kind == EdsDataKind::Boolean) {
        std::snprintf(signal.text.data(), signal.text.size(), "%s",
                      signal.raw != 0U ? "true" : "false");
    } else {
        std::snprintf(signal.text.data(), signal.text.size(), "%llu",
                      static_cast<unsigned long long>(signal.raw));
    }
    return signal;
}
} // namespace

bool decode_pdo(const nexigen::hw::can::CanMessage& message, const EdsPdoMapping& mapping,
                const EdsDictionary* dictionary, CoDecoded& out) noexcept {
    if (mapping.count == 0U) {
        out.status = CoDecodeStatus::Ok;
        out.set_text("PDO without mapping");
        return false;
    }
    const canopen::PdoDecodeResult decoded = canopen::decode_pdo(message, mapping, dictionary);
    out.status = map_status(decoded.status);
    for (std::uint8_t index = 0U; index < decoded.count; ++index) {
        const CoSignal signal = convert_signal(decoded.signals[index], dictionary);
        static_cast<void>(out.add_signal(signal));
    }
    return decoded.count > 0U;
}

} // namespace nexigen::canview
