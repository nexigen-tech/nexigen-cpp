// nexigen::recorder — cold-path payload decoding for the converter.
#include <nexigen/recorder/payload_decoder.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace nexigen::recorder {

namespace {

[[nodiscard]] std::string_view type_base(std::string_view type) noexcept {
    const auto bracket = type.find('[');
    return bracket == std::string_view::npos ? type : type.substr(0, bracket);
}

[[nodiscard]] std::uint64_t read_le_unsigned(std::span<const std::byte> payload, std::size_t offset,
                                             std::uint32_t size) noexcept {
    std::uint64_t value = 0;
    for (std::uint32_t i = 0; i < size; ++i) {
        value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(payload[offset + i]))
                 << (8U * i);
    }
    return value;
}

[[nodiscard]] std::int64_t read_le_signed(std::span<const std::byte> payload, std::size_t offset,
                                          std::uint32_t size) noexcept {
    const std::uint64_t raw = read_le_unsigned(payload, offset, size);
    const std::uint32_t bits = size * 8U;
    if (bits == 64U) {
        return static_cast<std::int64_t>(raw);
    }
    const std::uint64_t sign = 1ULL << (bits - 1U);
    if ((raw & sign) == 0U) {
        return static_cast<std::int64_t>(raw);
    }
    return static_cast<std::int64_t>(raw | (~0ULL << bits));
}

[[nodiscard]] bool integer_size_ok(std::uint32_t size) noexcept {
    return size == 1U || size == 2U || size == 4U || size == 8U;
}

[[nodiscard]] bool is_unsigned_base(std::string_view base) noexcept {
    return base == "u8" || base == "u16" || base == "u32" || base == "u64" || base == "uint8" ||
           base == "uint16" || base == "uint32" || base == "uint64";
}

[[nodiscard]] bool is_signed_base(std::string_view base) noexcept {
    return base == "i8" || base == "i16" || base == "i32" || base == "i64" || base == "int8" ||
           base == "int16" || base == "int32" || base == "int64";
}

[[nodiscard]] core::Result<nlohmann::json> decode_scalar(std::span<const std::byte> payload,
                                                         std::string_view base, std::size_t offset,
                                                         std::uint32_t size) {
    if (is_unsigned_base(base)) {
        return read_le_unsigned(payload, offset, size);
    }
    if (is_signed_base(base)) {
        return read_le_signed(payload, offset, size);
    }
    if (base == "f32" || base == "float" || base == "f32le") {
        const auto bits = static_cast<std::uint32_t>(read_le_unsigned(payload, offset, size));
        float value = 0.0F;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    if (base == "f64" || base == "double" || base == "f64le") {
        const auto bits = read_le_unsigned(payload, offset, size);
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    return std::unexpected(core::ErrorCode::UnsupportedFormat);
}

[[nodiscard]] core::Result<nlohmann::json>
decode_field(const DatasetField& field, std::span<const std::byte> payload) noexcept {
    if (field.element_size == 0U || field.count == 0U || !integer_size_ok(field.element_size)) {
        return std::unexpected(core::ErrorCode::UnsupportedFormat);
    }
    const std::uint32_t stride = field.stride == 0U ? field.element_size : field.stride;
    const std::string_view base = type_base(field.type);
    const std::size_t bytes =
        static_cast<std::size_t>(stride) * (field.count - 1U) + field.element_size;
    if (static_cast<std::size_t>(field.offset) + bytes > payload.size()) {
        return std::unexpected(core::ErrorCode::CorruptData);
    }
    if (base == "char") {
        std::string text;
        for (std::uint32_t i = 0; i < field.count; ++i) {
            const auto byte =
                static_cast<char>(payload[field.offset + static_cast<std::size_t>(i) * stride]);
            if (byte == '\0') {
                break;
            }
            text.push_back(byte);
        }
        return nlohmann::json(text);
    }
    if (field.count == 1U) {
        return decode_scalar(payload, base, field.offset, field.element_size);
    }
    nlohmann::json array = nlohmann::json::array();
    for (std::uint32_t i = 0; i < field.count; ++i) {
        auto value = decode_scalar(
            payload, base, field.offset + static_cast<std::size_t>(i) * stride, field.element_size);
        if (!value.has_value()) {
            return value;
        }
        array.push_back(std::move(*value));
    }
    return array;
}

[[nodiscard]] core::Result<nlohmann::json>
decode_pod_fields(const std::vector<DatasetField>& fields,
                  std::span<const std::byte> payload) noexcept {
    if (fields.empty()) {
        return std::unexpected(core::ErrorCode::CorruptData);
    }
    nlohmann::json object = nlohmann::json::object();
    for (const auto& field : fields) {
        if (field.name.empty()) {
            return std::unexpected(core::ErrorCode::CorruptData);
        }
        auto value = decode_field(field, payload);
        if (!value.has_value()) {
            return value;
        }
        object[field.name] = std::move(*value);
    }
    return object;
}

[[nodiscard]] std::string base64(std::span<const std::byte> bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bytes.size() + 2U) / 3U) * 4U);
    std::size_t i = 0;
    for (; i + 3U <= bytes.size(); i += 3U) {
        const auto a = static_cast<std::uint8_t>(bytes[i]);
        const auto b = static_cast<std::uint8_t>(bytes[i + 1U]);
        const auto c = static_cast<std::uint8_t>(bytes[i + 2U]);
        out.push_back(alphabet[a >> 2U]);
        out.push_back(alphabet[((a & 0x03U) << 4U) | (b >> 4U)]);
        out.push_back(alphabet[((b & 0x0FU) << 2U) | (c >> 6U)]);
        out.push_back(alphabet[c & 0x3FU]);
    }
    const std::size_t rest = bytes.size() - i;
    if (rest == 1U) {
        const auto a = static_cast<std::uint8_t>(bytes[i]);
        out.push_back(alphabet[a >> 2U]);
        out.push_back(alphabet[(a & 0x03U) << 4U]);
        out.append("==");
    } else if (rest == 2U) {
        const auto a = static_cast<std::uint8_t>(bytes[i]);
        const auto b = static_cast<std::uint8_t>(bytes[i + 1U]);
        out.push_back(alphabet[a >> 2U]);
        out.push_back(alphabet[((a & 0x03U) << 4U) | (b >> 4U)]);
        out.push_back(alphabet[(b & 0x0FU) << 2U]);
        out.push_back('=');
    }
    return out;
}

[[nodiscard]] bool read_varint(std::span<const std::uint8_t> data, std::size_t& pos,
                               std::uint64_t& out) noexcept {
    out = 0;
    for (std::uint32_t shift = 0; shift < 64U && pos < data.size(); shift += 7U) {
        const auto byte = data[pos++];
        out |= static_cast<std::uint64_t>(byte & 0x7FU) << shift;
        if ((byte & 0x80U) == 0U) {
            return true;
        }
    }
    return false;
}

/// @brief Probe one `FileDescriptorProto` message for its required name field.
[[nodiscard]] bool valid_file_descriptor(std::span<const std::uint8_t> data) noexcept {
    std::size_t pos = 0;
    bool has_name = false;
    while (pos < data.size()) {
        std::uint64_t key = 0;
        if (!read_varint(data, pos, key)) {
            return false;
        }
        const auto field = key >> 3U;
        const auto wire = key & 0x07U;
        if (wire == 2U) {
            std::uint64_t length = 0;
            if (!read_varint(data, pos, length) || length > data.size() - pos) {
                return false;
            }
            if (field == 1U) {
                has_name = length > 0U;
            }
            pos += static_cast<std::size_t>(length);
        } else if (wire == 0U) {
            std::uint64_t ignored = 0;
            if (!read_varint(data, pos, ignored)) {
                return false;
            }
        } else if (wire == 1U) {
            if (data.size() - pos < 8U) {
                return false;
            }
            pos += 8U;
        } else if (wire == 5U) {
            if (data.size() - pos < 4U) {
                return false;
            }
            pos += 4U;
        } else {
            return false;
        }
    }
    return has_name;
}

/// @brief Structural probe of an embedded `FileDescriptorSet`.
[[nodiscard]] bool valid_protobuf_descriptor(std::span<const std::uint8_t> data) noexcept {
    std::size_t pos = 0;
    bool has_file = false;
    while (pos < data.size()) {
        std::uint64_t key = 0;
        if (!read_varint(data, pos, key)) {
            return false;
        }
        const auto field = key >> 3U;
        const auto wire = key & 0x07U;
        if (wire == 2U) {
            std::uint64_t length = 0;
            if (!read_varint(data, pos, length) || length > data.size() - pos) {
                return false;
            }
            const auto span = data.subspan(pos, static_cast<std::size_t>(length));
            if (field == 1U && !valid_file_descriptor(span)) {
                return false;
            }
            has_file = has_file || field == 1U;
            pos += static_cast<std::size_t>(length);
        } else if (wire == 0U) {
            std::uint64_t ignored = 0;
            if (!read_varint(data, pos, ignored)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return has_file;
}

[[nodiscard]] std::uint32_t read_u32(std::span<const std::uint8_t> data,
                                     std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(read_le_unsigned(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(data.data()), data.size()),
        offset, 4U));
}

[[nodiscard]] std::int32_t read_i32(std::span<const std::uint8_t> data,
                                    std::size_t offset) noexcept {
    return static_cast<std::int32_t>(read_u32(data, offset));
}

[[nodiscard]] std::uint16_t read_u16(std::span<const std::uint8_t> data,
                                     std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(data[offset] |
                                      (static_cast<std::uint16_t>(data[offset + 1U]) << 8U));
}

/// @brief Structural probe of an embedded FlatBuffers reflection schema.
[[nodiscard]] bool valid_flatbuffer_schema(std::span<const std::uint8_t> data) noexcept {
    if (data.size() < 8U) {
        return false;
    }
    const std::uint32_t root = read_u32(data, 0);
    if (root < 4U || static_cast<std::size_t>(root) + 4U > data.size()) {
        return false;
    }
    const std::int32_t soffset = read_i32(data, root);
    const std::int64_t vtable = static_cast<std::int64_t>(root) - soffset;
    if (vtable < 0 || static_cast<std::size_t>(vtable) + 4U > data.size()) {
        return false;
    }
    const std::uint16_t vtable_bytes = read_u16(data, static_cast<std::size_t>(vtable));
    if (vtable_bytes < 4U || (vtable_bytes % 2U) != 0U ||
        static_cast<std::size_t>(vtable) + vtable_bytes > data.size()) {
        return false;
    }
    const std::uint16_t object_bytes = read_u16(data, static_cast<std::size_t>(vtable) + 2U);
    if (object_bytes < 4U || static_cast<std::size_t>(root) + object_bytes > data.size()) {
        return false;
    }
    return true;
}

[[nodiscard]] bool is_json_encoding(std::string_view encoding) noexcept {
    return encoding == "jsonschema" || encoding == "json" || encoding == "nexigen.pod.v1";
}

[[nodiscard]] core::Result<PayloadView> decode_json(std::span<const std::byte> payload) noexcept {
    const std::string text(reinterpret_cast<const char*>(payload.data()), payload.size());
    const auto document = nlohmann::json::parse(text, nullptr, false);
    if (document.is_discarded()) {
        return std::unexpected(core::ErrorCode::CorruptData);
    }
    return PayloadView{PayloadKind::Json, document.dump()};
}

[[nodiscard]] core::Result<PayloadView> opaque(std::span<const std::byte> payload) noexcept {
    return PayloadView{PayloadKind::Opaque, base64(payload)};
}

} // namespace

bool encoding_supported(std::string_view encoding) noexcept {
    return is_json_encoding(encoding) || encoding == "protobuf" || encoding == "flatbuffer" ||
           encoding == "ros2msg";
}

core::Result<PayloadView> decode_payload(std::string_view encoding,
                                         std::span<const std::uint8_t> descriptor,
                                         const std::vector<DatasetField>& fields,
                                         std::span<const std::byte> payload) noexcept {
    if (is_json_encoding(encoding)) {
        if (!fields.empty()) {
            const auto object = decode_pod_fields(fields, payload);
            if (!object.has_value()) {
                return std::unexpected(object.error());
            }
            return PayloadView{
                PayloadKind::Json,
                object->dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)};
        }
        return decode_json(payload);
    }
    if (encoding == "protobuf") {
        if (descriptor.empty() || !valid_protobuf_descriptor(descriptor)) {
            return std::unexpected(core::ErrorCode::CorruptData);
        }
        return opaque(payload);
    }
    if (encoding == "flatbuffer") {
        if (descriptor.empty() || !valid_flatbuffer_schema(descriptor)) {
            return std::unexpected(core::ErrorCode::CorruptData);
        }
        return opaque(payload);
    }
    if (encoding == "ros2msg") {
        if (descriptor.empty()) {
            return std::unexpected(core::ErrorCode::NotFound);
        }
        return opaque(payload);
    }
    return std::unexpected(core::ErrorCode::UnsupportedFormat);
}

} // namespace nexigen::recorder
