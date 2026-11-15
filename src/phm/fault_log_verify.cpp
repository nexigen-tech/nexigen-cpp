// nexigen::phm — fault-log verification (change add-integrity-primitives).
#include <nexigen/phm/fault_log_verify.hpp>

#include <array>
#include <cstdint>

#include <nexigen/core/integrity/crc32c.hpp>
#include <nexigen/core/settings_io.hpp>

namespace nexigen::phm {

namespace {

/// @brief One parsed line: field views plus the checksum position.
struct LineView {
    std::array<std::string_view, 6> fields{};
    std::size_t count{0}; ///< Total space-separated fields (may exceed 6).
    std::size_t crc_offset{0};
};

/// @brief Split a line on single spaces; the checksum offset is the start of
///        the last field.
[[nodiscard]] LineView split_line(std::string_view line) noexcept {
    LineView view{};
    std::size_t start = 0;
    for (;;) {
        const std::size_t space = line.find(' ', start);
        const std::size_t end = space == std::string_view::npos ? line.size() : space;
        const std::string_view field = line.substr(start, end - start);
        if (view.count < view.fields.size()) {
            view.fields[view.count] = field;
        }
        ++view.count;
        if (space == std::string_view::npos) {
            break;
        }
        start = space + 1U;
    }
    if (view.count == 6U) {
        view.crc_offset = static_cast<std::size_t>(view.fields[5].data() - line.data());
    }
    return view;
}

/// @brief Parse an 8-character hexadecimal CRC.
[[nodiscard]] bool parse_crc(std::string_view text, std::uint32_t& out) noexcept {
    if (text.size() != 8U) {
        return false;
    }
    std::uint32_t value = 0;
    for (const char character : text) {
        value <<= 4U;
        if (character >= '0' && character <= '9') {
            value |= static_cast<std::uint32_t>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            value |= static_cast<std::uint32_t>(character - 'a' + 10);
        } else if (character >= 'A' && character <= 'F') {
            value |= static_cast<std::uint32_t>(character - 'A' + 10);
        } else {
            return false;
        }
    }
    out = value;
    return true;
}

/// @brief Parse a non-negative decimal sequence number.
[[nodiscard]] bool parse_sequence(std::string_view text, std::uint64_t& out) noexcept {
    if (text.empty()) {
        return false;
    }
    std::uint64_t value = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        value = value * 10U + static_cast<std::uint64_t>(character - '0');
    }
    out = value;
    return true;
}

} // namespace

core::Status verify_fault_log(std::string_view path, FaultLogReport& out) noexcept {
    out = FaultLogReport{};
    std::string text;
    const core::settings_io::FileRead read = core::settings_io::read_text_file(path, text);
    if (read != core::settings_io::FileRead::Ok) {
        return std::unexpected(read == core::settings_io::FileRead::Absent
                                   ? core::ErrorCode::InvalidArgument
                                   : core::ErrorCode::SystemError);
    }
    std::uint64_t last_sequence = 0;
    bool have_sequence = false;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const std::size_t newline = text.find('\n', offset);
        const std::size_t end = newline == std::string::npos ? text.size() : newline;
        const std::string_view line(text.data() + offset, end - offset);
        offset = end + 1U;
        if (line.empty()) {
            continue;
        }
        const LineView view = split_line(line);
        if (view.count == 4U) {
            ++out.legacy;
            continue;
        }
        if (view.count != 6U) {
            if (view.count > 6U) {
                ++out.corrupted;
            } else {
                ++out.truncated;
            }
            continue;
        }
        std::uint32_t expected_crc = 0;
        std::uint64_t sequence = 0;
        if (!parse_crc(view.fields[5], expected_crc) || !parse_sequence(view.fields[4], sequence)) {
            ++out.corrupted;
            continue;
        }
        const std::uint32_t actual_crc = core::integrity::crc32c(line.substr(0, view.crc_offset));
        if (actual_crc != expected_crc) {
            ++out.corrupted;
            continue;
        }
        if (have_sequence && sequence <= last_sequence) {
            ++out.corrupted;
            continue;
        }
        last_sequence = sequence;
        have_sequence = true;
        ++out.intact;
    }
    return {};
}

} // namespace nexigen::phm
