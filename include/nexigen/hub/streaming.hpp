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
#include <nexigen/hub/model.hpp>

namespace nexigen::hub {

/// Control-plane diagnostic callback. It must not throw and should be cheap;
/// the hub passes preformatted messages once per failed refresh or stream.
using DiagnosticSink = void (*)(void* context,
                                 std::string_view message) noexcept;

/// Per-stream capacity and read bounds.
struct StreamLimits {
    std::size_t ring_records{4096};
    std::size_t ring_bytes{std::size_t{8} * 1024U * 1024U};
    std::size_t max_line_bytes{std::size_t{1} * 1024U * 1024U};
    std::size_t read_chunk_bytes{std::size_t{256} * 1024U};
    std::size_t subscriber_bytes{std::size_t{1} * 1024U * 1024U};

    [[nodiscard]] std::string to_string() const;
};

[[nodiscard]] inline std::string to_string(const StreamLimits& limits) {
    return limits.to_string();
}

/// Owns one tailer per discovered stream and all live subscribers.
class StreamManager {
public:
    explicit StreamManager(StreamLimits limits = {}) noexcept;
    ~StreamManager();

    StreamManager(const StreamManager&) = delete;
    StreamManager& operator=(const StreamManager&) = delete;

    /// Install a callback for stream open/stat/read failures. Passing nullptr
    /// (the default) keeps diagnostics silent.
    void set_diagnostic_sink(DiagnosticSink sink, void* context) noexcept;

    /// Reconcile tailers with the current tree. New streams are opened and
    /// seeded from the bounded file tail; removed streams are closed. Returns
    /// `SystemError` when at least one tailer could not be opened or refreshed.
    core::Status sync(const HubTree& tree) noexcept;

    /// Read bounded chunks from all tailers and fan records/gaps out to
    /// subscribers. Runs on the hub control loop.
    void poll() noexcept;

    /// Register a subscriber. Returns its id and fills the replay snapshot.
    /// `stream_id` empty means all streams. `since` filters one stream's replay.
    [[nodiscard]] std::uint64_t subscribe(
        std::string_view stream_id, std::uint64_t since,
        std::vector<StreamRecord>& snapshot,
        std::optional<GapMarker>& gap) noexcept;

    void unsubscribe(std::uint64_t subscriber_id) noexcept;

    /// Move queued live records/gaps out for one subscriber.
    void take(std::uint64_t subscriber_id, std::vector<StreamRecord>& records,
              std::vector<GapMarker>& gaps) noexcept;

    /// Copy up to `limit` newest records from one stream without subscribing.
    void recent(std::string_view stream_id, std::size_t limit,
                std::vector<StreamRecord>& records,
                std::optional<GapMarker>& gap) const noexcept;

    [[nodiscard]] bool overflowed(std::uint64_t subscriber_id) const noexcept;
    [[nodiscard]] std::size_t subscriber_count() const noexcept;
    [[nodiscard]] const StreamLimits& limits() const noexcept { return limits_; }
    [[nodiscard]] std::string to_string() const;

private:
    struct Tailer {
        HubStream descriptor{};
        int fd{-1};
        std::uint64_t device{0};
        std::uint64_t inode{0};
        std::uint64_t offset{0};
        std::string partial{};
        bool discarding{false};
        bool seeded{false};
        std::deque<StreamRecord> ring{};
        std::size_t bytes{0};
        std::uint64_t next_sequence{1};
        std::deque<GapMarker> gaps{};
    };

    struct Subscriber {
        std::uint64_t id{0};
        std::string filter{};
        std::unordered_map<std::string, std::uint64_t> last{};
        std::deque<StreamRecord> records{};
        std::deque<GapMarker> gaps{};
        std::size_t bytes{0};
        bool overflow{false};
    };

    [[nodiscard]] bool open_tailer(Tailer& tailer) noexcept;
    void report(std::string_view message) const noexcept;
    void poll_tailer(Tailer& tailer) noexcept;
    void emit(Tailer& tailer, std::string_view line, bool truncated) noexcept;
    void consume_chunk(Tailer& tailer, std::string_view chunk) noexcept;
    void dispatch(const StreamRecord& record) noexcept;
    void dispatch_gap(const GapMarker& gap) noexcept;
    [[nodiscard]] bool matches(const Subscriber& subscriber,
                               std::string_view stream_id) const noexcept;
    void push_record(Subscriber& subscriber, const StreamRecord& record) noexcept;
    void push_gap(Subscriber& subscriber, const GapMarker& gap) noexcept;

    StreamLimits limits_{};
    std::unordered_map<std::string, Tailer> tailers_{};
    std::unordered_map<std::uint64_t, Subscriber> subscribers_{};
    std::uint64_t next_subscriber_id_{1};
    DiagnosticSink diagnostic_sink_{nullptr};
    void* diagnostic_context_{nullptr};
};

} // namespace nexigen::hub


