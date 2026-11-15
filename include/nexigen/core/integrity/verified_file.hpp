// nexigen::core::integrity — verified file load (change add-integrity-primitives).
//
// Reads a file once, hashes exactly the bytes it returns, and refuses to return
// them when the expected digest differs, so a file swap between "hash" and
// "read" cannot bypass the check.
#pragma once

#include <string>
#include <string_view>

#include <nexigen/core/integrity/digest_compare.hpp>
#include <nexigen/core/integrity/sha256.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/settings_io.hpp>

namespace nexigen::core::integrity {

/// @brief Read a file and verify it against an expected SHA-256.
///
/// @param path File to read.
/// @param expected_sha256_hex Expected lowercase or uppercase hex digest.
/// @return The exact bytes that were hashed, or `SecurityError` on a digest
///         mismatch, `InvalidArgument` when the file is absent, and
///         `SystemError` when it cannot be read.
/// @note Spec `core/integrity`, requirement "Verified file load": the returned
///       buffer is the one that was hashed, so there is no second read.
[[nodiscard]] inline Result<std::string> read_verified(std::string_view path,
                                                       std::string_view expected_sha256_hex) noexcept {
    std::string text;
    const settings_io::FileRead read = settings_io::read_text_file(path, text);
    if (read != settings_io::FileRead::Ok) {
        return std::unexpected(read == settings_io::FileRead::Absent
                                   ? ErrorCode::InvalidArgument
                                   : ErrorCode::SystemError);
    }
    if (!digests_equal(sha256_hex(text), expected_sha256_hex)) {
        return std::unexpected(ErrorCode::SecurityError);
    }
    return text;
}

} // namespace nexigen::core::integrity
