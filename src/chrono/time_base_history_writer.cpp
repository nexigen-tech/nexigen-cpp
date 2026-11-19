// nexigen::chrono — time-base history file writer implementation (change
// add-time-base-service; spec chrono/time-base "Coefficient history").
//
// Cold path: nlohmann::json and std::string are used here on purpose. The
// stream is change-driven (a handful of lines per second at most), so the cost
// is irrelevant and readability of the on-disk contract wins.
#include <cstddef>
#include <string>

#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/time_base_history_writer.hpp>
#include <nexigen/core/sink_io.hpp>

namespace nexigen::chrono {

TimeBaseHistoryWriter::TimeBaseHistoryWriter(const JsonFileSinkConfig& config) noexcept
    : fd_(core::sink_io::open_output(config.path, config.append)) {}

TimeBaseHistoryWriter::~TimeBaseHistoryWriter() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool TimeBaseHistoryWriter::append(const TimeBaseHistoryEntry& entry) noexcept {
    if (fd_ < 0) {
        return false;
    }
    nlohmann::json line = entry;
    line["schema"] = 2;
    std::string text = line.dump();
    text.push_back('\n');
    return core::sink_io::write_all(fd_, text.data(), text.size()).has_value();
}

core::Status TimeBaseHistoryWriter::flush() noexcept {
    if (fd_ < 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    if (!core::sink_io::fsync_fd(fd_).has_value()) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

bool TimeBaseHistoryWriter::append_thunk(void* self, const TimeBaseHistoryEntry& entry) noexcept {
    auto* const writer = static_cast<TimeBaseHistoryWriter*>(self);
    return writer != nullptr && writer->append(entry);
}

std::string TimeBaseHistoryWriter::to_string() const {
    return "TimeBaseHistoryWriter(fd=" + std::to_string(fd_) +
           ", open=" + (is_open() ? "true" : "false") + ")";
}

std::string to_string(const TimeBaseHistoryWriter& writer) {
    return writer.to_string();
}

} // namespace nexigen::chrono
