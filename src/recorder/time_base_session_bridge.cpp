// nexigen::recorder — time-base session bridge implementation.
#include <cstddef>
#include <cstring>

#include <nexigen/recorder/session_writer.hpp>
#include <nexigen/recorder/time_base_session_bridge.hpp>
#include <nexigen/recorder/wire_types.hpp>

namespace nexigen::recorder {
namespace {

/// @brief Copy a boot UUID into the fixed payload field.
///
/// @param out Destination buffer.
/// @param uuid Source text.
void set_uuid(std::array<char, 37>& out, std::string_view uuid) noexcept {
    const std::size_t count = uuid.size() < out.size() - 1U ? uuid.size() : out.size() - 1U;
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = uuid[i];
    }
    out[count] = '\0';
}

} // namespace

TimeBaseSessionBridge::TimeBaseSessionBridge(SessionWriter& writer, std::string_view boot_uuid,
                                             std::uint64_t boot_token) noexcept
    : writer_(&writer), boot_token_(boot_token) {
    set_uuid(boot_uuid_, boot_uuid);
}

bool TimeBaseSessionBridge::record(const chrono::TimeBaseBlock& block) noexcept {
    if (writer_ == nullptr || block.generation == 0U || block.generation == last_generation_) {
        return true;
    }
    TimeBasePayload payload{};
    payload.generation = block.generation;
    payload.time_base_epoch = boot_token_;
    payload.boot_token = block.boot_token;
    payload.status = block.status;
    payload.sync_mode = block.sync_mode;
    payload.service_flags = block.service_flags;
    payload.tai_offset_ns = block.tai_offset_ns;
    payload.utc_offset_ns = block.utc_offset_ns;
    payload.utc_unc_ns = block.utc_unc_ns;
    payload.boot_uuid = boot_uuid_;
    if (block.status == chrono::TimeBaseStatus::Lost) {
        // A lost base has no honest rows: record the interval as a gap so the
        // resolver never applies stale maps.
        payload.gap = 1U;
        payload.domain_count = 0U;
    } else {
        payload.domain_count = block.domain_count;
        payload.rows = block.rows;
    }
    if (!writer_->append_time_base(payload).has_value()) {
        return false;
    }
    last_generation_ = block.generation;
    ++entries_;

    if (block.last_snapshot.generation != 0U &&
        block.last_snapshot.generation != last_snapshot_generation_) {
        if (!writer_->append_time_snapshot(block.last_snapshot).has_value()) {
            return false;
        }
        last_snapshot_generation_ = block.last_snapshot.generation;
        ++snapshots_;
    }
    return true;
}

} // namespace nexigen::recorder
