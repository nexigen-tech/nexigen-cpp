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

#include <nexigen/logging/logger_options.hpp>
#include <nexigen/logging/log_level.hpp>

namespace nexigen::logging::detail {

/// Per-shard anti-spam state (owner-written, consumer-read relaxed).
struct alignas(64) SpamShardState {
    std::uint64_t last_hash{0};          ///< hash of previous message identity
    std::uint32_t repeat_count{0};       ///< consecutive identical messages
    std::uint64_t repeat_window_start{0};///< monotonic ns of window start
    std::uint64_t tokens_used{0};        ///< messages in current second
    std::uint64_t window_start_ns{0};    ///< rate-limit window start
    std::atomic<std::uint64_t> throttled_count{0}; ///< rate-limited drops
    std::uint8_t pad_[16]{};
};

/// Two-level anti-spam: repeat suppression + token-bucket rate limiting.
class SpamProtection {
    SpamProtectionConfig config_{};

public:
    explicit SpamProtection(const SpamProtectionConfig& cfg = {}) noexcept : config_(cfg) {}

    void set_config(const SpamProtectionConfig& cfg) noexcept { config_ = cfg; }
    [[nodiscard]] const SpamProtectionConfig& config() const noexcept { return config_; }

    /// Level 1: repeat suppression. Returns true if the message should be
    /// enqueued (not a repeat). Updates shard state.
    [[nodiscard]] bool check_repeat(SpamShardState& state, std::uint64_t msg_hash,
                                   std::uint64_t now_ns) noexcept {
        if (!config_.enable_repeat_suppression) {
            return true;
        }
        if (state.last_hash == msg_hash && state.repeat_count > 0) {
            // Window expiry: force summary and reset.
            const std::uint64_t window_ns =
                static_cast<std::uint64_t>(config_.repeat_suppression_window_ms) * 1'000'000ULL;
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
        if (level >= config_.bypass_min_level) {
            return true;
        }
        // No limit configured.
        if (config_.max_messages_per_sec_per_shard == 0) {
            return true;
        }
        // Window rollover.
        const std::uint64_t window_ns = 1'000'000'000ULL; // 1 second
        if (state.window_start_ns == 0 || now_ns - state.window_start_ns >= window_ns) {
            state.window_start_ns = now_ns;
            state.tokens_used = 0;
        }
        if (state.tokens_used >= config_.max_messages_per_sec_per_shard) {
            state.throttled_count.fetch_add(1, std::memory_order_relaxed);
            return false; // throttled
        }
        ++state.tokens_used;
        return true;
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "SpamProtection(" + config_.to_string() + ")";
    }
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
    if (file != nullptr) { mix(file, std::strlen(file)); }
    mix(reinterpret_cast<const char*>(&line), sizeof line);
    if (fmt != nullptr) { mix(fmt, std::strlen(fmt)); }
    if (args != nullptr && args_len > 0) { mix(args, args_len); }
    return h;
}

} // namespace nexigen::logging::detail
