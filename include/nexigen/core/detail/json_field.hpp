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

/// Read a string field. Returns true only when the key exists, holds a string
/// and the destination was assigned.
inline bool read_string(const nlohmann::json& object, const char* key, std::string& out) noexcept {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return false;
    }
    out = it->get<std::string>();
    return true;
}

/// Read a boolean field.
inline bool read_bool(const nlohmann::json& object, const char* key, bool& out) noexcept {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_boolean()) {
        return false;
    }
    out = it->get<bool>();
    return true;
}

/// Convert a bare JSON number to a signed destination bounded by its type.
/// Unsigned JSON values are accepted only when they fit; nothing aborts.
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

/// Read a signed field bounded by the destination type.
template <class T>
bool read_signed(const nlohmann::json& object, const char* key, T& out) noexcept {
    const auto it = object.find(key);
    return it != object.end() && to_signed(*it, out);
}

/// Convert a bare JSON number to an unsigned destination bounded by its type.
/// A non-number or an out-of-range value is rejected (no abort).
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

/// Read an unsigned field bounded by the destination type. A value above
/// std::numeric_limits<T>::max() is rejected (destination untouched, no abort).
template <class T>
bool read_unsigned(const nlohmann::json& object, const char* key, T& out) noexcept {
    const auto it = object.find(key);
    return it != object.end() && to_unsigned(*it, out);
}

/// Read a nested object and hand it to `fn`. Returns false when the key is
/// absent or not an object; the destination object is then left untouched.
template <class Fn>
bool read_object(const nlohmann::json& object, const char* key, Fn&& fn) noexcept {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_object()) {
        return false;
    }
    std::forward<Fn>(fn)(*it);
    return true;
}

/// Walk a present array. `fn(const nlohmann::json&)` returns false to reject the
/// document. An absent key or a non-array value is ignored (returns true), which
/// is how the manifests treat an optional collection.
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

/// Collect a present array into `out`. `convert(const nlohmann::json&, T&)`
/// fills one element and returns false to reject the collection. An absent key or
/// a non-array value leaves `out` unchanged and succeeds; a rejected collection
/// also leaves `out` unchanged.
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

/// Collect a present array of objects into `out`. `parse(const nlohmann::json&, T&)`
/// fills one element. A non-object element rejects the whole collection (returns
/// false and leaves `out` unchanged); an absent key or a non-array value leaves
/// `out` unchanged and succeeds.
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

/// Copy a string field into a fixed-size char buffer, truncating at `capacity`
/// and zero-filling the remainder. Empty JSON strings clear the buffer.
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

/// Copy a string field into a fixed-size byte buffer, truncating at `capacity`
/// and zero-filling the remainder.
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
