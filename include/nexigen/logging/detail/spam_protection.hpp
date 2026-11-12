// nexigen::logging — two-level hot-path anti-spam (change
// rework-logging-hot-path-engine, design D5; spec logging/spam-protection).
//
// Level 1: repeat suppression — collapse consecutive identical messages per
// shard by comparing source_location + format pointer + arg_payload_ bytes.
// Level 2: per-shard token-bucket rate limiter with bypass_min_level.
//
// All checks are wait-free (one relaxed load + compare). No mutex, spin, or
// CAS loop on the hot path.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <nexigen/logging/detail/spam_shard_state.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/spam_protection_config.hpp>

namespace nexigen::logging::detail {

/// @brief Two-level anti-spam state: repeat suppression plus token-bucket limiting.
///
/// One instance is shared by every producer of one logger; the per-shard mutable
/// state lives in `SpamShardState`, so all checks here are wait-free (a relaxed
/// load plus a compare). The hot-path quota fields are per-field atomics
/// (design D5, change fix-p1-races-process-correctness): `set_config` may run
/// concurrently with producers, and every consumed field reads as a whole
/// old-or-new value (field-atomic, eventually consistent; never byte-torn) with
/// no mutex and no stall.
///
/// @note Spec `logging/spam-protection`, requirement "Dynamic quota updates are
///       race-free"; the former "wait-free reconfiguration" claim is replaced by
///       these honest per-field semantics.
class SpamProtection {
public:
    /// @brief Construct with a published quota snapshot.
    ///
    /// @param cfg Initial quotas copied field by field; the default
    ///        `SpamProtectionConfig` is the documented engine default.
    explicit SpamProtection(const SpamProtectionConfig& cfg = {}) noexcept
        : enable_repeat_suppression_(cfg.enable_repeat_suppression),
          repeat_suppression_window_ms_(cfg.repeat_suppression_window_ms),
          max_messages_per_sec_per_shard_(cfg.max_messages_per_sec_per_shard),
          bypass_min_level_(cfg.bypass_min_level) {}

    /// @brief Publish new quotas for concurrent producers (control plane).
    ///
    /// Writer side of the runtime reconfiguration: one relaxed store per field.
    /// A producer concurrently loading any single field observes a whole
    /// old-or-new value; a per-field boundary mixture within one emit call is
    /// tolerated and documented in the requirement text.
    ///
    /// @param cfg New quotas to publish field by field.
    /// @note Spec `logging/spam-protection`, requirement "Dynamic quota updates
    ///       are race-free".
    void set_config(const SpamProtectionConfig& cfg) noexcept {
        enable_repeat_suppression_.store(cfg.enable_repeat_suppression, std::memory_order_relaxed);
        repeat_suppression_window_ms_.store(cfg.repeat_suppression_window_ms,
                                            std::memory_order_relaxed);
        max_messages_per_sec_per_shard_.store(cfg.max_messages_per_sec_per_shard,
                                              std::memory_order_relaxed);
        bypass_min_level_.store(cfg.bypass_min_level, std::memory_order_relaxed);
    }

    /// @brief Test whether repeat suppression is armed.
    ///
    /// Hot-path guard: true only when repeat suppression is enabled. The
    /// producer skips the roughly 70 ns message-hash computation when this is
    /// false (the default), because `check_repeat` would short-circuit and never
    /// read the hash.
    ///
    /// @return `true` when repeat suppression is currently enabled.
    /// @note Spec `logging/spam-protection`, requirement "Repeat suppression
    ///       collapses identical messages".
    [[nodiscard]] bool repeat_suppression_enabled() const noexcept {
        return enable_repeat_suppression_.load(std::memory_order_relaxed);
    }

    /// @brief Snapshot the currently published quotas.
    ///
    /// Cold-path diagnostic: loads each field once. A concurrent `set_config`
    /// can yield a per-field valid mixture, never a torn field.
    ///
    /// @return Copy of the published quotas.
    [[nodiscard]] SpamProtectionConfig config() const noexcept {
        SpamProtectionConfig cfg{};
        cfg.enable_repeat_suppression = enable_repeat_suppression_.load(std::memory_order_relaxed);
        cfg.repeat_suppression_window_ms =
            repeat_suppression_window_ms_.load(std::memory_order_relaxed);
        cfg.max_messages_per_sec_per_shard =
            max_messages_per_sec_per_shard_.load(std::memory_order_relaxed);
        cfg.bypass_min_level = bypass_min_level_.load(std::memory_order_relaxed);
        return cfg;
    }

    /// @brief Level 1: decide whether a message is a repeat to suppress.
    ///
    /// Compares the message identity hash with the shard's previous hash and
    /// updates the shard's repeat tracking. Window expiry forces a summary
    /// emission and restarts the repeat window; a changed message resets the
    /// tracking and lets the caller emit the pending summary.
    ///
    /// @param state Shard-owned mutable anti-spam state; owner thread only.
    /// @param msg_hash Hash of source location, format pointer and packed args.
    /// @param now_ns Monotonic timestamp of the record, in nanoseconds.
    /// @return `true` when the message should be enqueued, `false` when it is a
    ///         suppressed duplicate.
    /// @warning `state` is single-writer per shard: only the shard's owner thread
    ///          may call this.
    /// @note Spec `logging/spam-protection`, requirement "Repeat suppression
    ///       collapses identical messages".
    [[nodiscard]] bool check_repeat(SpamShardState& state, std::uint64_t msg_hash,
                                    std::uint64_t now_ns) noexcept {
        if (!enable_repeat_suppression_.load(std::memory_order_relaxed)) {
            return true;
        }
        if (state.last_hash == msg_hash && state.repeat_count > 0) {
            // Window expiry: force summary and reset.
            const std::uint64_t window_ns =
                static_cast<std::uint64_t>(
                    repeat_suppression_window_ms_.load(std::memory_order_relaxed)) *
                1'000'000ULL;
            if (window_ns > 0 && now_ns - state.repeat_window_start >= window_ns) {
                state.repeat_count = 1;
                state.repeat_window_start = now_ns;
                return true; // emit summary + new message
            }
            ++state.repeat_count;
            return false; // suppressed
        }
        // Different message: reset repeat tracking.
        if (state.repeat_count > 0) {
            state.repeat_count = 0; // caller emits summary
        }
        state.last_hash = msg_hash;
        state.repeat_count = 1;
        state.repeat_window_start = now_ns;
        return true;
    }

    /// @brief Take a pending repeat summary and clear the counter.
    ///
    /// @param state Shard-owned mutable anti-spam state; owner thread only.
    /// @param count_out Receives the number of suppressed repeats on success;
    ///        untouched when no summary is pending.
    /// @return `true` when `count_out` was filled, `false` when the repeat count
    ///         is at most one.
    /// @note Spec `logging/spam-protection`, requirement "Repeat suppression
    ///       collapses identical messages"; the emitted line is
    ///       `[SUPPRESSED] Last message repeated N times`.
    [[nodiscard]] bool should_emit_summary(SpamShardState& state,
                                           std::uint32_t& count_out) noexcept {
        if (state.repeat_count > 1) {
            count_out = state.repeat_count;
            state.repeat_count = 0;
            return true;
        }
        return false;
    }

    /// @brief Level 2: per-shard token bucket, deciding whether a record passes.
    ///
    /// Records at or above `bypass_min_level` always pass; a zero quota means no
    /// limit. Otherwise a one-second window is rolled over and the record is
    /// compared against `max_messages_per_sec_per_shard`; a rejected record
    /// increments the shard's `throttled_count` and returns `false`.
    ///
    /// @param state Shard-owned mutable anti-spam state; owner thread only.
    /// @param level Severity of the record being emitted.
    /// @param now_ns Monotonic timestamp of the record, in nanoseconds.
    /// @return `true` when the record may continue through the pipeline,
    ///         `false` when it was throttled (already counted).
    /// @warning `state` is single-writer per shard: only the shard's owner thread
    ///          may call this.
    /// @note Spec `logging/spam-protection`, requirement "Per-shard token-bucket
    ///       rate limiter"; wait-free, one atomic read plus a compare.
    [[nodiscard]] bool check_rate_limit(SpamShardState& state, core::LogLevel level,
                                        std::uint64_t now_ns) noexcept {
        // bypass_min_level: always pass.
        if (level >= bypass_min_level_.load(std::memory_order_relaxed)) {
            return true;
        }
        // No limit configured.
        if (const std::uint32_t max_per_second =
                max_messages_per_sec_per_shard_.load(std::memory_order_relaxed);
            max_per_second == 0) {
            return true;
        }
        // Window rollover.
        const std::uint64_t window_ns = 1'000'000'000ULL; // 1 second
        if (state.window_start_ns == 0 || now_ns - state.window_start_ns >= window_ns) {
            state.window_start_ns = now_ns;
            state.tokens_used = 0;
        }
        if (const std::uint32_t max_per_second =
                max_messages_per_sec_per_shard_.load(std::memory_order_relaxed);
            state.tokens_used >= max_per_second) {
            state.throttled_count.fetch_add(1, std::memory_order_relaxed);
            return false; // throttled
        }
        ++state.tokens_used;
        return true;
    }

    /// @brief Render the quotas as a cold-path diagnostic string.
    ///
    /// @return Text of the form `SpamProtection(<quota dump>)`.
    /// @note Project mandate 9 (cold-path `to_string`); the snapshot is only
    ///       per-field consistent, never a linearized view of all quotas.
    [[nodiscard]] std::string to_string() const {
        return "SpamProtection(" + config().to_string() + ")";
    }

private:
    /// Flag read by the hot-path guard in `repeat_suppression_enabled()`.
    std::atomic<bool> enable_repeat_suppression_;
    /// Repeat window length in milliseconds; 0 disables window expiry.
    std::atomic<std::uint32_t> repeat_suppression_window_ms_;
    /// Per-shard messages allowed per second; 0 means no limit.
    std::atomic<std::uint32_t> max_messages_per_sec_per_shard_;
    /// Minimum level that always bypasses the token bucket.
    std::atomic<core::LogLevel> bypass_min_level_;
};

/// @brief Free cold-path helper mirroring the member rendering.
///
/// @param sp Anti-spam state to render.
/// @return `sp.to_string()`.
/// @note Project mandate 9 (free `to_string` beside the member).
[[nodiscard]] inline std::string to_string(const SpamProtection& sp) {
    return sp.to_string();
}

/// @brief Compute the identity hash of one message for repeat suppression.
///
/// FNV-1a over the source file, line, format string and packed argument bytes;
/// a serial dependent-multiply chain costing roughly 70 ns, which is why the
/// producer calls it only when suppression is armed. Null pointers are skipped,
/// so a synthesized record hashes deterministically from its non-null parts.
///
/// @param file Source file name from `std::source_location` (may be null).
/// @param line Source line number.
/// @param fmt Format-string pointer (may be null).
/// @param args Packed argument bytes (may be null).
/// @param args_len Number of valid bytes in `args`.
/// @return 64-bit FNV-1a hash of the identity fields.
/// @note Spec `logging/spam-protection`, requirement "Repeat suppression
///       collapses identical messages"; wait-free and allocation-free.
[[nodiscard]] inline std::uint64_t message_hash(const char* file, std::uint32_t line,
                                                const char* fmt, const char* args,
                                                std::uint16_t args_len) noexcept {
    std::uint64_t h = 14695981039346656037ULL; // FNV-1a offset
    auto mix = [&h](const char* p, std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i) {
            h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(p[i]));
            h *= 1099511628211ULL;
        }
    };
    if (file != nullptr) {
        mix(file, std::strlen(file));
    }
    mix(reinterpret_cast<const char*>(&line), sizeof line);
    if (fmt != nullptr) {
        mix(fmt, std::strlen(fmt));
    }
    if (args != nullptr && args_len > 0) {
        mix(args, args_len);
    }
    return h;
}

} // namespace nexigen::logging::detail
