// libFuzzer harness for the pure HTTP parsing helpers.
// Built only with -DNEXIGEN_BUILD_FUZZERS=ON (Clang).
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <nexigen/hub/detail/http_parse.hpp>

namespace hub_detail = nexigen::hub::detail;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    if (size == 0U) {
        return 0;
    }
    const std::string_view input(reinterpret_cast<const char*>(data), size);

    hub_detail::Request_line line;
    const hub_detail::Request_line_error error =
        hub_detail::parse_request_line(input, line);
    if (error == hub_detail::Request_line_error::None) {
        if (line.method != "GET" ||
            (line.version != "HTTP/1.0" && line.version != "HTTP/1.1")) {
            __builtin_trap();
        }
    }

    for (const std::string_view name :
         {std::string_view{"Last-Event-ID"}, std::string_view{"Connection"},
          std::string_view{"Host"}}) {
        const std::string_view value = hub_detail::header_value(input, name);
        if (!value.empty() &&
            (value.data() < input.data() ||
             value.data() + value.size() > input.data() + input.size())) {
            __builtin_trap();
        }
    }

    const std::string decoded = hub_detail::url_decode(input);
    if (decoded.size() > input.size()) {
        __builtin_trap();
    }

    const std::string query = hub_detail::query_value(input, "limit");
    if (query.size() > input.size()) {
        __builtin_trap();
    }
    return 0;
}
