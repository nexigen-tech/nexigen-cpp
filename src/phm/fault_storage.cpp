// nexigen::phm — fault storage implementation (change add-phm).
#include <nexigen/phm/fault_storage.hpp>

#include <chrono>

namespace nexigen::phm {

FaultStorage::FaultStorage(std::string path, core::ThreadSpec spec) : path_(std::move(path)) {
    file_ = std::fopen(path_.c_str(), "a");
    static_cast<void>(worker_.start(spec, Body{this}));
}

FaultStorage::~FaultStorage() {
    worker_.stop();
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

void FaultStorage::record(const FaultRecord& record) noexcept {
    const auto pushed = queue_.try_push(record);
    if (!pushed.has_value()) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
    }
}

void FaultStorage::worker_loop(std::stop_token stop) noexcept {
    FaultRecord record;
    while (!stop.stop_requested()) {
        // Drain everything currently queued, then check shutdown; a short
        // sleep avoids busy-spinning while idle.
        bool drained_any = false;
        while (queue_.try_pop(record).has_value()) {
            drained_any = true;
            if (file_ != nullptr) {
                const std::string_view kind = nexigen::phm::to_string(record.kind);
                std::fprintf(file_, "%.*s %u %u %llu\n", static_cast<int>(kind.size()), kind.data(),
                             record.process_index, record.checkpoint_id,
                             static_cast<unsigned long long>(record.timestamp_ns));
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

} // namespace nexigen::phm
