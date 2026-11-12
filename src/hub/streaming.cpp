/// @file
/// @brief Bounded tailers, replay rings, and subscribers — out-of-line
///        implementation of `nexigen::hub::StreamManager` and `StreamLimits`.
///
/// Every tailer owns a file descriptor on one stream file and a bounded ring of
/// the records already emitted from it. A poll reads at most one chunk, frames
/// it into lines (a line longer than `max_line_bytes` is discarded and
/// reported as a gap marker), appends the resulting records to the ring, and
/// dispatches them to the matching subscribers. The ring is bounded twice —
/// by record count and by bytes — and every eviction is visible to the
/// subscriber as a `GapMarker` naming the lost sequence range, so a slow
/// consumer is told what it missed instead of silently receiving a hole.
///
/// Cold path only: this translation unit runs in the hub control loop, never
/// in an application tick, producer, snapshot, or sink callback. Records are
/// copied into subscriber queues under a byte budget; a subscriber that
/// exceeds it is marked overflowed and disconnected by the HTTP server.
///
/// @note Spec `hub/streaming`, requirement "Tailing starts from existing
///       content and follows appends" — a freshly opened tailer seeds its ring
///       from the file's existing tail and then follows appends.
/// @note Spec `hub/streaming`, requirement "Lines are framed safely" — a
///       partial trailing line is buffered until its newline arrives; a line
///       over the size bound is dropped and recorded as a gap.
/// @note Spec `hub/streaming`, requirement "Rings are bounded per stream" —
///       the ring is trimmed by record count and by bytes; every eviction is
///       reported as a gap marker.
/// @note Spec `hub/streaming`, requirement "Subscribers receive replay before
///       live" — `subscribe()` replays the ring from the requested sequence
///       before live records reach the subscriber.
/// @note Spec `hub/streaming`, requirement "Slow subscribers are disconnected"
///       — the outbound byte budget marks a subscriber overflowed once
///       exceeded.
/// @note Spec `hub/streaming`, requirement "File replacement and new attempts
///       are followed" — a changed inode, device or truncated size reopens the
///       tailer and re-seeds it from the new file.
/// @note Spec `hub/streaming`, requirement "Streaming is cold path only".
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

#include <nexigen/chrono/civil_time.hpp>
#include <nexigen/hub/gap_marker.hpp>
#include <nexigen/hub/hub_tree.hpp>
#include <nexigen/hub/stream_record.hpp>
#include <nexigen/hub/streaming.hpp>

namespace nexigen::hub {

namespace {

constexpr std::size_t record_overhead_bytes = 64;

/// @brief Current UTC instant rendered as an RFC 3339 timestamp.
///
/// Cold-path helper for the `received_at_rfc3339` field of a stream record:
/// the wall-clock instant is sampled once per emitted record and rendered
/// through the shared chrono formatter into a fixed buffer.
///
/// @return The current UTC time as an RFC 3339 string with nanosecond
///         precision (e.g. `2026-11-13T08:15:00.123456789Z`).
/// @note Spec `hub/streaming`, requirement "Streaming types follow the
///       serialization contract" — the record timestamp is part of the
///       serialized record shape.
[[nodiscard]] std::string utc_now_rfc3339() {
    using namespace std::chrono;
    const auto since_epoch = system_clock::now().time_since_epoch();
    const std::int64_t epoch_ns = duration_cast<nanoseconds>(since_epoch).count();
    char buffer[31]{};
    nexigen::chrono::format_utc_iso8601_ns(epoch_ns, buffer);
    return std::string(buffer);
}

} // namespace

/// @brief Render the configured bounds for cold-path diagnostics.
///
/// @note Spec `hub/streaming`, requirement "Streaming types follow the
///       serialization contract" — every public type exposes `to_string()`.
std::string StreamLimits::to_string() const {
    return std::format("StreamLimits(ring_records={}, ring_bytes={}, max_line_bytes={}, "
                       "read_chunk_bytes={}, subscriber_bytes={})",
                       ring_records, ring_bytes, max_line_bytes, read_chunk_bytes,
                       subscriber_bytes);
}

/// @brief Construct a manager with the given bounds, clamped to usable minima.
///
/// Zero record bounds, a tiny byte budget, a maximum line size below the
/// framing floor and read/subscriber budgets below one chunk are raised to
/// the smallest value that keeps the tailer and the subscriber queues
/// functional; a manager is never constructed with a bound that cannot hold
/// a single record.
///
/// @note Spec `hub/streaming`, requirement "Rings are bounded per stream" —
///       the bounds are always at least one record / one chunk wide.
StreamManager::StreamManager(StreamLimits limits) noexcept : limits_(limits) {
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

/// @brief Close every tailer descriptor owned by the manager.
///
/// Each open tailer holds a read-only descriptor on a stream file; the
/// destructor closes them all. No record is lost by closing a tailer — the
/// ring it holds dies with the manager.
StreamManager::~StreamManager() {
    for (auto& [id, tailer] : tailers_) {
        static_cast<void>(id);
        if (tailer.fd >= 0) {
            static_cast<void>(::close(tailer.fd));
        }
    }
}

/// @brief Render the manager's live shape for cold-path diagnostics.
///
/// @note Spec `hub/streaming`, requirement "Streaming types follow the
///       serialization contract" — every public type exposes `to_string()`.
std::string StreamManager::to_string() const {
    return std::format("StreamManager(tailers={}, subscribers={})", tailers_.size(),
                       subscribers_.size());
}

/// @brief Install the sink that receives stream diagnostics.
///
/// @note Spec `hub/streaming`, requirement "Streaming is cold path only" —
///       diagnostics are a control-plane concern.
void StreamManager::set_diagnostic_sink(DiagnosticSink sink, void* context) noexcept {
    diagnostic_sink_ = sink;
    diagnostic_context_ = context;
}

/// @brief Forward one diagnostic message to the installed sink, if any.
///
/// @note Spec `hub/streaming`, requirement "Streaming is cold path only".
void StreamManager::report(std::string_view message) const noexcept {
    if (diagnostic_sink_ != nullptr) {
        diagnostic_sink_(diagnostic_context_, message);
    }
}

/// @brief Open (or reopen) one tailer and seed its ring from existing content.
///
/// Reopens the stream file read-only, records its device and inode so a later
/// replacement is detectable, and — on the first open — seeds the ring from
/// the tail of the file (bounded by the ring byte budget) so a subscriber that
/// connects late still sees recent history. A partially written first line is
/// discarded. Gaps produced while seeding are dispatched before returning.
///
/// @note Spec `hub/streaming`, requirement "Tailing starts from existing
///       content and follows appends" — the seed covers the existing tail.
/// @note Spec `hub/streaming`, requirement "File replacement and new attempts
///       are followed" — the device and inode recorded here are what later
///       detects a replaced file.
bool StreamManager::open_tailer(Tailer& tailer) noexcept {
    if (tailer.fd >= 0) {
        static_cast<void>(::close(tailer.fd));
        tailer.fd = -1;
    }
    const int fd = ::open(tailer.descriptor.path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        report(std::format("event=stream_open_failed stream={} path={} errno={}",
                           tailer.descriptor.id, tailer.descriptor.path, errno));
        return false;
    }
    struct stat status{};
    if (::fstat(fd, &status) != 0) {
        const int error = errno;
        static_cast<void>(::close(fd));
        report(std::format("event=stream_stat_failed stream={} path={} errno={}",
                           tailer.descriptor.id, tailer.descriptor.path, error));
        return false;
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
                if (count < 0) {
                    report(std::format("event=stream_read_failed stream={} path={} errno={}",
                                       tailer.descriptor.id, tailer.descriptor.path, errno));
                }
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
            consume_chunk(tailer, chunk);
            tailer.offset += static_cast<std::uint64_t>(count);
        }
        // speedup-validation-loop (latent-race fix exposed by ctest -j):
        // the seed read advances the fd cursor by the bytes actually consumed,
        // so pin `offset` to that same cursor. The former assignment to
        // `status.st_size` used a fstat size captured BEFORE the read — when a
        // writer truncates then rewrites while seeding, the file grows mid-read
        // and the stale size desyncs `offset` from the fd cursor, wedging the
        // tailer at offset < size (every later read then returns 0 forever).
        // A too-long-line gap produced during the seed read also needs to be
        // dispatched here (poll_tailer does the same after its own read).
        while (!tailer.gaps.empty()) {
            dispatch_gap(tailer.gaps.front());
            tailer.gaps.pop_front();
        }
        return true;
    }

    tailer.offset = 0;
    static_cast<void>(::lseek(fd, 0, SEEK_SET));
    return true;
}

/// @brief Frame a raw chunk into complete lines, buffering a partial tail.
///
/// Bytes are scanned for newlines; each complete line is emitted, and a
/// trailing fragment is kept in the tailer's buffer until the rest of the line
/// arrives. A line that grows past the maximum line size is dropped and
/// reported as a gap marker, after which the remainder of that line is
/// discarded up to the next newline.
///
/// @note Spec `hub/streaming`, requirement "Lines are framed safely" — a
///       partial line is never emitted half-formed.
/// @note Spec `hub/streaming`, requirement "Rings are bounded per stream" —
///       an over-long line is dropped instead of growing the ring without bound.
void StreamManager::consume_chunk(Tailer& tailer, std::string_view chunk) noexcept {
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
                GapMarker gap{};
                gap.stream_id = tailer.descriptor.id;
                gap.dropped = 1;
                gap.first_missing_sequence = tailer.next_sequence;
                gap.last_missing_sequence = tailer.next_sequence;
                ++tailer.next_sequence;
                tailer.gaps.push_back(std::move(gap));
            }
        }
    }
}

/// @brief Append one complete line as a record and dispatch it to subscribers.
///
/// The record receives the next sequence number of its stream and the current
/// UTC timestamp; the ring is then trimmed to the configured record and byte
/// bounds, and every evicted record is reported to the affected subscribers as
/// a gap marker naming the lost sequence range.
///
/// @note Spec `hub/streaming`, requirement "Streams have stable identities" —
///       the sequence number is the per-stream identity of a record.
/// @note Spec `hub/streaming`, requirement "Rings are bounded per stream".
void StreamManager::emit(Tailer& tailer, std::string_view line, bool truncated) noexcept {
    if (line.empty() && tailer.partial.empty() && truncated) {
        return;
    }
    StreamRecord record{};
    record.stream_id = tailer.descriptor.id;
    record.kind = tailer.descriptor.kind;
    record.sequence = tailer.next_sequence++;
    record.received_at_rfc3339 = utc_now_rfc3339();
    record.line = std::string(line);
    record.truncated = truncated;

    tailer.bytes += record.line.size() + record_overhead_bytes;
    tailer.ring.push_back(record);
    while (tailer.ring.size() > limits_.ring_records || tailer.bytes > limits_.ring_bytes) {
        if (tailer.ring.empty()) {
            break;
        }
        const StreamRecord& dropped = tailer.ring.front();
        tailer.bytes -= dropped.line.size() + record_overhead_bytes;
        tailer.ring.pop_front();
    }

    dispatch(record);
    while (!tailer.gaps.empty()) {
        dispatch_gap(tailer.gaps.front());
        tailer.gaps.pop_front();
    }
}

/// @brief Read once from one tailer, frame the lines, and dispatch results.
///
/// A truncated, replaced or deleted file (detected by device, inode or size)
/// reopens the tailer and re-seeds it from the new content. Otherwise one
/// chunk is read, framed into lines and dispatched; pending gap markers are
/// flushed after the read, so a gap caused by an over-long line is never
/// delivered after the records that follow it.
///
/// @note Spec `hub/streaming`, requirement "Tailing starts from existing
///       content and follows appends" — one poll follows the file's appends.
/// @note Spec `hub/streaming`, requirement "File replacement and new attempts
///       are followed" — a replaced file is reopened and re-seeded.
void StreamManager::poll_tailer(Tailer& tailer) noexcept {
    if (tailer.fd < 0) {
        static_cast<void>(open_tailer(tailer));
        if (tailer.fd < 0) {
            return;
        }
    }
    struct stat status{};
    if (::stat(tailer.descriptor.path.c_str(), &status) != 0) {
        report(std::format("event=stream_stat_failed stream={} path={} errno={}",
                           tailer.descriptor.id, tailer.descriptor.path, errno));
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
        static_cast<void>(open_tailer(tailer));
        tailer.seeded = was_seeded;
        if (tailer.fd < 0) {
            return;
        }
    }

    std::vector<char> buffer(limits_.read_chunk_bytes);
    const ssize_t count = ::read(tailer.fd, buffer.data(), buffer.size());
    if (count < 0) {
        report(std::format("event=stream_read_failed stream={} path={} errno={}",
                           tailer.descriptor.id, tailer.descriptor.path, errno));
        return;
    }
    if (count == 0) {
        return;
    }
    const std::string_view chunk(buffer.data(), static_cast<std::size_t>(count));
    consume_chunk(tailer, chunk);
    tailer.offset += static_cast<std::uint64_t>(count);
    while (!tailer.gaps.empty()) {
        dispatch_gap(tailer.gaps.front());
        tailer.gaps.pop_front();
    }
}

/// @brief Advance every tailer once, in no particular order.
///
/// One poll per tailer: read, frame, dispatch. Called from the hub control
/// loop, never from an application hot path.
///
/// @note Spec `hub/streaming`, requirement "Streaming is cold path only".
void StreamManager::poll() noexcept {
    for (auto& [id, tailer] : tailers_) {
        static_cast<void>(id);
        poll_tailer(tailer);
    }
}

/// @brief Reconcile the tailers with the discovered tree.
///
/// A stream that appeared is opened and seeded; a stream whose path changed
/// is reopened at offset zero; a stream that disappeared has its descriptor
/// closed and its tailer removed. Tailers for streams that are still present
/// keep their ring and their read offset.
///
/// @note Spec `hub/streaming`, requirement "File replacement and new attempts
///       are followed" — a new attempt (new path) is opened, a vanished
///       attempt is dropped.
/// @note Spec `hub/streaming`, requirement "Streaming is cold path only".
core::Status StreamManager::sync(const HubTree& tree) noexcept {
    bool failed = false;
    const std::vector<HubStream> streams = all_streams(tree);
    std::unordered_set<std::string> present;
    present.reserve(streams.size());
    for (const HubStream& descriptor : streams) {
        present.insert(descriptor.id);
        auto it = tailers_.find(descriptor.id);
        if (it == tailers_.end()) {
            Tailer tailer{};
            tailer.descriptor = descriptor;
            auto [inserted, ok] = tailers_.emplace(descriptor.id, std::move(tailer));
            static_cast<void>(ok);
            if (!open_tailer(inserted->second)) {
                failed = true;
            }
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
            if (it->second.fd < 0 && !open_tailer(it->second)) {
                failed = true;
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
    return failed ? core::Status{std::unexpected(core::ErrorCode::SystemError)} : core::Status{};
}

/// @brief Test whether a subscriber follows a stream.
///
/// @note Spec `hub/streaming`, requirement "Subscribers receive replay before
///       live" — only matching subscribers receive replay and live records.
bool StreamManager::matches(const Subscriber& subscriber,
                            std::string_view stream_id) const noexcept {
    return subscriber.filter.empty() || subscriber.filter == stream_id;
}

/// @brief Enqueue one gap marker within the subscriber's outbound budget.
///
/// A subscriber that would exceed its byte budget is marked overflowed and
/// receives nothing further; the HTTP server disconnects it. Gaps already
/// queued stay queued, so the consumer learns about the hole before the
/// disconnect.
///
/// @note Spec `hub/streaming`, requirement "Slow subscribers are disconnected"
///       — the outbound byte budget is the disconnection trigger.
/// @note Spec `hub/streaming`, requirement "Rings are bounded per stream" —
///       the gap names the sequence range the ring lost.
void StreamManager::push_gap(Subscriber& subscriber, const GapMarker& gap) noexcept {
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

/// @brief Enqueue one record within the subscriber's outbound budget.
///
/// @note Spec `hub/streaming`, requirement "Slow subscribers are disconnected"
///       — the outbound byte budget is the disconnection trigger.
void StreamManager::push_record(Subscriber& subscriber, const StreamRecord& record) noexcept {
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

/// @brief Deliver one record to every matching subscriber.
///
/// A subscriber whose last delivered sequence skips records receives a
/// synthesized gap marker for the missing range before the record itself;
/// a record that the subscriber already saw is dropped.
///
/// @note Spec `hub/streaming`, requirement "Subscribers receive replay before
///       live" — the replay establishes the sequence baseline the gap
///       detection compares against.
/// @note Spec `hub/streaming`, requirement "Rings are bounded per stream" —
///       skipped sequences are reported, not silently swallowed.
void StreamManager::dispatch(const StreamRecord& record) noexcept {
    for (auto& [id, subscriber] : subscribers_) {
        static_cast<void>(id);
        if (!matches(subscriber, record.stream_id)) {
            continue;
        }
        auto last = subscriber.last.find(record.stream_id);
        if (last != subscriber.last.end() && record.sequence <= last->second) {
            continue;
        }
        if (last != subscriber.last.end() && record.sequence > last->second + 1U) {
            GapMarker gap{};
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

/// @brief Deliver one gap marker to every matching subscriber.
///
/// @note Spec `hub/streaming`, requirement "Rings are bounded per stream" —
///       the lost range is delivered to the subscribers that need it.
void StreamManager::dispatch_gap(const GapMarker& gap) noexcept {
    for (auto& [id, subscriber] : subscribers_) {
        static_cast<void>(id);
        if (matches(subscriber, gap.stream_id)) {
            push_gap(subscriber, gap);
        }
    }
}

/// @brief Register a subscriber and replay the ring to it before live records.
///
/// The subscriber receives the ring content newer than `since` as the initial
/// snapshot; if the ring has already moved past that point, the skipped
/// range is reported as a gap marker. The subscriber's sequence baseline is
/// then set, so the next live record that skips sequences is detected as a gap
/// too.
///
/// @note Spec `hub/streaming`, requirement "Subscribers receive replay before
///       live" — the replay is delivered before any live record.
/// @note Spec `hub/streaming`, requirement "Streams have stable identities" —
///       replay is positioned by sequence number.
std::uint64_t StreamManager::subscribe(std::string_view stream_id, std::uint64_t since,
                                       std::vector<StreamRecord>& snapshot,
                                       std::optional<GapMarker>& gap) noexcept {
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
            GapMarker missing{};
            missing.stream_id = tailer.descriptor.id;
            missing.dropped = first - from - 1U;
            missing.first_missing_sequence = from + 1U;
            missing.last_missing_sequence = first - 1U;
            if (!gap.has_value()) {
                gap = missing;
            }
        }
        for (const StreamRecord& record : tailer.ring) {
            if (record.sequence > from) {
                snapshot.push_back(record);
            }
        }
        subscriber.last[tailer.descriptor.id] = std::max(tailer.ring.back().sequence, from);
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

/// @brief Drop a subscriber and everything queued for it.
///
/// @note Spec `hub/streaming`, requirement "Slow subscribers are disconnected"
///       — unsubscribing releases the outbound budget it held.
void StreamManager::unsubscribe(std::uint64_t subscriber_id) noexcept {
    subscribers_.erase(subscriber_id);
}

/// @brief Copy the most recent records of one stream out of its ring.
///
/// A cold-path snapshot helper: it reads the ring directly and never touches
/// the subscriber queues.
///
/// @note Spec `hub/streaming`, requirement "Streaming is cold path only".
void StreamManager::recent(std::string_view stream_id, std::size_t limit,
                           std::vector<StreamRecord>& records,
                           std::optional<GapMarker>& gap) const noexcept {
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

/// @brief Drain the records and gap markers queued for one subscriber.
///
/// Both queues are moved out and the subscriber's outbound byte accounting is
/// reset, so the budget reflects what is still pending rather than what has
/// ever been sent.
///
/// @note Spec `hub/streaming`, requirement "Slow subscribers are disconnected"
///       — the byte budget is reset by what is actually drained.
/// @note Spec `hub/streaming`, requirement "Streaming is cold path only".
void StreamManager::take(std::uint64_t subscriber_id, std::vector<StreamRecord>& records,
                         std::vector<GapMarker>& gaps) noexcept {
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

/// @brief Test whether a subscriber exceeded its outbound budget.
///
/// @note Spec `hub/streaming`, requirement "Slow subscribers are disconnected".
bool StreamManager::overflowed(std::uint64_t subscriber_id) const noexcept {
    const auto found = subscribers_.find(subscriber_id);
    return found != subscribers_.end() && found->second.overflow;
}

/// @brief Number of registered subscribers.
///
/// @note Spec `hub/streaming`, requirement "Streaming is cold path only".
std::size_t StreamManager::subscriber_count() const noexcept {
    return subscribers_.size();
}

} // namespace nexigen::hub
