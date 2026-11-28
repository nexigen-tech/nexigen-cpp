// nexigen::canopen::observability::detail — hot-path error suppression (change
// add-canopen-observability; spec canopen/observability; design D5/D6).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

#include <nexigen/canopen/observability/can_open_error_domain.hpp>
#include <nexigen/canopen/observability/can_open_log_policy.hpp>
#include <nexigen/canopen/observability/detail/episode_gate.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>

namespace nexigen::canopen::observability::detail {

/// @brief Bounded error-episode tracker implementing hot-path policy A.
///
/// The gate owns `node_capacity * domain_capacity` episode slots plus a global
/// repeat budget and per-domain summary windows. It never allocates, formats
/// text, takes a lock, or touches an atomic: every operation is a fixed-size
/// compare over the preallocated array, and the injected logger decides how the
/// packed record is delivered.
///
/// Policy:
///
/// 1. The caller records the metric before calling `on_error`; the gate never
///    suppresses metric recording.
/// 2. An idle key opens an episode and emits one record at
///    `hot_error_level`.
/// 3. A repeat inside `repeat_interval_ms` increments the suppressed count
///    without logging; reaching `max_suppressed_per_key` forces one record.
/// 4. A changed code emits immediately and updates the stored code.
/// 5. `poll_recovery` closes an episode after `recovery_hold_ms` without an
///    error and emits one record at `recovery_level` with the suppressed count.
/// 6. First-error, code-change, and recovery records bypass the global budget;
///    repeat records consume it.
/// 7. A record filtered by `logger.level()` leaves the episode not-logged; the
///    metric path is unaffected.
///
/// Node identifiers outside `0..127` fold onto the fixed array by masking the
/// low seven bits. The runtime is the single writer, so the class is neither
/// thread-safe nor lock-free by contract: it is single-threaded by design.
///
/// @tparam Logger Logger satisfying `logging::concepts::Logger`; its record
///         methods must not throw, because the gate is `noexcept`.
/// @note Spec `canopen/observability`, requirement "Hot-path error logging
///       follows episode policy".
/// @note Spec `canopen/observability`, requirement "Suppression defaults and
///       global budget".
/// @note Spec `canopen/observability`, requirement "Error tracking is
///       fixed-size".
template <logging::concepts::Logger Logger> class ErrorGate {
public:
    /// @brief Number of tracked node slots (CANopen node ids 0..127).
    static constexpr std::size_t node_capacity = 128U;
    /// @brief Number of tracked error domains.
    static constexpr std::size_t domain_capacity = error_domain_count;

    /// @brief Outcome of one `on_error()` call.
    enum class ErrorAction : std::uint8_t {
        None,            ///< Repeat suppressed inside the interval and budget.
        FirstOccurrence, ///< Episode opened (a record may have been emitted).
        Repeat,          ///< Periodic repeat record emitted.
        ChangedCode,     ///< Error code changed; a record was emitted.
        ForcedRepeat,    ///< Suppressed threshold reached; a record was emitted.
    };

    /// @brief Outcome of one `poll()` call.
    struct PollResult {
        std::size_t recovered{0}; ///< Episodes closed by recovery.
        std::size_t summaries{0}; ///< Domain summary records emitted.
    };

    /// @brief Bind a policy and a logger.
    ///
    /// @param policy Policy copied into the gate; must already be validated.
    /// @param logger Borrowed logger that must outlive the gate.
    /// @note Cold path only.
    ErrorGate(const CanOpenLogPolicy& policy, Logger& logger) noexcept
        : policy_(policy), logger_(logger), budget_tokens_(policy.max_hot_logs_per_second) {}

    /// @brief Record one hot-path error and apply the episode policy.
    ///
    /// The caller MUST record the Tier 1 metric first; the gate only decides
    /// whether a log record is emitted.
    ///
    /// @param domain Subsystem that produced the error.
    /// @param node Node identifier; only the low seven bits are used.
    /// @param code Domain-specific error code.
    /// @param now_ns Monotonic timestamp in nanoseconds.
    /// @return The action taken by the gate.
    /// @note Hot path: no allocation, locking, or formatting.
    ErrorAction on_error(CanOpenErrorDomain domain, std::uint8_t node, std::uint32_t code,
                         std::uint64_t now_ns) noexcept {
        EpisodeGate& gate = slot(domain, node);
        DomainWindow& window = windows_[index_of(domain)];
        ++window.errors;
        window.last_activity_ns = now_ns;

        if (gate.active == 0U) {
            gate = EpisodeGate{};
            gate.active = 1U;
            gate.last_code = code;
            gate.last_error_ns = now_ns;
            gate.episode_start_ns = now_ns;
            if (level_enabled(policy_.hot_error_level)) {
                logger_.log(policy_.hot_error_level,
                            "canopen error episode start domain={} node={} code=0x{:08x}", domain,
                            static_cast<unsigned>(node), code);
                gate.logged = 1U;
                gate.last_log_ns = now_ns;
            }
            return ErrorAction::FirstOccurrence;
        }

        gate.last_error_ns = now_ns;
        if (code != gate.last_code) {
            const std::uint32_t previous = gate.last_code;
            gate.last_code = code;
            if (level_enabled(policy_.hot_error_level)) {
                logger_.log(policy_.hot_error_level,
                            "canopen error code change domain={} node={} code=0x{:08x} "
                            "previous=0x{:08x}",
                            domain, static_cast<unsigned>(node), code, previous);
                gate.logged = 1U;
                gate.last_log_ns = now_ns;
            }
            return ErrorAction::ChangedCode;
        }

        ++gate.suppressed;
        ++window.suppressed;
        if (!level_enabled(policy_.hot_error_level)) {
            return ErrorAction::None;
        }
        if (gate.suppressed >= policy_.max_suppressed_per_key) {
            if (!take_repeat_token(now_ns)) {
                return ErrorAction::None;
            }
            log_repeat(domain, node, code, gate);
            gate.suppressed = 0U;
            gate.logged = 1U;
            gate.last_log_ns = now_ns;
            return ErrorAction::ForcedRepeat;
        }
        if (now_ns - gate.last_log_ns >= to_ns(policy_.repeat_interval_ms)) {
            if (!take_repeat_token(now_ns)) {
                return ErrorAction::None;
            }
            log_repeat(domain, node, code, gate);
            gate.suppressed = 0U;
            gate.logged = 1U;
            gate.last_log_ns = now_ns;
            return ErrorAction::Repeat;
        }
        return ErrorAction::None;
    }

    /// @brief Close every episode that stayed quiet for `recovery_hold_ms`.
    ///
    /// Recovery records bypass the global repeat budget. A filtered recovery
    /// record still closes the episode and counts as recovered.
    ///
    /// @param now_ns Monotonic timestamp in nanoseconds.
    /// @return Number of episodes closed.
    /// @note Same single-threaded contract as `on_error`.
    std::size_t poll_recovery(std::uint64_t now_ns) noexcept {
        std::size_t recovered = 0U;
        for (std::size_t node = 0U; node < node_capacity; ++node) {
            for (std::size_t domain = 0U; domain < domain_capacity; ++domain) {
                EpisodeGate& gate = gates_[node][domain];
                if (gate.active == 0U) {
                    continue;
                }
                if (now_ns - gate.last_error_ns < to_ns(policy_.recovery_hold_ms)) {
                    continue;
                }
                if (level_enabled(policy_.recovery_level)) {
                    logger_.log(policy_.recovery_level,
                                "canopen error episode recovered domain={} node={} "
                                "code=0x{:08x} suppressed={}",
                                static_cast<CanOpenErrorDomain>(domain),
                                static_cast<unsigned>(node), gate.last_code, gate.suppressed);
                }
                ++windows_[domain].recovered;
                gate = EpisodeGate{};
                ++recovered;
            }
        }
        return recovered;
    }

    /// @brief Emit one summary record per active domain after the interval.
    ///
    /// Nothing is emitted when no error was observed since the previous
    /// summary. Emitted domains are reset; the timestamp advances whenever the
    /// window has activity, even when the records are filtered by level.
    ///
    /// @param now_ns Monotonic timestamp in nanoseconds.
    /// @return Number of summary records emitted.
    std::size_t emit_summary(std::uint64_t now_ns) noexcept {
        if (now_ns - last_summary_ns_ < to_ns(policy_.summary_interval_ms)) {
            return 0U;
        }
        if (!summary_window_active()) {
            return 0U;
        }
        last_summary_ns_ = now_ns;
        std::size_t emitted = 0U;
        for (std::size_t domain = 0U; domain < domain_capacity; ++domain) {
            DomainWindow& window = windows_[domain];
            if (window.errors == 0U && window.suppressed == 0U) {
                continue;
            }
            if (level_enabled(policy_.summary_level)) {
                logger_.log(policy_.summary_level,
                            "canopen error summary domain={} errors={} suppressed={} "
                            "recovered={}",
                            static_cast<CanOpenErrorDomain>(domain), window.errors,
                            window.suppressed, window.recovered);
                ++emitted;
            }
            window = DomainWindow{};
        }
        return emitted;
    }

    /// @brief Run recovery and summary polling in one call.
    ///
    /// The runtime thread calls this once per cycle or control tick.
    ///
    /// @param now_ns Monotonic timestamp in nanoseconds.
    /// @return Recovered episodes and emitted summaries.
    PollResult poll(std::uint64_t now_ns) noexcept {
        const std::size_t recovered = poll_recovery(now_ns);
        const std::size_t summaries = emit_summary(now_ns);
        return PollResult{recovered, summaries};
    }

    /// @brief Read one episode slot without mutating it.
    ///
    /// @param domain Subsystem of the error.
    /// @param node Node identifier; only the low seven bits are used.
    /// @return The current slot state.
    /// @note Diagnostic/test access only.
    [[nodiscard]] const EpisodeGate& episode(CanOpenErrorDomain domain,
                                             std::uint8_t node) const noexcept {
        return gates_[node_index(node)][index_of(domain)];
    }

    /// @brief Policy copied at construction.
    ///
    /// @return Immutable reference to the gate's policy.
    [[nodiscard]] const CanOpenLogPolicy& policy() const noexcept { return policy_; }

    /// @brief Repeat records rejected by the global budget so far.
    ///
    /// @return Counter of budget denials.
    [[nodiscard]] std::uint64_t repeat_budget_drops() const noexcept { return budget_drops_; }

    /// @brief Cold-path diagnostic string.
    ///
    /// @return Active episode count and budget denials.
    /// @note Mandate 9; allocates.
    [[nodiscard]] std::string to_string() const {
        std::size_t active = 0U;
        for (const auto& row : gates_) {
            for (const EpisodeGate& gate : row) {
                if (gate.active != 0U) {
                    ++active;
                }
            }
        }
        return std::format("ErrorGate(active_episodes={}, budget_drops={})", active, budget_drops_);
    }

private:
    /// @brief Per-domain summary window.
    struct DomainWindow {
        std::uint64_t errors{0};           ///< Errors observed in the window.
        std::uint64_t suppressed{0};       ///< Suppressed occurrences in the window.
        std::uint64_t recovered{0};        ///< Episodes recovered in the window.
        std::uint64_t last_activity_ns{0}; ///< Last error timestamp.
    };

    /// @brief Fold a node identifier onto the fixed array.
    ///
    /// @param node Node id.
    /// @return Index in `[0, node_capacity)`.
    [[nodiscard]] static std::size_t node_index(std::uint8_t node) noexcept {
        return static_cast<std::size_t>(node) & (node_capacity - 1U);
    }

    /// @brief Mutable access to one episode slot.
    ///
    /// @param domain Error domain.
    /// @param node Node identifier.
    /// @return Reference to the `(node, domain)` slot.
    [[nodiscard]] EpisodeGate& slot(CanOpenErrorDomain domain, std::uint8_t node) noexcept {
        return gates_[node_index(node)][index_of(domain)];
    }

    /// @brief Index of a domain.
    ///
    /// @param domain Domain enumerator.
    /// @return Index in `[0, domain_capacity)`.
    [[nodiscard]] static std::size_t index_of(CanOpenErrorDomain domain) noexcept {
        return static_cast<std::size_t>(domain);
    }

    /// @brief Convert milliseconds to nanoseconds without overflow.
    ///
    /// @param milliseconds Bounded policy value (at most one hour).
    /// @return Equivalent nanoseconds.
    [[nodiscard]] static constexpr std::uint64_t to_ns(std::uint32_t milliseconds) noexcept {
        return static_cast<std::uint64_t>(milliseconds) * 1'000'000ULL;
    }

    /// @brief Whether the logger accepts a record at `level`.
    ///
    /// `Off` is a rendering, not a reachable record level: a policy that sets
    /// it disables that record class even when the logger threshold is lower.
    ///
    /// @param level Record severity.
    /// @return `true` when the threshold is at or below `level` and `level` is
    ///         not `Off`.
    [[nodiscard]] bool level_enabled(core::LogLevel level) const noexcept {
        if (level == core::LogLevel::Off) {
            return false;
        }
        return logger_.level() <= level;
    }

    /// @brief Consume one repeat-log token, refilling once per second.
    ///
    /// @param now_ns Monotonic timestamp in nanoseconds.
    /// @return `true` when a token was available.
    [[nodiscard]] bool take_repeat_token(std::uint64_t now_ns) noexcept {
        if (now_ns - budget_window_start_ns_ >= 1'000'000'000ULL) {
            budget_window_start_ns_ = now_ns;
            budget_tokens_ = policy_.max_hot_logs_per_second;
        }
        if (budget_tokens_ == 0U) {
            ++budget_drops_;
            return false;
        }
        --budget_tokens_;
        return true;
    }

    /// @brief Emit one repeat record with the suppressed count.
    ///
    /// @param domain Error domain.
    /// @param node Node identifier.
    /// @param code Error code.
    /// @param gate Slot providing the suppressed count.
    void log_repeat(CanOpenErrorDomain domain, std::uint8_t node, std::uint32_t code,
                    const EpisodeGate& gate) noexcept {
        logger_.log(policy_.hot_error_level,
                    "canopen error repeat domain={} node={} code=0x{:08x} suppressed={}", domain,
                    static_cast<unsigned>(node), code, gate.suppressed);
    }

    /// @brief Whether any domain observed activity since the last summary.
    ///
    /// @return `true` when at least one domain has errors or suppressed counts.
    [[nodiscard]] bool summary_window_active() const noexcept {
        for (const DomainWindow& window : windows_) {
            if (window.errors != 0U || window.suppressed != 0U) {
                return true;
            }
        }
        return false;
    }

    /// @brief Copied policy.
    CanOpenLogPolicy policy_{};
    /// @brief Borrowed logger.
    Logger& logger_;
    /// @brief Fixed episode storage: `[node][domain]`.
    std::array<std::array<EpisodeGate, domain_capacity>, node_capacity> gates_{};
    /// @brief Per-domain summary windows.
    std::array<DomainWindow, domain_capacity> windows_{};
    /// @brief Tokens left in the current one-second budget window.
    std::uint32_t budget_tokens_{0U};
    /// @brief Start of the current budget window.
    std::uint64_t budget_window_start_ns_{0U};
    /// @brief Repeat records rejected by the budget.
    std::uint64_t budget_drops_{0U};
    /// @brief Last summary emission.
    std::uint64_t last_summary_ns_{0U};
};

} // namespace nexigen::canopen::observability::detail
