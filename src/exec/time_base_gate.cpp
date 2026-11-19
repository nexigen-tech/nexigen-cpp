// nexigen::exec — time-base gate implementation (change add-time-base-service).
//
// Cold path: attaching a client maps the segment for the duration of the check.
// The supervisor calls this only while a dependency is pending or a retry is
// being decided, never in a tick.
#include <string>
#include <string_view>

#include <nexigen/app/infrastructure_stop_reason.hpp>
#include <nexigen/chrono/time_base_client.hpp>
#include <nexigen/exec/detail/time_base_gate.hpp>

namespace nexigen::exec::detail {

bool time_base_ready(std::string_view segment) noexcept {
    if (segment.empty()) {
        return false;
    }
    const auto client = nexigen::chrono::TimeBaseClient::attach(
        std::string(segment), nexigen::chrono::TimeBaseClientConfig{});
    return client.has_value() && client->status() == nexigen::chrono::TimeBaseStatus::Ready;
}

bool hold_retry_for_time_base(std::string_view segment, bool daemon_running,
                              int exit_code) noexcept {
    if (exit_code != nexigen::app::time_base_lost_exit_code) {
        return false;
    }
    return !daemon_running || !time_base_ready(segment);
}

} // namespace nexigen::exec::detail
