// nexigen::phm — declared fork/join region (change close-phm-safety-loop).
#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace nexigen::phm {

/// @brief Maximum number of declared fork regions per process.
///
/// The supervisor keeps a fixed-depth stack of entered-branch bitsets; a
/// manifest declaring more forks than the stack can hold is rejected when the
/// manifest is loaded. This also bounds the runtime nesting depth, because a
/// process can nest at most one frame per declared fork.
inline constexpr std::size_t k_max_logical_forks = 8;

/// @brief One declared fork/join region of a logical supervision graph.
///
/// After `fork_id` arrives, every identifier in `branch_entries` MUST be
/// reached before `join_id`; otherwise the join records a logical failure.
/// Branch entries and the join are checkpoint identifiers that must also
/// appear in `SupervisedProcess::checkpoints`.
///
/// @note Spec `phm/supervision`, requirement "Logical supervision"; Spec
///       `phm/manifest`, requirement "Fork/join configuration".
struct LogicalFork {
    std::uint32_t fork_id{0};                    ///< Checkpoint that opens the region.
    std::vector<std::uint32_t> branch_entries{}; ///< Checkpoints that must each be reached.
    std::uint32_t join_id{0};                    ///< Checkpoint that closes the region.

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return Text naming the fork, its branch entries, and the join.
    /// @note Mandate 9; allocates, so cold path only.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `LogicalFork::to_string()` (mandate 9).
[[nodiscard]] inline std::string to_string(const LogicalFork& fork) {
    return fork.to_string();
}

/// @brief ADL JSON writer for one fork region.
void to_json(nlohmann::json& j, const LogicalFork& fork);

/// @brief ADL JSON reader for one fork region (non-throwing).
void from_json(const nlohmann::json& j, LogicalFork& fork) noexcept;

} // namespace nexigen::phm

/// @brief `std::formatter` for one fork region (project mandate 11).
template <> struct std::formatter<nexigen::phm::LogicalFork> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::phm::LogicalFork& fork, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(fork.to_string(), ctx);
    }
};
