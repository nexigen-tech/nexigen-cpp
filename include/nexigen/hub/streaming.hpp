// nexigen::hub — bounded tailers, replay rings, and subscribers
// (change nexigen-hub; spec hub/streaming).
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nexigen/core/result.hpp>
#include <nexigen/hub/gap_marker.hpp>
#include <nexigen/hub/hub_tree.hpp>
#include <nexigen/hub/stream_record.hpp>

namespace nexigen::hub {

/// @brief Control-plane diagnostic callback.
///
/// The hub passes preformatted messages once per failed refresh or stream, so
/// the sink sees one line per event rather than per record. It must not throw
/// and should be cheap; it is never called from an application hot path.
///
/// @param context Opaque pointer supplied with the sink.
/// @param message Message view valid only for the duration of the call.
/// @note Spec `hub/http-api`, requirement "Control-plane refresh failures are
///       reported".
using DiagnosticSink = void (*)(void* context, std::string_view message) noexcept;

/// @brief Per-stream capacity and read bounds.
///
/// Both ring bounds apply at once: the tailer drops the oldest records until
/// the record count and the accumulated byte size fit. The line bound caps one
/// framed line, and `read_chunk_bytes` caps a single `read()` so a huge file
/// never allocates a huge buffer.
///
/// @note Spec `hub/streaming`, requirement "Rings are bounded per stream"
///       (defaults 4,096 records and 8 MiB).
/// @note Spec `hub/streaming`, requirement "Lines are framed safely".
struct StreamLimits {
    std::size_t ring_records{4096};                               ///< Retained records per stream.
    std::size_t ring_bytes{std::size_t{8} * 1024U * 1024U};       ///< Retained record bytes.
    std::size_t max_line_bytes{std::size_t{1} * 1024U * 1024U};   ///< Longest framed line.
    std::size_t read_chunk_bytes{std::size_t{256} * 1024U};       ///< Bytes per `read()`.
    std::size_t subscriber_bytes{std::size_t{1} * 1024U * 1024U}; ///< Outbound buffer bound.

    /// @brief Render the limits as a diagnostic string.
    ///
    /// @return Text naming all five bounds; allocates.
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic helper mirroring `StreamLimits::to_string`.
///
/// @param limits Limits to render.
/// @return The member `to_string()` output.
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const StreamLimits& limits) {
    return limits.to_string();
}

/// @brief Owns one tailer per discovered stream and all live subscribers.
///
/// `sync()` reconciles the tailer set with the current tree: new streams are
/// opened and seeded from a bounded file tail, removed streams are closed.
/// `poll()` reads bounded chunks, frames complete lines, appends records to the
/// bounded per-stream ring, and fans records and gap markers out to matching
/// subscribers. A new subscriber receives the ring snapshot up to its sequence
/// cursor before any live record; the HTTP layer marks the boundary.
///
/// Everything runs on the hub control loop, so the manager is single-threaded
/// and needs no locks. It is cold path only: no application tick, producer,
/// snapshot, or sink callback may call into it, and tailing only reads source
/// files.
///
/// @note Spec `hub/streaming`, requirement "Subscribers receive replay before
///       live".
/// @note Spec `hub/streaming`, requirement "Streaming is cold path only".
class StreamManager {
public:
    /// @brief Construct an empty manager with the given bounds.
    ///
    /// Bounds are clamped to safe minimums: at least 1 retained record, 256
    /// ring bytes, 64 bytes per line, 1 KiB per read, and 1 KiB per subscriber.
    ///
    /// @param limits Capacity and read bounds copied for the manager's life.
    explicit StreamManager(StreamLimits limits = {}) noexcept;

    /// @brief Close every tailed file and drop all subscribers.
    ~StreamManager();

    /// @brief Deleted: the manager owns descriptors and tailer state.
    StreamManager(const StreamManager&) = delete;

    /// @brief Deleted: the manager owns descriptors and tailer state.
    StreamManager& operator=(const StreamManager&) = delete;

    /// @brief Install a callback for stream open/stat/read failures.
    ///
    /// @param sink Callback that must not throw and should be cheap.
    /// @param context Opaque pointer handed to `sink` on every call; `nullptr`
    ///        (the default) keeps diagnostics silent.
    /// @note Spec `hub/http-api`, requirement "Control-plane refresh failures
    ///       are reported".
    void set_diagnostic_sink(DiagnosticSink sink, void* context) noexcept;

    /// @brief Reconcile tailers with the current discovered tree.
    ///
    /// Opens new streams, seeds each from the bounded tail of an existing file
    /// (or from the beginning when the file appears later), reopens truncated
    /// or renamed files while keeping the stream identity, and closes streams
    /// that disappeared. A new attempt directory yields a new tailer instead of
    /// appending to the old attempt.
    ///
    /// @param tree Current tree snapshot.
    /// @return Empty `core::Status` when every tailer opened or refreshed;
    ///         `SystemError` when at least one could not be opened or refreshed
    ///         (the served state stays available).
    /// @note Spec `hub/streaming`, requirement "Tailing starts from existing
    ///       content and follows appends".
    /// @note Spec `hub/streaming`, requirement "File replacement and new
    ///       attempts are followed".
    core::Status sync(const HubTree& tree) noexcept;

    /// @brief Read from every tailer and fan records/gaps out to subscribers.
    ///
    /// Each tailer performs at most one bounded read pass per call. Incomplete
    /// trailing lines stay buffered; an oversized line is discarded and
    /// reported through a gap marker.
    ///
    /// @note Spec `hub/streaming`, requirement "Lines are framed safely".
    /// @note Runs on the hub control loop only.
    void poll() noexcept;

    /// @brief Register a subscriber and fill its replay snapshot.
    ///
    /// With an empty `stream_id` the subscriber follows every stream and is
    /// seeded from each ring from the oldest retained record. With a concrete
    /// id, the ring replay starts after `since`; when the requested cursor is
    /// older than the ring, `gap` describes the missing range.
    ///
    /// @param stream_id Stream identity to follow, or empty for all streams.
    /// @param since Last sequence already seen by the client.
    /// @param snapshot Output receiving the replay records in sequence order;
    ///        appended to, never cleared.
    /// @param gap Output receiving the first replay gap when the cursor is
    ///        older than the ring; left untouched when no gap is needed.
    /// @return Identifier to pass to `take()`/`unsubscribe()`.
    /// @note Spec `hub/streaming`, requirement "Subscribers receive replay
    ///       before live".
    [[nodiscard]] std::uint64_t subscribe(std::string_view stream_id, std::uint64_t since,
                                          std::vector<StreamRecord>& snapshot,
                                          std::optional<GapMarker>& gap) noexcept;

    /// @brief Remove a subscriber and discard its queued delivery.
    ///
    /// @param subscriber_id Identifier returned by `subscribe()`; unknown ids
    ///        are ignored.
    void unsubscribe(std::uint64_t subscriber_id) noexcept;

    /// @brief Move queued live records and gaps out for one subscriber.
    ///
    /// Queued items are appended to the outputs and removed from the
    /// subscriber, which also resets its outbound byte count.
    ///
    /// @param subscriber_id Identifier returned by `subscribe()`.
    /// @param records Output receiving queued records in delivery order.
    /// @param gaps Output receiving queued gap markers in delivery order.
    void take(std::uint64_t subscriber_id, std::vector<StreamRecord>& records,
              std::vector<GapMarker>& gaps) noexcept;

    /// @brief Copy the newest records of one stream without subscribing.
    ///
    /// @param stream_id Stream identity to read; an unknown id yields nothing.
    /// @param limit Maximum records to copy; capped by the ring size.
    /// @param records Output appended with the newest records in sequence
    ///        order.
    /// @param gap Gap output; the bounded ring lookup does not synthesize one.
    /// @note Spec `hub/http-api`, requirement "Read-only JSON endpoints"
    ///       (recent endpoint caps `limit` at the ring capacity).
    void recent(std::string_view stream_id, std::size_t limit, std::vector<StreamRecord>& records,
                std::optional<GapMarker>& gap) const noexcept;

    /// @brief True when a subscriber's outbound bound was exceeded.
    ///
    /// The HTTP layer uses this to close the slow client instead of blocking
    /// tailing or other subscribers.
    ///
    /// @param subscriber_id Identifier returned by `subscribe()`.
    /// @return `true` for an overflowing subscriber, `false` for a healthy or
    ///         unknown one.
    /// @note Spec `hub/streaming`, requirement "Slow subscribers are
    ///       disconnected".
    [[nodiscard]] bool overflowed(std::uint64_t subscriber_id) const noexcept;

    /// @brief Number of currently registered subscribers.
    ///
    /// @return Live subscriber count.
    [[nodiscard]] std::size_t subscriber_count() const noexcept;

    /// @brief The bounds this manager was constructed with.
    ///
    /// @return Const reference to manager-owned limits.
    [[nodiscard]] const StreamLimits& limits() const noexcept { return limits_; }

    /// @brief Render the manager state as a diagnostic string.
    ///
    /// @return Text naming the tailer and subscriber counts; allocates.
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Per-stream tailer state and bounded replay ring.
    struct Tailer {
        HubStream descriptor{};          ///< Identity, kind, and source path.
        int fd{-1};                      ///< Open file descriptor, `-1` when closed.
        std::uint64_t device{0};         ///< Device id captured at open.
        std::uint64_t inode{0};          ///< Inode captured at open (replacement check).
        std::uint64_t offset{0};         ///< Read cursor in bytes.
        std::string partial{};           ///< Incomplete trailing line awaiting `\n`.
        bool discarding{false};          ///< Skipping the rest of an oversized line.
        bool seeded{false};              ///< Initial bounded tail read completed.
        std::deque<StreamRecord> ring{}; ///< Retained records, oldest first.
        std::size_t bytes{0};            ///< Accumulated record bytes in `ring`.
        std::uint64_t next_sequence{1};  ///< Sequence assigned to the next record.
        std::deque<GapMarker> gaps{};    ///< Gaps awaiting dispatch to subscribers.
    };

    /// @brief One live subscriber's filter and bounded outbound queue.
    struct Subscriber {
        std::uint64_t id{0};                                   ///< Registration id.
        std::string filter{};                                  ///< Stream id, empty for all.
        std::unordered_map<std::string, std::uint64_t> last{}; ///< Last delivered sequence.
        std::deque<StreamRecord> records{};                    ///< Queued live records.
        std::deque<GapMarker> gaps{};                          ///< Queued live gaps.
        std::size_t bytes{0};                                  ///< Queued record bytes.
        bool overflow{false};                                  ///< Outbound bound exceeded.
    };

    /// @brief Open (or reopen) one tailer and seed its ring.
    ///
    /// @param tailer Tailer to open; identity fields are already set.
    /// @return `true` on success, `false` when open/stat fails (reported to the
    ///         diagnostic sink).
    [[nodiscard]] bool open_tailer(Tailer& tailer) noexcept;

    /// @brief Forward one message to the diagnostic sink when configured.
    ///
    /// @param message Preformatted message view.
    void report(std::string_view message) const noexcept;

    /// @brief Read once from one tailer, frame lines, and dispatch results.
    ///
    /// @param tailer Tailer to advance.
    void poll_tailer(Tailer& tailer) noexcept;

    /// @brief Append one complete line as a record and dispatch it.
    ///
    /// @param tailer Tailer that produced the line.
    /// @param line Complete line without its newline.
    /// @param truncated Whether the line was cut to the size bound.
    void emit(Tailer& tailer, std::string_view line, bool truncated) noexcept;

    /// @brief Frame a raw chunk into lines, buffering a partial tail.
    ///
    /// @param tailer Tailer owning the partial-line buffer.
    /// @param chunk Bytes just read from the file.
    void consume_chunk(Tailer& tailer, std::string_view chunk) noexcept;

    /// @brief Deliver one record to every matching subscriber.
    ///
    /// @param record Record to enqueue; gaps are synthesized for subscribers
    ///        whose last sequence skipped records.
    void dispatch(const StreamRecord& record) noexcept;

    /// @brief Deliver one gap marker to every matching subscriber.
    ///
    /// @param gap Gap to enqueue.
    void dispatch_gap(const GapMarker& gap) noexcept;

    /// @brief Test whether a subscriber follows a stream.
    ///
    /// @param subscriber Subscriber to test.
    /// @param stream_id Stream identity to match.
    /// @return `true` for an all-stream subscriber or an exact filter match.
    [[nodiscard]] bool matches(const Subscriber& subscriber,
                               std::string_view stream_id) const noexcept;

    /// @brief Enqueue one record within the outbound bound.
    ///
    /// @param subscriber Destination subscriber.
    /// @param record Record to copy; sets `overflow` when the bound is hit.
    void push_record(Subscriber& subscriber, const StreamRecord& record) noexcept;

    /// @brief Enqueue one gap marker within the outbound bound.
    ///
    /// @param subscriber Destination subscriber.
    /// @param gap Gap to copy; sets `overflow` when the bound is hit.
    void push_gap(Subscriber& subscriber, const GapMarker& gap) noexcept;

    StreamLimits limits_{};                                       ///< Capacity and read bounds.
    std::unordered_map<std::string, Tailer> tailers_{};           ///< Tailers by stream id.
    std::unordered_map<std::uint64_t, Subscriber> subscribers_{}; ///< Subscribers by id.
    std::uint64_t next_subscriber_id_{1};     ///< Next id handed to a subscriber.
    DiagnosticSink diagnostic_sink_{nullptr}; ///< Optional failure callback.
    void* diagnostic_context_{nullptr};       ///< Context for the callback.
};

} // namespace nexigen::hub
