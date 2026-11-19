// nexigen::chrono — kernel boot identity implementation (change
// add-time-base-service; design D-I).
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

#include <nexigen/chrono/boot_identity.hpp>

namespace nexigen::chrono {
namespace {

/// @brief Convert one hexadecimal digit.
///
/// @param character Candidate digit.
/// @return Value in `0..15`, or `-1` for a non-hex character.
[[nodiscard]] int hex_value(char character) noexcept {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

} // namespace

std::uint64_t boot_token_from_uuid(std::string_view uuid) noexcept {
    std::uint64_t token = 0;
    int nibbles = 0;
    for (const char character : uuid) {
        if (character == '-') {
            continue;
        }
        const int value = hex_value(character);
        if (value < 0) {
            break;
        }
        token = (token << 4U) | static_cast<std::uint64_t>(value);
        if (++nibbles == 16) {
            break;
        }
    }
    return token;
}

BootIdentity read_boot_identity() noexcept {
    BootIdentity identity{};
    std::ifstream file{"/proc/sys/kernel/random/boot_id"};
    if (!file.is_open()) {
        return identity;
    }
    std::string uuid;
    std::getline(file, uuid);
    const std::size_t take =
        uuid.size() < identity.uuid.size() - 1U ? uuid.size() : identity.uuid.size() - 1U;
    for (std::size_t i = 0; i < take; ++i) {
        identity.uuid[i] = uuid[i];
    }
    identity.uuid[take] = '\0';
    identity.token = boot_token_from_uuid(uuid);
    return identity;
}

std::uint64_t current_boot_token() noexcept {
    static const std::uint64_t token = read_boot_identity().token;
    return token;
}

std::string_view format_boot_token(std::uint64_t token,
                                   char (&out)[boot_token_text_size + 1U]) noexcept {
    static constexpr char digits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < boot_token_text_size; ++i) {
        const unsigned shift = static_cast<unsigned>((boot_token_text_size - 1U - i) * 4U);
        out[i] = digits[(token >> shift) & 0xFU];
    }
    out[boot_token_text_size] = '\0';
    return std::string_view{out, boot_token_text_size};
}

bool parse_boot_token(std::string_view text, std::uint64_t& out) noexcept {
    if (text.size() != boot_token_text_size) {
        return false;
    }
    std::uint64_t token = 0;
    for (const char character : text) {
        const int value = hex_value(character);
        if (value < 0) {
            return false;
        }
        token = (token << 4U) | static_cast<std::uint64_t>(value);
    }
    out = token;
    return true;
}

} // namespace nexigen::chrono
