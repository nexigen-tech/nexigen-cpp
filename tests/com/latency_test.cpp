// Тесты nexigen::com — микробенч латентности горячего пути логгера поверх
// iceoryx2 (change bench-log-iceoryx2-latency; design D1-D6). Сборка только
// при NEXIGEN_ENABLE_COM=ON.
//
// Меряет три варианта транспорта против базы ~20 нс внутрипроцессного
// SPSC-ring (tests/logging/latency_test.cpp):
//   (a)        loan_uninit -> write_payload -> send
//   (a+event)  + EventNotifier::notify()  (полный путь REQ_LOGC_005: два сервиса)
//   (b)        worker-publish: продюсер try_push в SPSC-ring, воркер
//              pop -> loan/write_payload/send/notify (обе стороны отдельно)
//
// Вывод: медиана/p99/p99.9/max. Строгий гейт (только Release x86-64) ассертит
// sanity-потолок (a+event медиана <= 2 мкс) — защита от катастрофы, НЕ порог
// приёмки: порог решается отдельным change после получения цифр.
//
// Проверка нуля аллокаций: heap_in_use() (mallinfo2) снимается до и после
// измерительного окна. Транспортный путь — Rust (iceoryx2), его аллокации
// идут через системный malloc, поэтому счётчик уровня кучи, а не override
// operator new, покрывает и C++, и Rust. Настройка (open_or_create) и первый
// вызов аллоцируют — они выполняются до прогрева и вне измеряемого окна.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <thread>

#include <malloc.h>
#include <pthread.h>
#include <x86intrin.h> // __rdtsc — an intrinsic, not a language extension

#include <nexigen/com/concepts/message_concept.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/spsc_ring_buffer.hpp>

namespace com = nexigen::com;
namespace core = nexigen::core;

namespace {

int g_checks = 0;
int g_failures = 0;

#define NX_CHECK(...)                                                                              \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(__VA_ARGS__)) {                                                                      \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
        }                                                                                          \
    } while (false)

void run_suite(const char* name, void (*body)()) {
    const int checks_before = g_checks;
    const int failures_before = g_failures;
    body();
    std::printf("[%-22s] %d checks, %d failed\n", name, g_checks - checks_before,
                g_failures - failures_before);
}

// Heap in use (bytes handed to malloc and not freed). Противопоставлен
// override-счётчику логирования: для iceoryx2 (Rust через FFI) важна именно
// куча на уровне malloc, а не C++ operator new.
[[nodiscard]] std::size_t heap_in_use() noexcept {
    return static_cast<std::size_t>(mallinfo2().uordblks);
}

// Payload-заглушка: 256-байтовый фрейм фиксированного размера, кратный
// кэш-линии, тривиально-копируемый (удовлетворяет com::concepts::Message).
struct alignas(64) LogFrame256 {
    std::array<std::uint8_t, 256> bytes{};
};
static_assert(sizeof(LogFrame256) == 256);
static_assert(alignof(LogFrame256) == 64);
static_assert(com::concepts::Message<LogFrame256>);

// Порядковая статистика отсортированного массива тиков: медиана, p99, p99.9
// и максимум в наносекундах, с вычетом накладных расходов измерения.
struct LatencyStats {
    double median_ns{};
    double p99_ns{};
    double p999_ns{};
    double max_ns{};
};

[[nodiscard]] LatencyStats stats_of(std::uint64_t* ticks, int n, double ns_per_tick,
                                    double ovh_ns) {
    std::sort(ticks, ticks + n);
    const auto at = [&](int i) {
        return (static_cast<double>(ticks[i]) - ovh_ns) * ns_per_tick;
    };
    const int i50 = n / 2;
    const int i99 = static_cast<int>(static_cast<double>(n) * 0.99);
    const int i999 = static_cast<int>(static_cast<double>(n) * 0.999);
    return LatencyStats{at(i50), at(i99), at(i999), at(n - 1)};
}

void print_stats(const char* label, const LatencyStats& s) {
    std::printf("latency: %-28s median %.1f ns  p99 %.1f ns  p99.9 %.1f ns  max %.1f ns\n", label,
                s.median_ns, s.p99_ns, s.p999_ns, s.max_ns);
}

void test_latency() {
#if defined(__x86_64__)
    // --- Размещение: producer/consumer на разные разрешённые ядра ---
    std::size_t lo = CPU_SETSIZE;
    std::size_t hi = CPU_SETSIZE;
    cpu_set_t orig{};
    bool pinned = false;
    if (::pthread_getaffinity_np(pthread_self(), sizeof(orig), &orig) == 0) {
        for (std::size_t c = 0; c < static_cast<std::size_t>(CPU_SETSIZE); ++c) {
            if (CPU_ISSET(c, &orig)) {
                if (lo == CPU_SETSIZE) {
                    lo = c;
                }
                hi = c;
            }
        }
        if (lo != CPU_SETSIZE && hi != lo) {
            cpu_set_t mine{};
            CPU_SET(lo, &mine);
            pinned = ::pthread_setaffinity_np(pthread_self(), sizeof(mine), &mine) == 0;
        }
    }

    // --- Транспорт: один узел, издатель и нотификатор (без подписчика) ---
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto pub_name = com::ServiceName::create("Nexigen/Test/LatencyLog");
    NX_CHECK(pub_name.has_value());
    auto publisher = com::Publisher<LogFrame256>::create(*node, *pub_name);
    NX_CHECK(publisher.has_value());
    const auto ev_name = com::ServiceName::create("Nexigen/Test/LatencyLogEvent");
    NX_CHECK(ev_name.has_value());
    auto notifier = com::EventNotifier::create(*node, *ev_name);
    NX_CHECK(notifier.has_value());

    LogFrame256 frame{};

    // --- Калибровка ns/tick (контрольная плоскость, 50 мс окно) ---
    const std::uint64_t t_start = __rdtsc();
    const auto m_start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto m_end = std::chrono::steady_clock::now();
    const std::uint64_t t_end = __rdtsc();
    const double ns_per_tick =
        static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(m_end - m_start)
                                .count()) /
        static_cast<double>(t_end - t_start);

    constexpr int WARMUP = 2'000;
    constexpr int N = 10'000;
    alignas(64) static std::uint64_t raw[N];
    alignas(64) static std::uint64_t ovr[N];

    // Прогрев: первый loan/send/notify аллоцирует ленивые пути iceoryx2.
    for (int i = 0; i < WARMUP; ++i) {
        auto loaned = publisher->loan_uninit();
        auto sample = loaned->write_payload(frame);
        static_cast<void>(publisher->send(std::move(sample)));
        static_cast<void>(notifier->notify());
    }

    // Накладные расходы измерения (пустые пары rdtsc).
    for (int i = 0; i < N; ++i) {
        const std::uint64_t r0 = __rdtsc();
        const std::uint64_t r1 = __rdtsc();
        ovr[i] = r1 - r0;
    }
    std::sort(ovr, ovr + N);
    const double ovh_ns = static_cast<double>(ovr[N / 2]) * ns_per_tick;

#if defined(NEXIGEN_COM_STRICT_LATENCY) && !defined(NX_SANITIZER_BUILD)
    constexpr int passes = 3;
#else
    constexpr int passes = 1;
#endif

    const std::size_t heap_before = heap_in_use();

    double best_a_median = -1.0;
    double best_ae_median = -1.0;
    LatencyStats best_a{};
    LatencyStats best_ae{};
    for (int pass = 0; pass < passes; ++pass) {
        // (a) прямой: loan -> write_payload -> send.
        for (int i = 0; i < N; ++i) {
            const std::uint64_t r0 = __rdtsc();
            auto loaned = publisher->loan_uninit();
            auto sample = loaned->write_payload(frame);
            static_cast<void>(publisher->send(std::move(sample)));
            const std::uint64_t r1 = __rdtsc();
            raw[i] = r1 - r0;
        }
        const LatencyStats a = stats_of(raw, N, ns_per_tick, ovh_ns);
        if (best_a_median < 0.0 || a.median_ns < best_a_median) {
            best_a_median = a.median_ns;
            best_a = a;
        }

        // (a+event): полный путь REQ_LOGC_005 (+ notify).
        for (int i = 0; i < N; ++i) {
            const std::uint64_t r0 = __rdtsc();
            auto loaned = publisher->loan_uninit();
            auto sample = loaned->write_payload(frame);
            static_cast<void>(publisher->send(std::move(sample)));
            static_cast<void>(notifier->notify());
            const std::uint64_t r1 = __rdtsc();
            raw[i] = r1 - r0;
        }
        const LatencyStats ae = stats_of(raw, N, ns_per_tick, ovh_ns);
        if (best_ae_median < 0.0 || ae.median_ns < best_ae_median) {
            best_ae_median = ae.median_ns;
            best_ae = ae;
        }
    }

    // Ноль аллокаций на чистом транспортном пути (a + a+event): до/после
    // измерительного окна, после прогрева. Вариант (b) ниже не входит в это
    // окно — создание std::jthread аллоцирует и зашумляет сигнал.
    const std::size_t heap_after = heap_in_use();
    NX_CHECK(heap_after == heap_before);

    // (b) worker-publish: продюсер try_push (локально), воркер pop -> полный
    // транспорт. Ёмкость 16384 > N, чтобы кольцо не переполнялось и счёт
    // pop был честным. Обе стороны меряются на своих ядрах отдельно.
    core::SpscRingBuffer<LogFrame256, 16384> ring{};
    std::atomic<bool> stop{false};
    std::uint64_t producer_raw[N];
    std::uint64_t worker_raw[N];
    std::uint64_t worker_count = 0;
    std::jthread consumer([&] {
        if (pinned) {
            cpu_set_t s{};
            CPU_SET(hi, &s);
            static_cast<void>(::pthread_setaffinity_np(pthread_self(), sizeof(s), &s));
        }
        LogFrame256 rec{};
        while (!stop.load(std::memory_order_relaxed) || !ring.is_empty()) {
            if (ring.try_pop(rec)) {
                const std::uint64_t r0 = __rdtsc();
                auto loaned = publisher->loan_uninit();
                auto sample = loaned->write_payload(rec);
                static_cast<void>(publisher->send(std::move(sample)));
                static_cast<void>(notifier->notify());
                const std::uint64_t r1 = __rdtsc();
                if (worker_count < N) {
                    worker_raw[worker_count++] = r1 - r0;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        }
    });

    for (int i = 0; i < N; ++i) {
        const std::uint64_t r0 = __rdtsc();
        static_cast<void>(ring.try_push(frame));
        const std::uint64_t r1 = __rdtsc();
        producer_raw[i] = r1 - r0;
    }
    stop.store(true, std::memory_order_relaxed);
    consumer.join();

    if (pinned) {
        static_cast<void>(::pthread_setaffinity_np(pthread_self(), sizeof(orig), &orig));
    }

    const LatencyStats b_producer = stats_of(producer_raw, N, ns_per_tick, ovh_ns);
    const LatencyStats b_worker = stats_of(worker_raw, static_cast<int>(worker_count), ns_per_tick,
                                           ovh_ns);

    // Корректность: все N продюсер-сэмплов прошли через кольцо и воркер.
    NX_CHECK(worker_count == static_cast<std::uint64_t>(N));

    print_stats("(a) direct loan/send", best_a);
    print_stats("(a+event) +notify", best_ae);
    print_stats("(b) producer try_push", b_producer);
    print_stats("(b) worker loan+send+notify", b_worker);
    std::printf("latency: best-of-%d passes, ns/tick %.4f, meas-overhead %.2f ns, heap-delta %zd B\n",
                passes, ns_per_tick, ovh_ns, static_cast<std::ptrdiff_t>(heap_after - heap_before));

#if defined(NEXIGEN_COM_STRICT_LATENCY) && !defined(NX_SANITIZER_BUILD)
    // Sanity-потолок (защита от катастрофы, НЕ порог приёмки): полный путь
    // (a+event) не должен выходить за 2 мкс — вдвое над WCET из REQ_LOGC_006
    // (1.5 мкс). Реальный порог введёт отдельный change после цифр.
    NX_CHECK(best_ae_median <= 2000.0);
#endif
#else
    // Не-x86: accounting-only прогон (без rdtsc) — корректность bounded-time.
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/LatencyLog");
    NX_CHECK(name.has_value());
    auto publisher = com::Publisher<LogFrame256>::create(*node, *name);
    NX_CHECK(publisher.has_value());
    LogFrame256 frame{};
    for (int i = 0; i < 10'000; ++i) {
        auto loaned = publisher->loan_uninit();
        auto sample = loaned->write_payload(frame);
        NX_CHECK(publisher->send(std::move(sample)).has_value());
    }
    const std::string text = publisher->to_string();
    NX_CHECK(text.find("sent=10000") != std::string::npos);
    std::printf("latency: accounting-only run (non-x86), sent=10000\n");
#endif
}

} // namespace

int main() {
    run_suite("latency", test_latency);
    std::printf("TOTAL %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
