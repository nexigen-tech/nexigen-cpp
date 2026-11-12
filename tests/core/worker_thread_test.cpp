// Tests nexigen::core::WorkerThread and the ThreadSpec fallback helpers
// (change add-worker-thread; spec core/worker-thread).
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <format>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include <nexigen/core/affinity.hpp>
#include <nexigen/core/detail/fault_injection.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/worker_thread.hpp>

namespace core = nexigen::core;

namespace {

int g_checks = 0;
int g_failures = 0;

#define NX_CHECK(...)                                                                              \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(__VA_ARGS__)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
        }                                                                                          \
    } while (false)

void run_suite(const char* name, void (*body)()) {
    const int checks_before = g_checks;
    const int failures_before = g_failures;
    body();
    std::printf("[%-24s] %d checks, %d failed\n", name, g_checks - checks_before,
                g_failures - failures_before);
}

bool wait_flag(const std::atomic<bool>& flag, int milliseconds = 2000) {
    for (int i = 0; i < milliseconds && !flag.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return flag.load(std::memory_order_acquire);
}

bool wait_value(const std::atomic<int>& value, int expected, int milliseconds = 2000) {
    for (int i = 0; i < milliseconds && value.load(std::memory_order_acquire) != expected; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return value.load(std::memory_order_acquire) == expected;
}

template <typename T>
concept HasFromJson = requires(const nlohmann::json& json, T& value) { from_json(json, value); };

struct BasicBody {
    std::atomic<bool>* ran{nullptr};
    std::atomic<int>* count{nullptr};
    std::thread::id seen{};
    std::thread::id* seen_out{nullptr};

    void run(std::stop_token) noexcept {
        seen = std::this_thread::get_id();
        if (seen_out != nullptr) {
            *seen_out = seen;
        }
        if (ran != nullptr) {
            ran->store(true, std::memory_order_release);
        }
        if (count != nullptr) {
            count->fetch_add(1, std::memory_order_relaxed);
        }
    }
};

struct OrderBody {
    std::atomic<int>* phase{nullptr};
    core::Status start_status{};

    core::Status on_start() noexcept {
        phase->store(1, std::memory_order_release);
        return start_status;
    }
    void run(std::stop_token) noexcept { phase->store(2, std::memory_order_release); }
};

struct BlockingBody {
    std::atomic<bool>* ran{nullptr};
    void run(std::stop_token stop) noexcept {
        if (ran != nullptr) {
            ran->store(true, std::memory_order_release);
        }
        while (!stop.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
};

struct CvState {
    std::mutex mutex;
    std::condition_variable_any condition;
    std::atomic<bool> exited{false};
};

struct CvBody {
    CvState* state{nullptr};
    void run(std::stop_token stop) noexcept {
        std::unique_lock<std::mutex> lock(state->mutex);
        state->condition.wait(lock, stop, [] { return false; });
        state->exited.store(true, std::memory_order_release);
    }
};

[[nodiscard]] std::uint64_t first_core_mask() {
    std::uint64_t mask = 0U;
    if (!core::affinity::self_affinity(mask)) {
        return 0U;
    }
    return mask;
}

void test_spec_helpers() {
    core::ThreadSpec empty{};
    NX_CHECK(!empty.has_requests());

    core::ThreadSpec named{};
    NX_CHECK(named.set_name("worker").has_value());
    NX_CHECK(named.has_requests());

    core::ThreadSpec placed{};
    placed.set_cpus(core::CpuSet::from_core_mask(1ULL));
    NX_CHECK(placed.has_requests());

    core::ThreadSpec scheduled{};
    NX_CHECK(scheduled.set_scheduler(core::SchedulingPolicy::Other, 0).has_value());
    NX_CHECK(scheduled.has_requests());

    core::ThreadSpec fallback{};
    NX_CHECK(fallback.set_name("bg").has_value());
    fallback.set_cpus(core::CpuSet::from_core_mask(0b11ULL));
    NX_CHECK(fallback.set_scheduler(core::SchedulingPolicy::FIFO, 10).has_value());

    const core::ThreadSpec effective = core::effective_thread_spec(empty, fallback);
    NX_CHECK(effective == fallback);
    NX_CHECK(!empty.has_requests()); // neither input mutated

    const core::ThreadSpec primary = core::effective_thread_spec(named, fallback);
    NX_CHECK(primary == named);
    NX_CHECK(primary.name() == "worker");

    const core::ThreadSpec none = core::effective_thread_spec(empty, core::ThreadSpec{});
    NX_CHECK(!none.has_requests());
}

void test_basic_lifecycle() {
    std::atomic<bool> ran{false};
    std::atomic<int> count{0};
    std::thread::id child_id{};

    core::WorkerThread<BasicBody> worker{};
    BasicBody body{};
    body.ran = &ran;
    body.count = &count;
    body.seen_out = &child_id;
    NX_CHECK(worker.start(core::ThreadSpec{}, body).has_value());
    NX_CHECK(wait_flag(ran));
    NX_CHECK(count.load() == 1);
    NX_CHECK(child_id != std::this_thread::get_id());
    NX_CHECK(worker.ready());
    NX_CHECK(worker.start_code() == core::ErrorCode::Ok);
    NX_CHECK(worker.joinable());

    worker.stop();
    NX_CHECK(!worker.joinable());
    NX_CHECK(worker.ready()); // readiness snapshot survives stop
    worker.stop();            // idempotent
    worker.join();
}

void test_double_start_and_destruction() {
    std::atomic<bool> ran{false};
    {
        core::WorkerThread<BlockingBody> worker{};
        BlockingBody body{};
        body.ran = &ran;
        NX_CHECK(worker.start(core::ThreadSpec{}, body).has_value());
        NX_CHECK(wait_flag(ran));
        const core::Status second = worker.start(core::ThreadSpec{}, body);
        NX_CHECK(!second.has_value());
        NX_CHECK(second.error() == core::ErrorCode::Conflict);
    } // destructor requests stop and joins

    NX_CHECK(ran.load());
}

void test_on_start_order_and_failure() {
    std::atomic<int> phase{0};
    core::WorkerThread<OrderBody> worker{};
    OrderBody body{};
    body.phase = &phase;
    NX_CHECK(worker.start(core::ThreadSpec{}, body).has_value());
    NX_CHECK(wait_value(phase, 2));
    NX_CHECK(worker.start_code() == core::ErrorCode::Ok);
    worker.stop();

    std::atomic<int> failing_phase{0};
    std::atomic<bool> ran{false};
    struct FailingBody {
        std::atomic<int>* phase{nullptr};
        std::atomic<bool>* ran{nullptr};
        core::Status on_start() noexcept {
            phase->store(1, std::memory_order_release);
            return std::unexpected(core::ErrorCode::SystemError);
        }
        void run(std::stop_token) noexcept {
            phase->store(2, std::memory_order_release);
            ran->store(true, std::memory_order_release);
        }
    };
    core::WorkerThread<FailingBody> failing{};
    FailingBody failing_body{};
    failing_body.phase = &failing_phase;
    failing_body.ran = &ran;
    NX_CHECK(failing.start(core::ThreadSpec{}, failing_body).has_value());
    NX_CHECK(wait_flag(ran)); // on_start failure does not suppress run
    NX_CHECK(failing.start_code() == core::ErrorCode::SystemError);
    failing.stop();
}

[[maybe_unused]] bool fifo_is_denied() {
    core::ThreadSpec spec{};
    if (!spec.set_scheduler(core::SchedulingPolicy::FIFO, 1).has_value()) {
        return true;
    }
    spec.set_apply_mode(core::ThreadApplyMode::Required);
    core::ThreadApplyReport report{};
    bool denied = false;
    std::jthread probe([&] { denied = !core::apply_thread_spec_self(spec, report).has_value(); });
    probe.join();
    return denied;
}

void test_required_failure() {
    bool denied = false;
#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
    const auto hook = [](core::fault_injection::FaultPoint point) noexcept {
        return point == core::fault_injection::FaultPoint::Thread_set_scheduler;
    };
    core::fault_injection::set_fault_hook(hook);
    denied = true;
#else
    denied = fifo_is_denied();
#endif
    if (!denied) {
        std::printf("required-failure path skipped: FIFO is permitted in this environment\n");
        return;
    }

    core::ThreadSpec spec{};
    NX_CHECK(spec.set_scheduler(core::SchedulingPolicy::FIFO, 1).has_value());
    spec.set_apply_mode(core::ThreadApplyMode::Required);
    std::atomic<bool> ran{false};
    core::WorkerThread<BasicBody> worker{};
    BasicBody body{};
    body.ran = &ran;
    const core::Status started = worker.start(spec, body);
    NX_CHECK(!started.has_value());
    NX_CHECK(started.error() == core::ErrorCode::SystemError);
    NX_CHECK(!ran.load());
    NX_CHECK(worker.start_code() == core::ErrorCode::SystemError);
    NX_CHECK(worker.report().first_error() == core::ErrorCode::SystemError);
    NX_CHECK((worker.report().failed() & core::ThreadApplyReport::scheduler_mask) != 0U);
    NX_CHECK(!worker.joinable());

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
    core::fault_injection::set_fault_hook(nullptr);
#endif
}

void test_best_effort_partial() {
    bool denied = false;
#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
    const auto hook = [](core::fault_injection::FaultPoint point) noexcept {
        return point == core::fault_injection::FaultPoint::Thread_set_scheduler;
    };
    core::fault_injection::set_fault_hook(hook);
    denied = true;
#else
    denied = fifo_is_denied();
#endif
    if (!denied) {
        std::printf("best-effort partial path skipped: FIFO is permitted in this environment\n");
        return;
    }

    core::ThreadSpec spec{};
    NX_CHECK(spec.set_scheduler(core::SchedulingPolicy::FIFO, 1).has_value());
    spec.set_apply_mode(core::ThreadApplyMode::BestEffort);
    std::atomic<bool> ran{false};
    core::WorkerThread<BasicBody> worker{};
    BasicBody body{};
    body.ran = &ran;
    NX_CHECK(worker.start(spec, body).has_value());
    NX_CHECK(wait_flag(ran)); // partial application still runs the body
    NX_CHECK(worker.start_code() == core::ErrorCode::SystemError);
    NX_CHECK((worker.report().failed() & core::ThreadApplyReport::scheduler_mask) != 0U);
    worker.stop();

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
    core::fault_injection::set_fault_hook(nullptr);
#endif
}

void test_stop_wakes_cv() {
    CvState state{};
    core::WorkerThread<CvBody> worker{};
    CvBody body{};
    body.state = &state;
    NX_CHECK(worker.start(core::ThreadSpec{}, body).has_value());
    for (int i = 0; i < 2000 && !worker.ready(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    NX_CHECK(worker.ready());
    worker.request_stop();
    worker.join();
    NX_CHECK(state.exited.load());
    NX_CHECK(!worker.joinable());
}

void test_cpu_mask() {
    const std::uint64_t available = first_core_mask();
    if (available == 0U) {
        return;
    }
    std::uint64_t bit = 1ULL;
    while ((available & bit) == 0U && bit != 0ULL) {
        bit <<= 1U;
    }
    if (bit == 0ULL) {
        return;
    }
    core::ThreadSpec spec{};
    spec.set_cpus(core::CpuSet::from_core_mask(bit));
    std::atomic<bool> ran{false};
    core::WorkerThread<BasicBody> worker{};
    BasicBody body{};
    body.ran = &ran;
    NX_CHECK(worker.start(spec, body).has_value());
    NX_CHECK(wait_flag(ran));
    NX_CHECK((worker.mask() & bit) == bit);
    worker.stop();
}

void test_diagnostics() {
    std::atomic<bool> ran{false};
    using Worker = core::WorkerThread<BasicBody>;
    static_assert(!HasFromJson<Worker>, "live WorkerThread must not have from_json");

    Worker worker{};
    BasicBody body{};
    body.ran = &ran;
    NX_CHECK(worker.start(core::ThreadSpec{}, body).has_value());
    NX_CHECK(wait_flag(ran));

    const std::string text = worker.to_string();
    NX_CHECK(text.find("WorkerThread") != std::string::npos);
    NX_CHECK(text.find("start_code=Ok") != std::string::npos);
    NX_CHECK(core::to_string(worker) == text);
    NX_CHECK(std::format("{}", worker) == text);

    nlohmann::json json{};
    core::to_json(json, worker);
    NX_CHECK(json.contains("spec"));
    NX_CHECK(json.contains("ready"));
    NX_CHECK(json.contains("start_code"));
    NX_CHECK(json.contains("mask"));
    NX_CHECK(json.contains("report"));
    NX_CHECK(json["report"].contains("requested"));
    NX_CHECK(json["report"].contains("applied"));
    NX_CHECK(json["report"].contains("failed"));
    NX_CHECK(json["report"].contains("rollback_failed"));
    NX_CHECK(json["report"].contains("first_error"));
    worker.stop();
}

void test_probe_helper() {
    std::uint64_t before = 0U;
    NX_CHECK(core::affinity::self_affinity(before).has_value());

    NX_CHECK(core::probe_thread_spec(core::ThreadSpec{}).has_value());

    bool denied = false;
#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
    const auto hook = [](core::fault_injection::FaultPoint point) noexcept {
        return point == core::fault_injection::FaultPoint::Thread_set_scheduler;
    };
    core::fault_injection::set_fault_hook(hook);
    denied = true;
#else
    denied = fifo_is_denied();
#endif
    if (denied) {
        core::ThreadSpec required{};
        NX_CHECK(required.set_scheduler(core::SchedulingPolicy::FIFO, 1).has_value());
        required.set_apply_mode(core::ThreadApplyMode::Required);
        NX_CHECK(!core::probe_thread_spec(required).has_value());

        core::ThreadSpec best_effort = required;
        best_effort.set_apply_mode(core::ThreadApplyMode::BestEffort);
        NX_CHECK(core::probe_thread_spec(best_effort).has_value());
    }

    std::uint64_t after = 0U;
    NX_CHECK(core::affinity::self_affinity(after).has_value());
    NX_CHECK(before == after); // the probe applies attributes on its own thread

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
    core::fault_injection::set_fault_hook(nullptr);
#endif
}

void test_worker_thread() {
    test_spec_helpers();
    test_basic_lifecycle();
    test_double_start_and_destruction();
    test_on_start_order_and_failure();
    test_required_failure();
    test_best_effort_partial();
    test_stop_wakes_cv();
    test_cpu_mask();
    test_diagnostics();
    test_probe_helper();
}

} // namespace

int main() {
    run_suite("worker_thread", test_worker_thread);
    std::printf("total: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
