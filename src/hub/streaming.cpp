// nexigen::hub — bounded tailers, replay rings, and subscribers
// (change nexigen-hub; spec hub/streaming).
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nexigen/chrono/time_format.hpp>
#include <nexigen/hub/model.hpp>
#include <nexigen/hub/streaming.hpp>

namespace nexigen::hub {

namespace {

constexpr std::size_t record_overhead_bytes = 64;

[[nodiscard]] std::string utc_now_rfc3339() {
    using namespace std::chrono;
    const auto since_epoch = system_clock::now().time_since_epoch();
    const std::int64_t epoch_ns =
        duration_cast<nanoseconds>(since_epoch).count();
    char buffer[31]{};
    nexigen::chrono::format_utc_iso8601_ns(epoch_ns, buffer);
    return std::string(buffer);
}

} // namespace

std::string Stream_limits::to_string() const {
    return std::format(
        "Stream_limits(ring_records={}, ring_bytes={}, max_line_bytes={}, "
        "read_chunk_bytes={}, subscriber_bytes={})",
        ring_records, ring_bytes, max_line_bytes, read_chunk_bytes,
        subscriber_bytes);
}

Stream_manager::Stream_manager(Stream_limits limits) noexcept : limits_(limits) {
    if (limits_.ring_records == 0) {
        limits_.ring_records = 1;
    }
    if (limits_.ring_bytes < 256) {
        limits_.ring_bytes = 256;
    }
    if (limits_.max_line_bytes < 64) {
        limits_.max_line_bytes = 64;
    }
    if (limits_.read_chunk_bytes < 1024) {
        limits_.read_chunk_bytes = 1024;
    }
    if (limits_.subscriber_bytes < 1024) {
        limits_.subscriber_bytes = 1024;
    }
}

Stream_manager::~Stream_manager() {
    for (auto& [id, tailer] : tailers_) {
        static_cast<void>(id);
        if (tailer.fd >= 0) {
            static_cast<void>(::close(tailer.fd));
        }
    }
}

std::string Stream_manager::to_string() const {
    return std::format("Stream_manager(tailers={}, subscribers={})",
                       tailers_.size(), subscribers_.size());
}

void Stream_manager::open_tailer(Tailer& tailer) noexcept {
    if (tailer.fd >= 0) {
        static_cast<void>(::close(tailer.fd));
        tailer.fd = -1;
    }
    const int fd = ::open(tailer.descriptor.path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return;
    }
    struct stat status {};
    if (::fstat(fd, &status) != 0) {
        static_cast<void>(::close(fd));
        return;
    }
    tailer.fd = fd;
    tailer.device = status.st_dev;
    tailer.inode = status.st_ino;
    tailer.offset = 0;
    tailer.partial.clear();
    tailer.discarding = false;

    if (!tailer.seeded) {
        tailer.seeded = true;
        if (static_cast<std::uint64_t>(status.st_size) >
            static_cast<std::uint64_t>(limits_.ring_bytes)) {
            tailer.offset = static_cast<std::uint64_t>(status.st_size) -
                            static_cast<std::uint64_t>(limits_.ring_bytes);
            if (::lseek(fd, static_cast<off_t>(tailer.offset), SEEK_SET) < 0) {
                tailer.offset = 0;
                static_cast<void>(::lseek(fd, 0, SEEK_SET));
            }
        }
        // Seed the ring with bounded existing lines; discard a partial first
        // line when the read started in the middle of the file.
        std::vector<char> buffer(limits_.read_chunk_bytes);
        bool first_chunk = tailer.offset != 0;
        while (true) {
            const ssize_t count = ::read(fd, buffer.data(), buffer.size());
            if (count <= 0) {
                break;
            }
            std::string_view chunk(buffer.data(), static_cast<std::size_t>(count));
            if (first_chunk) {
                first_chunk = false;
                const std::size_t newline = chunk.find('\n');
                if (newline == std::string_view::npos) {
                    tailer.offset += static_cast<std::uint64_t>(count);
                    continue;
                }
                chunk.remove_prefix(newline + 1);
            }
            for (const char c : chunk) {
                if (tailer.discarding) {
                    if (c == '\n') {
                        tailer.discarding = false;
                    }
                    continue;
                }
                if (c == '\n') {
                    emit(tailer, tailer.partial, false);
                    tailer.partial.clear();
                } else {
                    tailer.partial.push_back(c);
                    if (tailer.partial.size() > limits_.max_line_bytes) {
                        tailer.partial.clear();
                        tailer.discarding = true;
                        Gap_marker gap{};
                        gap.stream_id = tailer.descriptor.id;
                        gap.dropped = 1;
                        gap.first_missing_sequence = tailer.next_sequence;
                        gap.last_missing_sequence = tailer.next_sequence;
                        ++tailer.next_sequence;
                        tailer.gaps.push_back(std::move(gap));
                    }
                }
            }
            tailer.offset += static_cast<std::uint64_t>(count);
        }
        tailer.offset = static_cast<std::uint64_t>(status.st_size);
        return;
    }

    tailer.offset = 0;
    static_cast<void>(::lseek(fd, 0, SEEK_SET));
}

void Stream_manager::emit(Tailer& tailer, std::string_view line,
                          bool truncated) noexcept {
    if (line.empty() && tailer.partial.empty() && truncated) {
        return;
    }
    Stream_record record{};
    record.stream_id = tailer.descriptor.id;
    record.kind = tailer.descriptor.kind;
    record.sequence = tailer.next_sequence++;
    record.received_at_rfc3339 = utc_now_rfc3339();
    record.line = std::string(line);
    record.truncated = truncated;

    tailer.bytes += record.line.size() + record_overhead_bytes;
    tailer.ring.push_back(record);
    while (tailer.ring.size() > limits_.ring_records ||
           tailer.bytes > limits_.ring_bytes) {
        if (tailer.ring.empty()) {
            break;
        }
        const Stream_record& dropped = tailer.ring.front();
        tailer.bytes -= dropped.line.size() + record_overhead_bytes;
        tailer.ring.pop_front();
    }

    dispatch(record);
    while (!tailer.gaps.empty()) {
        dispatch_gap(tailer.gaps.front());
        tailer.gaps.pop_front();
    }
}

void Stream_manager::poll_tailer(Tailer& tailer) noexcept {
    if (tailer.fd < 0) {
        open_tailer(tailer);
        if (tailer.fd < 0) {
            return;
        }
    }
    struct stat status {};
    if (::stat(tailer.descriptor.path.c_str(), &status) != 0) {
        return;
    }
    if (static_cast<std::uint64_t>(status.st_dev) != tailer.device ||
        static_cast<std::uint64_t>(status.st_ino) != tailer.inode ||
        static_cast<std::uint64_t>(status.st_size) < tailer.offset) {
        // Truncation or replacement: reopen and read the new file from zero.
        const bool was_seeded = tailer.seeded;
        static_cast<void>(::close(tailer.fd));
        tailer.fd = -1;
        tailer.seeded = false;
        open_tailer(tailer);
        tailer.seeded = was_seeded;
        if (tailer.fd < 0) {
            return;
        }
    }

    std::vector<char> buffer(limits_.read_chunk_bytes);
    const ssize_t count = ::read(tailer.fd, buffer.data(), buffer.size());
    if (count <= 0) {
        return;
    }
    const std::string_view chunk(buffer.data(), static_cast<std::size_t>(count));
    for (const char c : chunk) {
        if (tailer.discarding) {
            if (c == '\n') {
                tailer.discarding = false;
            }
            continue;
        }
        if (c == '\n') {
            emit(tailer, tailer.partial, false);
            tailer.partial.clear();
        } else {
            tailer.partial.push_back(c);
            if (tailer.partial.size() > limits_.max_line_bytes) {
                tailer.partial.clear();
                tailer.discarding = true;
                Gap_marker gap{};
                gap.stream_id = tailer.descriptor.id;
                gap.dropped = 1;
                gap.first_missing_sequence = tailer.next_sequence;
                gap.last_missing_sequence = tailer.next_sequence;
                ++tailer.next_sequence;
                tailer.gaps.push_back(std::move(gap));
            }
        }
    }
    tailer.offset += static_cast<std::uint64_t>(count);
    while (!tailer.gaps.empty()) {
        dispatch_gap(tailer.gaps.front());
        tailer.gaps.pop_front();
    }
}

void Stream_manager::poll() noexcept {
    for (auto& [id, tailer] : tailers_) {
        static_cast<void>(id);
        poll_tailer(tailer);
    }
}

void Stream_manager::sync(const Hub_tree& tree) noexcept {
    const std::vector<Hub_stream> streams = all_streams(tree);
    std::unordered_set<std::string> present;
    present.reserve(streams.size());
    for (const Hub_stream& descriptor : streams) {
        present.insert(descriptor.id);
        auto it = tailers_.find(descriptor.id);
        if (it == tailers_.end()) {
            Tailer tailer{};
            tailer.descriptor = descriptor;
            auto [inserted, ok] = tailers_.emplace(descriptor.id,
                                                   std::move(tailer));
            static_cast<void>(ok);
            open_tailer(inserted->second);
        } else {
            if (it->second.descriptor.path != descriptor.path) {
                if (it->second.fd >= 0) {
                    static_cast<void>(::close(it->second.fd));
                    it->second.fd = -1;
                }
                it->second.seeded = true;
                it->second.offset = 0;
                it->second.partial.clear();
            }
            it->second.descriptor = descriptor;
            if (it->second.fd < 0) {
                open_tailer(it->second);
            }
        }
    }
    for (auto it = tailers_.begin(); it != tailers_.end();) {
        if (present.find(it->first) == present.end()) {
            if (it->second.fd >= 0) {
                static_cast<void>(::close(it->second.fd));
            }
            it = tailers_.erase(it);
        } else {
            ++it;
        }
    }
}

bool Stream_manager::matches(const Subscriber& subscriber,
                             std::string_view stream_id) const noexcept {
    return subscriber.filter.empty() || subscriber.filter == stream_id;
}

void Stream_manager::push_gap(Subscriber& subscriber,
                              const Gap_marker& gap) noexcept {
    if (subscriber.overflow) {
        return;
    }
    const std::size_t size = gap.stream_id.size() + 64;
    if (subscriber.bytes + size > limits_.subscriber_bytes) {
        subscriber.overflow = true;
        return;
    }
    subscriber.bytes += size;
    subscriber.gaps.push_back(gap);
}

void Stream_manager::push_record(Subscriber& subscriber,
                                 const Stream_record& record) noexcept {
    if (subscriber.overflow) {
        return;
    }
    const std::size_t size = record.line.size() + record.stream_id.size() + 96;
    if (subscriber.bytes + size > limits_.subscriber_bytes) {
        subscriber.overflow = true;
        return;
    }
    subscriber.bytes += size;
    subscriber.records.push_back(record);
}

void Stream_manager::dispatch(const Stream_record& record) noexcept {
    for (auto& [id, subscriber] : subscribers_) {
        static_cast<void>(id);
        if (!matches(subscriber, record.stream_id)) {
            continue;
        }
        auto last = subscriber.last.find(record.stream_id);
        if (last != subscriber.last.end() &&
            record.sequence <= last->second) {
            continue;
        }
        if (last != subscriber.last.end() &&
            record.sequence > last->second + 1U) {
            Gap_marker gap{};
            gap.stream_id = record.stream_id;
            gap.dropped = record.sequence - last->second - 1U;
            gap.first_missing_sequence = last->second + 1U;
            gap.last_missing_sequence = record.sequence - 1U;
            push_gap(subscriber, gap);
        }
        subscriber.last[record.stream_id] = record.sequence;
        push_record(subscriber, record);
    }
}

void Stream_manager::dispatch_gap(const Gap_marker& gap) noexcept {
    for (auto& [id, subscriber] : subscribers_) {
        static_cast<void>(id);
        if (matches(subscriber, gap.stream_id)) {
            push_gap(subscriber, gap);
        }
    }
}

std::uint64_t Stream_manager::subscribe(
    std::string_view stream_id, std::uint64_t since,
    std::vector<Stream_record>& snapshot,
    std::optional<Gap_marker>& gap) noexcept {
    const std::uint64_t id = next_subscriber_id_++;
    Subscriber subscriber{};
    subscriber.id = id;
    subscriber.filter = std::string(stream_id);

    auto append_tailer = [&](const Tailer& tailer, std::uint64_t from) {
        if (tailer.ring.empty()) {
            return;
        }
        const std::uint64_t first = tailer.ring.front().sequence;
        if (first > from + 1U) {
            Gap_marker missing{};
            missing.stream_id = tailer.descriptor.id;
            missing.dropped = first - from - 1U;
            missing.first_missing_sequence = from + 1U;
            missing.last_missing_sequence = first - 1U;
            if (!gap.has_value()) {
                gap = missing;
            }
        }
        for (const Stream_record& record : tailer.ring) {
            if (record.sequence > from) {
                snapshot.push_back(record);
            }
        }
        subscriber.last[tailer.descriptor.id] =
            std::max(tailer.ring.back().sequence, from);
    };

    if (subscriber.filter.empty()) {
        for (const auto& [key, tailer] : tailers_) {
            static_cast<void>(key);
            append_tailer(tailer, 0);
        }
    } else {
        const auto found = tailers_.find(subscriber.filter);
        if (found != tailers_.end()) {
            append_tailer(found->second, since);
        } else {
            subscriber.last[subscriber.filter] = since;
        }
    }
    subscribers_.emplace(id, std::move(subscriber));
    return id;
}

void Stream_manager::unsubscribe(std::uint64_t subscriber_id) noexcept {
    subscribers_.erase(subscriber_id);
}

void Stream_manager::recent(std::string_view stream_id, std::size_t limit,
                            std::vector<Stream_record>& records,
                            std::optional<Gap_marker>& gap) const noexcept {
    static_cast<void>(gap);
    const auto found = tailers_.find(std::string(stream_id));
    if (found == tailers_.end()) {
        return;
    }
    const auto& ring = found->second.ring;
    const std::size_t count = limit < ring.size() ? limit : ring.size();
    records.reserve(records.size() + count);
    const std::size_t start = ring.size() - count;
    for (std::size_t index = start; index < ring.size(); ++index) {
        records.push_back(ring[index]);
    }
}

void Stream_manager::take(std::uint64_t subscriber_id,
                          std::vector<Stream_record>& records,
                          std::vector<Gap_marker>& gaps) noexcept {
    const auto found = subscribers_.find(subscriber_id);
    if (found == subscribers_.end()) {
        return;
    }
    Subscriber& subscriber = found->second;
    records.reserve(records.size() + subscriber.records.size());
    while (!subscriber.records.empty()) {
        records.push_back(std::move(subscriber.records.front()));
        subscriber.records.pop_front();
    }
    gaps.reserve(gaps.size() + subscriber.gaps.size());
    while (!subscriber.gaps.empty()) {
        gaps.push_back(std::move(subscriber.gaps.front()));
        subscriber.gaps.pop_front();
    }
    subscriber.bytes = 0;
}

bool Stream_manager::overflowed(std::uint64_t subscriber_id) const noexcept {
    const auto found = subscribers_.find(subscriber_id);
    return found != subscribers_.end() && found->second.overflow;
}

std::size_t Stream_manager::subscriber_count() const noexcept {
    return subscribers_.size();
}

} // namespace nexigen::hub
