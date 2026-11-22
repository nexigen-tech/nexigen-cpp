// nexigen::core — canonical application identity code (change
// widen-app-id-to-16-bytes; spec core/app-id).
//
// One logical application is identified by 1..16 bytes from a deliberately
// small alphabet: `A-Z`, `a-z`, `0-9`, `_`, and `-`. The value stores exactly
// 16 bytes with a zero-filled tail so it can be copied verbatim into the
// telemetry frames without a serialization step. Construction is fallible and
// never truncates.
#pragma once

#include <array>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>

namespace nexigen::core {

/// @brief Canonical 16-byte application identity code.
///
/// Used by manifests, the command line, the telemetry sinks, and the wire
/// frames. A default-constructed value is unset and renders as an empty
/// string; an unset id tells a policy to keep its own fallback identity.
///
/// The type is exactly 16 bytes and trivially copyable, so it can be copied
/// into `char app_id[16]` with `std::memcpy` and read back without touching a
/// heap. `to_string()` allocates and is therefore cold path only; `text()`
/// returns a bounded view over the inline storage.
///
/// @note Spec `core/app-id`, requirement "Application identifier is a fixed
///       16-byte code".
/// @note Spec `core/app-id`, requirement "Identifier construction never
///       truncates".
struct AppId {
    /// @brief Maximum accepted identifier width in bytes.
    static constexpr std::size_t capacity = 16;

    /// @brief Construct the unset identifier.
    ///
    /// @note The zero-filled storage renders as an empty string.
    constexpr AppId() noexcept = default;

    /// @brief Reset the value to unset.
    ///
    /// @note Cold path only; the wire copy is made by value.
    constexpr void clear() noexcept { bytes_.fill('\0'); }

    /// @brief Create an identifier from text.
    ///
    /// @param text Candidate bytes; must be 1..16 bytes from `A-Z`, `a-z`,
    ///        `0-9`, `_`, or `-`.
    /// @return The value on success, otherwise
    ///         `core::ErrorCode::InvalidArgument`.
    /// @note Empty, oversized, embedded-NUL, and forbidden-byte inputs are
    ///       rejected; the input is never truncated.
    /// @note Spec `core/app-id`, requirement "Identifier construction never
    ///       truncates".
    [[nodiscard]] static Result<AppId> create(std::string_view text) noexcept {
        if (text.empty() || text.size() > capacity) {
            return std::unexpected(ErrorCode::InvalidArgument);
        }
        AppId id;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (!is_permitted(text[i])) {
                return std::unexpected(ErrorCode::InvalidArgument);
            }
            id.bytes_[i] = text[i];
        }
        return id;
    }

    /// @brief Whether every byte of a string literal is permitted.
    ///
    /// @tparam N Literal size including the terminating NUL.
    /// @param text Literal to inspect.
    /// @return `true` when the literal holds 1..16 permitted bytes.
    /// @note This predicate exists for compile-time policy tags: an
    ///       `Iceoryx2Policy` instantiated with an invalid tag fails to
    ///       compile instead of publishing a malformed frame.
    /// @note Spec `core/app-id`, requirement "Identifier construction never
    ///       truncates".
    template <std::size_t N>
    [[nodiscard]] static constexpr bool literal_is_valid(const char (&text)[N]) noexcept {
        if (N < 2 || N > capacity + 1) {
            return false;
        }
        for (std::size_t i = 0; i + 1 < N; ++i) {
            if (!is_permitted(text[i])) {
                return false;
            }
        }
        return true;
    }

    /// @brief Adopt a literal that `literal_is_valid` accepted.
    ///
    /// @tparam N Literal size including the terminating NUL.
    /// @param text Literal bytes.
    /// @return The identifier value.
    /// @note Invalid characters and an out-of-range width are rejected at
    ///       compile time: the consteval evaluation reaches a non-constant
    ///       `std::abort()` and the call fails to compile.
    /// @note Spec `core/app-id`, requirement "Application identifier is a
    ///       fixed 16-byte code".
    template <std::size_t N>
    [[nodiscard]] static consteval AppId from_literal(const char (&text)[N]) noexcept {
        static_assert(N >= 2 && N <= capacity + 1, "AppId literal must hold 1..16 bytes");
        if (!literal_is_valid(text)) {
            std::abort(); // non-constant in a consteval call: invalid literals do not compile
        }
        AppId id;
        for (std::size_t i = 0; i + 1 < N; ++i) {
            id.bytes_[i] = text[i];
        }
        return id;
    }

    /// @brief Whether the value is unset.
    ///
    /// @return `true` when the first stored byte is NUL.
    /// @note Spec `core/app-id`, requirement "Application identifier is a
    ///       fixed 16-byte code".
    [[nodiscard]] constexpr bool empty() const noexcept { return bytes_[0] == '\0'; }

    /// @brief Number of stored identifier bytes.
    ///
    /// @return Length up to the first NUL, in `0..16`.
    /// @note A full-width identifier has no NUL; the bound is the capacity.
    [[nodiscard]] constexpr std::size_t size() const noexcept {
        std::size_t length = 0;
        while (length < capacity && bytes_[length] != '\0') {
            ++length;
        }
        return length;
    }

    /// @brief Read the identifier as a bounded view.
    ///
    /// @return View over `size()` stored bytes; empty when unset.
    /// @note Never allocates and never reads past the inline storage.
    [[nodiscard]] constexpr std::string_view text() const noexcept {
        return std::string_view{bytes_.data(), size()};
    }

    /// @brief Access the raw 16-byte storage for a frame copy.
    ///
    /// @return Pointer to the first byte of the zero-filled storage.
    /// @note The storage is exactly `capacity` bytes; unused bytes are zero.
    [[nodiscard]] constexpr const char* data() const noexcept { return bytes_.data(); }

    /// @brief Render the identifier as owned text.
    ///
    /// @return The stored bytes as a `std::string`.
    /// @note Allocates, so cold path only (project mandate 9).
    [[nodiscard]] std::string to_string() const { return std::string(text()); }

    /// @brief Compare two identifiers byte for byte.
    ///
    /// @param other Identifier to compare against.
    /// @return `true` when both stored 16-byte images are equal.
    /// @note The zero-filled tail is part of the invariant, so a valid value
    ///       and the unset value never compare equal.
    [[nodiscard]] friend constexpr bool operator==(const AppId&, const AppId&) noexcept = default;

private:
    /// @brief Whether one byte belongs to the permitted alphabet.
    ///
    /// @param c Candidate byte.
    /// @return `true` for `A-Z`, `a-z`, `0-9`, `_`, or `-`.
    /// @note The alphabet is safe in JSON, recorder channel topics, and
    ///       Prometheus labels (spec `core/app-id`).
    [[nodiscard]] static constexpr bool is_permitted(char c) noexcept {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-';
    }

    std::array<char, capacity> bytes_{}; ///< Zero-filled identifier image.
};

static_assert(sizeof(AppId) == 16, "AppId must be exactly 16 bytes for the wire field");
static_assert(std::is_trivially_copyable_v<AppId>, "AppId must cross the zero-copy frames");

/// @brief Free helper mirroring `AppId::to_string()`.
///
/// @param id Identifier to render.
/// @return The stored bytes as a `std::string`.
/// @note Allocates, so cold path only (project mandate 9).
[[nodiscard]] inline std::string to_string(const AppId& id) {
    return id.to_string();
}

/// @brief Write an identifier as a JSON string.
///
/// @param j Destination value.
/// @param id Identifier to serialize.
/// @note Cold path only (project mandate 10).
inline void to_json(nlohmann::json& j, const AppId& id) {
    j = id.to_string();
}

/// @brief Read an identifier from JSON without throwing.
///
/// @param j Source value; must be a string holding a valid identifier.
/// @param id Destination, left unchanged on invalid or mistyped input.
/// @note Spec `core/app-id`, requirement "Identifier values render and
///       serialize"; safe under `JSON_NOEXCEPTION=1`.
inline void from_json(const nlohmann::json& j, AppId& id) noexcept {
    if (!j.is_string()) {
        return;
    }
    const std::string text = j.get<std::string>();
    if (auto parsed = AppId::create(text); parsed) {
        id = *parsed;
    }
}

} // namespace nexigen::core
