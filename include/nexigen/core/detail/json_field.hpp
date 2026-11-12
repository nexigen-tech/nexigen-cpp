// nexigen::core — cold-path JSON field-extraction vocabulary (change
// refactor-complexity-hotspots; spec core/serialization).
//
// Every manifest/config from_json probes nlohmann::json objects the same way:
// find the key, check the type, assign the value, otherwise leave the
// destination untouched. This header carries that probe exactly once so the
// deserializers stay flat and readable.
//
// Constraints (core/serialization): these helpers are noexcept, never throw,
// abort or terminate under JSON_NOEXCEPTION=1, and are cold-path only — they
// allocate std::string/nodes and must never be reached from a producer or
// worker hot path. No hot-path header may include this file.
//
// The bool result reports whether the field was present and accepted; per-field
// readers are tolerant by design, so callers usually ignore it, while the
// collect_* helpers use it to reject a whole document.
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace nexigen::core::detail {

/// @brief Read a string field, assigning only on success.
///
/// @param object JSON object to probe.
/// @param key Field name to look up.
/// @param out Destination, left untouched unless the field exists and is a
///        string.
/// @return `true` when the key exists, holds a string, and `out` was assigned;
///         `false` otherwise.
/// @note Spec `core/serialization`, requirement "Manifest and config
///       deserialization uses a shared field vocabulary".
inline bool read_string(const nlohmann::json& object, const char* key, std::string& out) noexcept {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return false;
    }
    out = it->get<std::string>();
    return true;
}

/// @brief Read a boolean field, assigning only on success.
///
/// @param object JSON object to probe.
/// @param key Field name to look up.
/// @param out Destination, left untouched unless the field exists and is a
///        boolean.
/// @return `true` when the key exists, holds a boolean, and `out` was assigned;
///         `false` otherwise.
/// @note Spec `core/serialization`, requirement "Manifest and config
///       deserialization uses a shared field vocabulary".
inline bool read_bool(const nlohmann::json& object, const char* key, bool& out) noexcept {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_boolean()) {
        return false;
    }
    out = it->get<bool>();
    return true;
}

/// @brief Convert a bare JSON number to a signed destination bounded by `T`.
///
/// Unsigned JSON values are accepted only when they fit in `std::int64_t`;
/// negative and out-of-range values are rejected without aborting.
///
/// @tparam T Signed destination type (constrained by
///         `std::is_signed_v<T>`).
/// @param value JSON value to convert.
/// @param out Destination, assigned only when the value is in range.
/// @return `true` when `value` is a number representable in `T` and `out` was
///         assigned; `false` otherwise.
/// @note Spec `core/serialization`, requirement "Field-extraction helpers are
///       non-throwing and cold-path only".
template <class T> bool to_signed(const nlohmann::json& value, T& out) noexcept {
    static_assert(std::is_signed_v<T>, "to_signed requires a signed destination");
    std::int64_t wide = 0;
    if (value.is_number_unsigned()) {
        const std::uint64_t raw = value.get<std::uint64_t>();
        if (raw > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return false;
        }
        wide = static_cast<std::int64_t>(raw);
    } else if (value.is_number_integer()) {
        wide = value.get<std::int64_t>();
    } else {
        return false;
    }
    if (wide < static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
        wide > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
        return false;
    }
    out = static_cast<T>(wide);
    return true;
}

/// @brief Read a signed field bounded by the destination type.
///
/// @tparam T Signed destination type (constrained by
///         `std::is_signed_v<T>`).
/// @param object JSON object to probe.
/// @param key Field name to look up.
/// @param out Destination, left untouched unless the field is a number in
///        range.
/// @return `true` when the key exists, holds an in-range number, and `out` was
///         assigned; `false` otherwise.
/// @note Spec `core/serialization`, requirement "Manifest and config
///       deserialization uses a shared field vocabulary".
template <class T>
bool read_signed(const nlohmann::json& object, const char* key, T& out) noexcept {
    const auto it = object.find(key);
    return it != object.end() && to_signed(*it, out);
}

/// @brief Convert a bare JSON number to an unsigned destination bounded by `T`.
///
/// A negative, non-integer, or out-of-range value is rejected; nothing aborts.
///
/// @tparam T Unsigned destination type (constrained by
///         `std::is_unsigned_v<T>`).
/// @param value JSON value to convert.
/// @param out Destination, assigned only when the value is in range.
/// @return `true` when `value` is a non-negative integer representable in `T`
///         and `out` was assigned; `false` otherwise.
/// @note Spec `core/serialization`, requirement "Field-extraction helpers are
///       non-throwing and cold-path only".
template <class T> bool to_unsigned(const nlohmann::json& value, T& out) noexcept {
    static_assert(std::is_unsigned_v<T>, "to_unsigned requires an unsigned destination");
    if (!value.is_number_unsigned()) {
        return false;
    }
    const std::uint64_t raw = value.get<std::uint64_t>();
    if (raw > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
        return false;
    }
    out = static_cast<T>(raw);
    return true;
}

/// @brief Read an unsigned field bounded by the destination type.
///
/// A value above `std::numeric_limits<T>::max()` is rejected: the destination
/// is left untouched and nothing aborts.
///
/// @tparam T Unsigned destination type (constrained by
///         `std::is_unsigned_v<T>`).
/// @param object JSON object to probe.
/// @param key Field name to look up.
/// @param out Destination, left untouched unless the field is a non-negative
///        integer in range.
/// @return `true` when the key exists, holds an in-range value, and `out` was
///         assigned; `false` otherwise.
/// @note Spec `core/serialization`, requirement "Manifest and config
///       deserialization uses a shared field vocabulary".
template <class T>
bool read_unsigned(const nlohmann::json& object, const char* key, T& out) noexcept {
    const auto it = object.find(key);
    return it != object.end() && to_unsigned(*it, out);
}

/// @brief Read a nested object and hand it to a callable.
///
/// @tparam Fn Callable invocable as `fn(const nlohmann::json&)`; not required
///         to be `noexcept`, but the helper itself is.
/// @param object JSON object to probe.
/// @param key Field name to look up.
/// @param fn Callable invoked with the nested object when the field is present
///        and an object.
/// @return `true` when the key exists, holds an object, and `fn` was invoked;
///         `false` otherwise (the destination stays untouched).
/// @note Spec `core/serialization`, scenario "Object and array composition
///       keep the current semantics".
template <class Fn>
bool read_object(const nlohmann::json& object, const char* key, Fn&& fn) noexcept {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_object()) {
        return false;
    }
    std::forward<Fn>(fn)(*it);
    return true;
}

/// @brief Walk a present array in document order.
///
/// An absent key or a non-array value is ignored and reported as success,
/// which is how the manifests treat an optional collection.
///
/// @tparam Fn Callable invocable as `fn(const nlohmann::json&) -> bool`.
/// @param object JSON object to probe.
/// @param key Field name to look up.
/// @param fn Visitor for each element; returning `false` rejects the whole
///        document.
/// @return `false` when `fn` rejected an element; `true` otherwise, including
///         for an absent or non-array field.
/// @note Spec `core/serialization`, scenario "Object and array composition
///       keep the current semantics".
template <class Fn>
bool collect_array(const nlohmann::json& object, const char* key, Fn&& fn) noexcept {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array()) {
        return true;
    }
    auto&& callable = fn;
    for (const nlohmann::json& element : *it) {
        if (!callable(element)) {
            return false;
        }
    }
    return true;
}

/// @brief Collect a present array of values into `out`.
///
/// An absent key or a non-array value leaves `out` unchanged and succeeds; a
/// rejected element also leaves `out` unchanged.
///
/// @tparam T Element type collected into the vector.
/// @tparam Convert Callable invocable as
///         `convert(const nlohmann::json&, T&) -> bool`; returning `false`
///         rejects the collection.
/// @param object JSON object to probe.
/// @param key Field name to look up.
/// @param out Destination vector, replaced only on full success.
/// @param convert Per-element converter filling one `T`.
/// @return `true` on success (including an absent or non-array field);
///         `false` when any element was rejected.
/// @note Spec `core/serialization`, scenario "Object and array composition
///       keep the current semantics".
template <class T, class Convert>
bool collect_array_into(const nlohmann::json& object, const char* key, std::vector<T>& out,
                        Convert&& convert) noexcept {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array()) {
        return true;
    }
    auto&& convert_element = convert;
    std::vector<T> parsed;
    parsed.reserve(it->size());
    for (const nlohmann::json& element : *it) {
        T value{};
        if (!convert_element(element, value)) {
            return false;
        }
        parsed.push_back(std::move(value));
    }
    out = std::move(parsed);
    return true;
}

/// @brief Collect a present array of objects into `out`.
///
/// A non-object element rejects the whole collection (returns `false` and
/// leaves `out` unchanged); an absent key or a non-array value leaves `out`
/// unchanged and succeeds.
///
/// @tparam T Element type collected into the vector.
/// @tparam Parse Callable invocable as `parse(const nlohmann::json&, T&)`,
///         filling one element from an object.
/// @param object JSON object to probe.
/// @param key Field name to look up.
/// @param out Destination vector, replaced only when every element parses.
/// @param parse Per-element parser.
/// @return `true` on success (including an absent or non-array field);
///         `false` when a non-object element was found.
/// @note Spec `core/serialization`, scenario "Object and array composition
///       keep the current semantics".
template <class T, class Parse>
bool collect_object_array(const nlohmann::json& object, const char* key, std::vector<T>& out,
                          Parse&& parse) noexcept {
    auto&& fill = parse;
    return collect_array_into<T>(object, key, out,
                                 [&fill](const nlohmann::json& element, T& value) noexcept {
                                     if (!element.is_object()) {
                                         return false;
                                     }
                                     fill(element, value);
                                     return true;
                                 });
}

/// @brief Copy a string field into a fixed-size char buffer.
///
/// Truncates at `capacity` and zero-fills the remainder; an empty JSON string
/// clears the buffer.
///
/// @param object JSON object to probe.
/// @param key Field name to look up.
/// @param destination Caller-owned buffer of at least `capacity` bytes.
/// @param capacity Number of bytes writable in `destination`.
/// @return `true` when the key exists, holds a string, and the buffer was
///         written; `false` for a missing/wrong-typed field or a null buffer.
/// @note Spec `core/serialization`, requirement "Manifest and config
///       deserialization uses a shared field vocabulary".
inline bool read_fixed_string(const nlohmann::json& object, const char* key, char* destination,
                              std::size_t capacity) noexcept {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string() || destination == nullptr) {
        return false;
    }
    const std::string value = it->get<std::string>();
    const std::size_t take = value.size() < capacity ? value.size() : capacity;
    for (std::size_t i = 0; i < take; ++i) {
        destination[i] = value[i];
    }
    for (std::size_t i = take; i < capacity; ++i) {
        destination[i] = '\0';
    }
    return true;
}

/// @brief Copy a string field into a fixed-size byte buffer.
///
/// Truncates at `capacity` and zero-fills the remainder.
///
/// @param object JSON object to probe.
/// @param key Field name to look up.
/// @param destination Caller-owned buffer of at least `capacity` bytes.
/// @param capacity Number of bytes writable in `destination`.
/// @return `true` when the key exists, holds a string, and the buffer was
///         written; `false` for a missing/wrong-typed field or a null buffer.
/// @note Spec `core/serialization`, requirement "Manifest and config
///       deserialization uses a shared field vocabulary".
inline bool read_fixed_bytes(const nlohmann::json& object, const char* key,
                             std::uint8_t* destination, std::size_t capacity) noexcept {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string() || destination == nullptr) {
        return false;
    }
    const std::string value = it->get<std::string>();
    const std::size_t take = value.size() < capacity ? value.size() : capacity;
    for (std::size_t i = 0; i < take; ++i) {
        destination[i] = static_cast<std::uint8_t>(value[i]);
    }
    for (std::size_t i = take; i < capacity; ++i) {
        destination[i] = 0U;
    }
    return true;
}

} // namespace nexigen::core::detail
