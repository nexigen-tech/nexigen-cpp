#include "resource_guard.hpp"

#include <format>

namespace nexigen::canview {

std::string ResourceGuardConfig::to_string() const {
    return std::format("ResourceGuardConfig(cpu_limit={}, entry={}, exit={}, ratio={:.2f}, "
                       "storm={}, window_ns={})",
                       cpu_limit_percent, entry_windows, exit_windows, exit_ratio, storm_rate,
                       window_ns);
}

ResourceMode ResourceGuard::observe(std::uint64_t now_ns, std::uint64_t process_cpu_ns,
                                    std::uint64_t rate) noexcept {
    if (!initialized_) {
        initialized_ = true;
        window_start_ns_ = now_ns;
        window_cpu_ns_ = process_cpu_ns;
        rate_ewma_ = static_cast<double>(rate);
        last_rate_ = rate;
        evaluate();
        return mode_;
    }
    rate_ewma_ = rate_ewma_ * 0.8 + static_cast<double>(rate) * 0.2;
    last_rate_ = rate;

    if (now_ns < window_start_ns_ + config_.window_ns) {
        evaluate();
        return mode_;
    }
    const std::uint64_t wall = now_ns - window_start_ns_;
    const std::uint64_t cpu =
        process_cpu_ns >= window_cpu_ns_ ? process_cpu_ns - window_cpu_ns_ : 0U;
    last_cpu_percent_ =
        wall == 0U ? 0.0 : (static_cast<double>(cpu) * 100.0 / static_cast<double>(wall));
    window_start_ns_ = now_ns;
    window_cpu_ns_ = process_cpu_ns;
    const double limit = static_cast<double>(config_.cpu_limit_percent);
    if (last_cpu_percent_ > limit) {
        ++above_windows_;
        below_windows_ = 0;
    } else {
        above_windows_ = 0;
        if (last_cpu_percent_ < limit * config_.exit_ratio) {
            ++below_windows_;
        } else {
            below_windows_ = 0;
        }
    }
    evaluate();
    return mode_;
}

void ResourceGuard::evaluate() noexcept {
    const bool cpu_entry = above_windows_ >= config_.entry_windows;
    const bool storm_entry =
        last_rate_ > config_.storm_rate || rate_ewma_ > static_cast<double>(config_.storm_rate);
    const bool cpu_exit =
        last_cpu_percent_ < static_cast<double>(config_.cpu_limit_percent) * config_.exit_ratio;
    const bool storm_exit =
        rate_ewma_ < static_cast<double>(config_.storm_rate) * config_.exit_ratio;
    if (mode_ == ResourceMode::Normal) {
        if (cpu_entry && storm_entry) {
            mode_ = ResourceMode::Eco;
            trigger_ = EcoTrigger::Both;
        } else if (cpu_entry) {
            mode_ = ResourceMode::Eco;
            trigger_ = EcoTrigger::Cpu;
        } else if (storm_entry) {
            mode_ = ResourceMode::Eco;
            trigger_ = EcoTrigger::Storm;
        }
        return;
    }
    if (below_windows_ >= config_.exit_windows && storm_exit) {
        mode_ = ResourceMode::Normal;
        trigger_ = EcoTrigger::None;
        above_windows_ = 0;
        below_windows_ = 0;
    } else if (storm_entry && cpu_entry) {
        trigger_ = EcoTrigger::Both;
    } else if (storm_entry) {
        trigger_ = EcoTrigger::Storm;
    } else if (cpu_entry) {
        trigger_ = EcoTrigger::Cpu;
    } else if (cpu_exit && storm_exit) {
        trigger_ = EcoTrigger::None;
    }
}

bool ResourceGuard::recompute_due(std::uint64_t now_ns) noexcept {
    if (input_pending_) {
        input_pending_ = false;
        last_recompute_ns_ = now_ns;
        return true;
    }
    if (mode_ == ResourceMode::Normal) {
        last_recompute_ns_ = now_ns;
        return true;
    }
    if (now_ns >= last_recompute_ns_ + 250'000'000ULL) {
        last_recompute_ns_ = now_ns;
        return true;
    }
    return false;
}

void ResourceGuard::reset() noexcept {
    mode_ = ResourceMode::Normal;
    trigger_ = EcoTrigger::None;
    window_start_ns_ = 0;
    window_cpu_ns_ = 0;
    above_windows_ = 0;
    below_windows_ = 0;
    last_cpu_percent_ = 0.0;
    rate_ewma_ = 0.0;
    last_rate_ = 0;
    initialized_ = false;
    last_recompute_ns_ = 0;
    input_pending_ = false;
}

std::string ResourceGuard::to_string() const {
    return std::format("ResourceGuard(mode={}, trigger={}, cpu={:.2f}%, rate={:.0f})", mode_,
                       trigger_, last_cpu_percent_, rate_ewma_);
}

std::string to_string(const ResourceGuard& guard) {
    return guard.to_string();
}

} // namespace nexigen::canview
