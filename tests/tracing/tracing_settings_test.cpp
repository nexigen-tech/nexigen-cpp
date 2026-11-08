// Split from tracing_test.cpp: nexigen::tracing settings and worker-thread startup.

#include "tracing_test_support.hpp"

namespace {

void test_tracing_worker_thread_settings() {
    char global_path[256];
    char local_path[256];
    std::snprintf(global_path, sizeof global_path, "/tmp/opencode/tracing-worker-global-%d.json",
                  ::getpid());
    std::snprintf(local_path, sizeof local_path, "/tmp/opencode/tracing-worker-local-%d.json",
                  ::getpid());
    auto write = [](const char* path, std::string_view text) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << text;
        NX_CHECK(file.good());
    };
    write(global_path, R"({"core_mask":5,"worker_thread":{"name":"trace-worker",)"
                       R"("policy":"rr","priority":40,"mode":"required"}})");
    write(local_path, R"({"worker_thread":{"cpus":[64,65]}})");

    tracing::Settings merged{};
    NX_CHECK(load_tracing_settings({global_path, local_path}, merged).has_value());
    NX_CHECK(merged.options.worker_thread.name() == "trace-worker");
    NX_CHECK(merged.options.worker_thread.policy() == core::SchedulingPolicy::RR);
    NX_CHECK(merged.options.worker_thread.priority() == 40);
    NX_CHECK(merged.options.worker_thread.apply_mode() == core::ThreadApplyMode::Required);
    NX_CHECK(merged.options.worker_thread.cpus().contains(64U));
    NX_CHECK(merged.options.worker_thread.cpus().contains(65U));
    NX_CHECK(merged.options.worker_thread.cpus().count() == 2U);

    write(local_path, R"({"worker_thread":{"cpus":[]}})");
    NX_CHECK(load_tracing_settings({global_path, local_path}, merged).has_value());
    NX_CHECK(merged.options.worker_thread.cpus().empty());
    NX_CHECK(merged.options.worker_thread.name() == "trace-worker");

    NX_CHECK(::setenv("NEXIGEN_TRACING_CORE_MASK", "3", 1) == 0);
    NX_CHECK(load_tracing_settings({global_path, local_path}, merged).has_value());
    NX_CHECK(merged.options.worker_thread.cpus() == core::CpuSet::from_core_mask(3));
    NX_CHECK(merged.options.worker_thread.name() == "trace-worker");
    NX_CHECK(::unsetenv("NEXIGEN_TRACING_CORE_MASK") == 0);

    nlohmann::json json;
    to_json(json, merged);
    NX_CHECK(json.contains("worker_thread"));
    NX_CHECK(!json.contains("core_mask"));
    NX_CHECK(json["worker_thread"]["name"] == "trace-worker");

    write(local_path, R"({"worker_thread":{"cpus":[64,64]}})");
    NX_CHECK(!load_tracing_settings({global_path, local_path}, merged).has_value());
    write(local_path, R"({"worker_thread":{"policy":"fifo","priority":0}})");
    NX_CHECK(!load_tracing_settings({global_path, local_path}, merged).has_value());

    static_cast<void>(std::remove(global_path));
    static_cast<void>(std::remove(local_path));
}

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
nexigen::core::fault_injection::FaultPoint g_tracing_fault_point =
    nexigen::core::fault_injection::FaultPoint::Thread_set_affinity;
bool tracing_fault_hook(nexigen::core::fault_injection::FaultPoint point) noexcept {
    return point == g_tracing_fault_point;
}
#endif

void test_tracing_required_thread_failure() {
#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
    using Provider = tracing::TracerProvider<64, 32>;
    std::uint64_t allowed = 0ULL;
    NX_CHECK(tracing::detail::self_affinity(allowed).has_value());
    const std::uint64_t core = allowed != 0ULL ? (allowed & (~allowed + 1ULL)) : 0ULL;
    NX_CHECK(core != 0ULL);

    // Required affinity failure joins and resets the worker. The same provider
    // can initialize again once the injected error is removed.
    {
        tracing::NullTraceSink sink{};
        tracing::TracerProviderConfig config{};
        config.worker_thread.set_cpus(core::CpuSet::from_core_mask(core));
        NX_CHECK(config.worker_thread.set_name("trace-worker").has_value());
        config.worker_thread.set_apply_mode(core::ThreadApplyMode::Required);
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider = new (static_cast<void*>(storage)) Provider(config, sink);
        g_tracing_fault_point = nexigen::core::fault_injection::FaultPoint::Thread_set_affinity;
        nexigen::core::fault_injection::set_fault_hook(tracing_fault_hook);
        const Status failed = provider->initialize();
        nexigen::core::fault_injection::set_fault_hook(nullptr);
        NX_CHECK(!failed.has_value());
        NX_CHECK(failed.error() == core::ErrorCode::SystemError);
        NX_CHECK(!provider->worker_started());
        NX_CHECK(provider->flush().error() == core::ErrorCode::NotInitialized);

        NX_CHECK(provider->initialize().has_value());
        for (int i = 0; i < 2000 && !provider->worker_started(); ++i) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        NX_CHECK(provider->worker_started());
        NX_CHECK(provider->worker_start_code() == core::ErrorCode::Ok);
        NX_CHECK(provider->flush().has_value());
        std::destroy_at(provider);
    }

    // Best effort publishes a scheduler failure and continues the pipeline.
    {
        tracing::NullTraceSink sink{};
        tracing::TracerProviderConfig config{};
        NX_CHECK(config.worker_thread.set_scheduler(core::SchedulingPolicy::FIFO, 1).has_value());
        config.worker_thread.set_apply_mode(core::ThreadApplyMode::BestEffort);
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider = new (static_cast<void*>(storage)) Provider(config, sink);
        g_tracing_fault_point = nexigen::core::fault_injection::FaultPoint::Thread_set_scheduler;
        nexigen::core::fault_injection::set_fault_hook(tracing_fault_hook);
        NX_CHECK(provider->initialize().has_value());
        for (int i = 0; i < 2000 && !provider->worker_started(); ++i) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        nexigen::core::fault_injection::set_fault_hook(nullptr);
        NX_CHECK(provider->worker_started());
        NX_CHECK(provider->worker_start_code() == core::ErrorCode::SystemError);
        NX_CHECK(provider->worker_thread_report().failed() ==
                 core::ThreadApplyReport::scheduler_mask);
        NX_CHECK(provider->flush().has_value());
        std::destroy_at(provider);
    }
#else
    std::printf("tracing_required_thread_failure: fault injection disabled; skipped\n");
#endif
}

} // namespace

void suite_tracing_required_thread_failure() {
    run_suite("tracing_required_thread_failure", test_tracing_required_thread_failure);
}

void suite_tracing_worker_thread_settings() {
    run_suite("tracing_worker_thread_settings", test_tracing_worker_thread_settings);
}
