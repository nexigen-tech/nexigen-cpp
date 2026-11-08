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
#include <nexigen/logging/log_level.hpp>
#include <nexigen/logging/spam_protection_config.hpp>

namespace nexigen::logging::detail {

/// Two-level anti-spam: repeat suppression + token-bucket rate limiting.
/// The hot-path quota fields are per-field atomics (design D5, change
/// fix-p1-races-process-correctness): `set_config` may run concurrently with
/// producers — every consumed field reads as a whole old-or-new value
/// (field-atomic, eventually consistent; never byte-torn), no mutex, no stall.
/// The former "wait-free reconfiguration" claim is replaced by these honest
/// semantics (spec logging/spam-protection "Dynamic quota updates are
/// race-free").
class SpamProtection {
public:
    explicit SpamProtection(const SpamProtectionConfig& cfg = {}) noexcept
        : enable_repeat_suppression_(cfg.enable_repeat_suppression),
          repeat_suppression_window_ms_(cfg.repeat_suppression_window_ms),
          max_messages_per_sec_per_shard_(cfg.max_messages_per_sec_per_shard),
          bypass_min_level_(cfg.bypass_min_level) {}

    /// Writer side (runtime reconfiguration): one relaxed store per field.
    /// A producer concurrently loading any single field observes a whole
    /// old-or-new value; a per-field boundary mixture within one emit call is
    /// tolerated and documented in the requirement text.
    void set_config(const SpamProtectionConfig& cfg) noexcept {
        enable_repeat_suppression_.store(cfg.enable_repeat_suppression, std::memory_order_relaxed);
        repeat_suppression_window_ms_.store(cfg.repeat_suppression_window_ms,
                                            std::memory_order_relaxed);
        max_messages_per_sec_per_shard_.store(cfg.max_messages_per_sec_per_shard,
                                              std::memory_order_relaxed);
        bypass_min_level_.store(cfg.bypass_min_level, std::memory_order_relaxed);
    }

    /// Hot-path guard: true only when repeat suppression is armed. The
    /// producer skips the ~70 ns message-hash computation when this is false
    /// (the default) — `check_repeat` would short-circuit and never read it.
    [[nodiscard]] bool repeat_suppression_enabled() const noexcept {
        return enable_repeat_suppression_.load(std::memory_order_relaxed);
    }

    /// Snapshot view of the currently published quotas (cold-path diagnostic;
    /// a concurrent set_config yields a per-field valid mixture).
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

    /// Level 1: repeat suppression. Returns true if the message should be
    /// enqueued (not a repeat). Updates shard state.
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

    /// Check if a repeat summary should be emitted (and get the count).
    [[nodiscard]] bool should_emit_summary(SpamShardState& state,
                                           std::uint32_t& count_out) noexcept {
        if (state.repeat_count > 1) {
            count_out = state.repeat_count;
            state.repeat_count = 0;
            return true;
        }
        return false;
    }

    /// Level 2: token-bucket rate limiter. Returns true if the message passes.
    [[nodiscard]] bool check_rate_limit(SpamShardState& state, LogLevel level,
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

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "SpamProtection(" + config().to_string() + ")";
    }

private:
    std::atomic<bool> enable_repeat_suppression_;
    std::atomic<std::uint32_t> repeat_suppression_window_ms_;
    std::atomic<std::uint32_t> max_messages_per_sec_per_shard_;
    std::atomic<LogLevel> bypass_min_level_;
};

/// Free helper (mandate 9).
[[nodiscard]] inline std::string to_string(const SpamProtection& sp) {
    return sp.to_string();
}

/// Compute a simple hash of message identity (source_location + fmt + args).
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
