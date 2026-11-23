// Тесты общих констант ёмкости лог-сервиса (change
// fix-log-subscriber-capacity). Сборка без NEXIGEN_ENABLE_COM: заголовок
// обязан оставаться COM-free, потому что его включает config-TU nexigen_logging.
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include <nexigen/logging/log_service_capacity.hpp>

namespace {

int g_checks = 0;
int g_failures = 0;

std::string read_text(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

#define NX_CHECK(...)                                                                              \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(__VA_ARGS__)) {                                                                      \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
        }                                                                                          \
    } while (false)

} // namespace

int main() {
    using namespace nexigen::logging;
    static_assert(kLogServiceMaxSubscribers == 4U);
    static_assert(kLogServiceMaxPublishers == 64U);
    static_assert(kLogServiceMaxNodes == 64U);
    static_assert(kLogServiceBufferDepth == 16384U);
    static_assert(kLogServiceHistoryDepth == 4U);
    static_assert(kLogServiceSafeOverflow);

    NX_CHECK(kLogServiceMaxSubscribers == 4U);
    NX_CHECK(kLogServiceMaxPublishers == 64U);
    NX_CHECK(kLogServiceMaxNodes == 64U);
    NX_CHECK(kLogServiceBufferDepth == 16384U);
    NX_CHECK(kLogServiceHistoryDepth == 4U);
    NX_CHECK(kLogServiceSafeOverflow);
    // The depth must address every subscriber buffer with one u64 sample count.
    NX_CHECK(kLogServiceBufferDepth < (std::uint64_t{1} << 32U));

#ifdef NEXIGEN_SOURCE_DIR
    // Keep the documented table in sync with the constants (verification
    // Suggestion 4): a wrong number in the article now fails this test.
    const std::string docs =
        read_text(std::string(NEXIGEN_SOURCE_DIR) + "/docs/articles/logging.md");
    NX_CHECK(!docs.empty());
    NX_CHECK(contains(docs, "| `max_subscribers` | 4 |"));
    NX_CHECK(contains(docs, "| `max_publishers` | 64 |"));
    NX_CHECK(contains(docs, "| `max_nodes` | 64 |"));
    NX_CHECK(contains(docs, "| `subscriber_max_buffer_size` | 16384 |"));
    NX_CHECK(contains(docs, "| `history_size` | 4 |"));
    NX_CHECK(contains(docs, "| `enable_safe_overflow` | `true` |"));

    const std::string architecture =
        read_text(std::string(NEXIGEN_SOURCE_DIR) + "/docs/architecture.md");
    NX_CHECK(!architecture.empty());
    NX_CHECK(contains(architecture, "16 384"));
    NX_CHECK(contains(architecture, "log_service_options()"));
#endif

    std::printf("[log_service_capacity] %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
