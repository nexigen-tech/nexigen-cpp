// nexigen::sm — UCM update coordination vocabulary
// (change p3-ucm-coordination; spec sm/ucm-coordination).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/com/concepts/message_concept.hpp>
#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::sm {

/// @brief Length of the name buffers in UCM payloads.
inline constexpr std::size_t k_ucm_name_length = 32;

/// @brief Requested update operation.
enum class UpdateAction : std::uint8_t {
    Start,
    Complete,
    Cancel,
};

/// @brief Outcome of one UCM request.
enum class UpdateResult : std::uint8_t {
    Accepted,
    Rejected,
    Busy,
    InvalidGroup,
    InvalidState,
    EmFailure,
};

/// @brief One UCM update request.
struct UpdateRequest {
    char function_group[k_ucm_name_length]{}; ///< Target Function Group.
    char update_id[k_ucm_name_length]{};      ///< Update identity.
    UpdateAction action{UpdateAction::Start}; ///< Requested update action.
    char target_state[k_ucm_name_length]{};   ///< Complete only; empty = previous state.

    /// @brief Render the request for cold-path diagnostics.
    /// @return Text naming the Function Group, update id, action, and target.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Response to one UCM update request.
struct UpdateResponse {
    UpdateResult result{UpdateResult::Rejected}; ///< Arbitration outcome.
    char function_group[k_ucm_name_length]{};    ///< Function Group the response applies to.
    char active_update_id[k_ucm_name_length]{};  ///< Update currently blocking the group, if any.

    /// @brief Render the response for cold-path diagnostics.
    /// @return Text naming the result, Function Group, and active update id.
    [[nodiscard]] std::string to_string() const;
};

static_assert(com::concepts::Message<UpdateRequest>);
static_assert(com::concepts::Message<UpdateResponse>);

[[nodiscard]] constexpr std::string_view to_string(UpdateAction value) noexcept {
    return core::enum_name(value);
}
[[nodiscard]] constexpr bool from_string(std::string_view name, UpdateAction& out) noexcept {
    return core::enum_value(name, out);
}
[[nodiscard]] constexpr std::string_view to_string(UpdateResult value) noexcept {
    return core::enum_name(value);
}
[[nodiscard]] constexpr bool from_string(std::string_view name, UpdateResult& out) noexcept {
    return core::enum_value(name, out);
}

inline void to_json(nlohmann::json& j, const UpdateAction value) {
    core::mandate::enum_to_json(j, value);
}
inline void from_json(const nlohmann::json& j, UpdateAction& out) noexcept {
    core::mandate::enum_from_json(j, out);
}
inline void to_json(nlohmann::json& j, const UpdateResult value) {
    core::mandate::enum_to_json(j, value);
}
inline void from_json(const nlohmann::json& j, UpdateResult& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

[[nodiscard]] inline std::string to_string(const UpdateRequest& value) {
    return value.to_string();
}
[[nodiscard]] inline std::string to_string(const UpdateResponse& value) {
    return value.to_string();
}

void to_json(nlohmann::json& j, const UpdateRequest& value);
void from_json(const nlohmann::json& j, UpdateRequest& value) noexcept;
void to_json(nlohmann::json& j, const UpdateResponse& value);
void from_json(const nlohmann::json& j, UpdateResponse& value) noexcept;

} // namespace nexigen::sm

namespace nexigen::core {

template <> struct EnumTraits<nexigen::sm::UpdateAction> {
    struct Entry {
        nexigen::sm::UpdateAction value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::sm::UpdateAction::Start, "Start"},
        {nexigen::sm::UpdateAction::Complete, "Complete"},
        {nexigen::sm::UpdateAction::Cancel, "Cancel"},
    }};
};

template <> struct EnumTraits<nexigen::sm::UpdateResult> {
    struct Entry {
        nexigen::sm::UpdateResult value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 6> entries{{
        {nexigen::sm::UpdateResult::Accepted, "Accepted"},
        {nexigen::sm::UpdateResult::Rejected, "Rejected"},
        {nexigen::sm::UpdateResult::Busy, "Busy"},
        {nexigen::sm::UpdateResult::InvalidGroup, "InvalidGroup"},
        {nexigen::sm::UpdateResult::InvalidState, "InvalidState"},
        {nexigen::sm::UpdateResult::EmFailure, "EmFailure"},
    }};
};

} // namespace nexigen::core

template <> struct std::formatter<nexigen::sm::UpdateAction> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::sm::UpdateAction value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::sm::to_string(value), ctx);
    }
};

template <> struct std::formatter<nexigen::sm::UpdateResult> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::sm::UpdateResult value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::sm::to_string(value), ctx);
    }
};

template <> struct std::formatter<nexigen::sm::UpdateRequest> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::sm::UpdateRequest& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};

template <> struct std::formatter<nexigen::sm::UpdateResponse> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::sm::UpdateResponse& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};
