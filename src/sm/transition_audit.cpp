// nexigen::sm — transition audit log implementation (change add-sm).
#include <nexigen/sm/transition_audit.hpp>

#include <chrono>

namespace nexigen::sm {

TransitionAudit::TransitionAudit(std::string path, core::ThreadSpec spec) : path_(std::move(path)) {
    file_ = std::fopen(path_.c_str(), "a");
    static_cast<void>(worker_.start(spec, Body{this}));
}

TransitionAudit::~TransitionAudit() {
    worker_.stop();
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

void TransitionAudit::record(const TransitionRecord& record) noexcept {
    const auto pushed = queue_.try_push(record);
    if (!pushed.has_value()) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
    }
}

void TransitionAudit::worker_loop(std::stop_token stop) noexcept {
    TransitionRecord record;
    while (!stop.stop_requested()) {
        bool drained_any = false;
        while (queue_.try_pop(record).has_value()) {
            drained_any = true;
            if (file_ != nullptr) {
                std::fprintf(file_, "%llu %s %s->%s by %s: %.*s\n",
                             static_cast<unsigned long long>(record.timestamp_ns),
                             record.function_group, record.old_state, record.new_state,
                             record.requester_id,
                             static_cast<int>(nexigen::sm::to_string(record.result).size()),
                             nexigen::sm::to_string(record.result).data());
            }
            recorded_.fetch_add(1, std::memory_order_relaxed);
        }
        if (stop.stop_requested()) {
            return;
        }
        if (!drained_any) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

} // namespace nexigen::sm
