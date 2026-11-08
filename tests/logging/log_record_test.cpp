// Split from log_record_test.cpp: nexigen::logging LogRecord and deferred formatting.

#include "logging_test_support.hpp"

namespace {

template <class... Args> [[nodiscard]] std::string rendered(const char* lit, Args&&... args) {
    LogRecord<128> rec{};
    [[maybe_unused]] constexpr auto offs =
        detail::arg_offsets<128U, std::remove_cvref_t<Args>...>();
    constexpr std::size_t total = detail::arg_total<128U, std::remove_cvref_t<Args>...>();
    static_assert(total <= 128U, "harness pack must fit");
    std::size_t i = 0;
    std::uint8_t flags = 0;
    ((detail::pack_one<128U>(rec.arg_payload_.data(), offs[i++], args, flags)), ...);
    rec.fmt = lit;
    rec.arg_length = static_cast<std::uint16_t>(total);
    rec.render = &detail::render_impl<128U, std::remove_cvref_t<Args>...>;
    rec.flags = flags;
    char out[1024];
    const std::size_t n = rec.render_into(out, sizeof out);
    return std::string(out, n);
}

void test_record() {
    // Scalars, floats, enums, timestamps render exactly as std::format does.
    NX_CHECK(rendered("plain text") == "plain text");
    NX_CHECK(rendered("v {} {}", 42, 3.5) == std::format("v {} {}", 42, 3.5));
    NX_CHECK(rendered("Level is {}", LogLevel::Warning) == "Level is warning");
    NX_CHECK(rendered("ec {}", ErrorCode::BufferFull) == "ec BufferFull");
    NX_CHECK(rendered("rot {}", RotationSchedule::Daily) == "rot daily");
    NX_CHECK(rendered("t {}", Timestamp{1234ULL}) == "t 1234");
    NX_CHECK(rendered("Прямой вызов метода debug. Давление: {:.4f}", 0.0025) ==
             "Прямой вызов метода debug. Давление: 0.0025");
    NX_CHECK(rendered("mixed {} {} {:.2}", 1, std::string_view("sv"), 0.125) ==
             std::format("mixed {} {} {:.2}", 1, std::string_view("sv"), 0.125));

    // string_view args are COPIED into the blob: the source may die (spec R5).
    std::string text_view;
    {
        const std::string ephemeral = "transient payload";
        text_view = rendered("{}", std::string_view{ephemeral});
    }
    NX_CHECK(text_view == "transient payload");

    // Long string arguments truncate the inline copy and flag bit0.
    const std::string huge(500U, 'x');
    LogRecord<128> rec{};
    rec.fmt = "{}";
    std::uint8_t flags = 0;
    detail::pack_one<128U>(rec.arg_payload_.data(), 0U, std::string_view{huge}, flags);
    rec.arg_length = static_cast<std::uint16_t>(detail::arg_total<128U, std::string_view>());
    rec.render = &detail::render_impl<128U, std::string_view>;
    rec.flags = flags;
    NX_CHECK(rec.args_truncated());
    char out[1024];
    const std::size_t n = rec.render_into(out, sizeof out);
    NX_CHECK(n == 64U); // inline cap = MessageCapacity/2
    NX_CHECK(std::all_of(out, out + n, [](char c) { return c == 'x'; }));

    // Render overflow: bounded output + bit1, never half-emitted downstream.
    rec.render_into(out, 8U);
    NX_CHECK(rec.render_truncated());

    // Default record: no callback → empty message (no UB, no crash).
    LogRecord<128> empty_rec{};
    NX_CHECK(empty_rec.render_into(out, sizeof out) == 0U);

    // to_string dump (mandate 9): level, location triplet, fmt text, accounting.
    rec.file_name = __FILE__;
    rec.function_name = "test_record";
    rec.line = 42U;
    const std::string dump = rec.to_string();
    NX_CHECK(dump.find("LogRecord(") == 0U);
    NX_CHECK(dump.find("level=info") != std::string::npos);
    NX_CHECK(dump.find("log_record_test.cpp:42") != std::string::npos);
    NX_CHECK(dump.find("fmt={}") != std::string::npos);
    NX_CHECK(dump.find("test_record") != std::string::npos);
    NX_CHECK(to_string(rec) == dump);
    NX_CHECK(LogRecord<128>::message_capacity() == 128U);
}

} // namespace

void suite_record() {
    run_suite("record", test_record);
}
