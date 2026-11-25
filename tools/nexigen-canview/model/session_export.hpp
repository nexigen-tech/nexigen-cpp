// nexigen-canview — session export (JSONL/CSV/TXT) and async writer.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/core/spsc_ring_buffer.hpp>

#include "can_ingress_record.hpp"
#include "can_record_view.hpp"
#include "record_source.hpp"
#include "session_record.hpp"

namespace nexigen::canview {

/// @brief Export container format.
enum class SessionFormat : std::uint8_t {
    Jsonl, ///< Canonical session JSONL v1.
    Csv,   ///< Tabular CSV, export only.
    Txt,   ///< Fixed-column text, export only.
};

/// @brief Build a frame line from one record.
/// @param record Record to export.
/// @param channel_name Channel display name.
/// @param seq Local identity.
/// @return Frame record.
[[nodiscard]] SessionFrameRecord session_frame_from_view(const CanRecordView& record,
                                                         std::string_view channel_name,
                                                         std::uint64_t seq);

/// @brief Build a gap line from one record.
/// @param record Record to export.
/// @param channel_name Channel display name.
/// @param seq Local identity.
/// @return Gap record.
[[nodiscard]] SessionGapRecord session_gap_from_view(const CanRecordView& record,
                                                     std::string_view channel_name,
                                                     std::uint64_t seq);

/// @brief Build a status line.
/// @param event Status sample.
/// @param channel_name Channel display name.
/// @param seq Local identity.
/// @return Status record.
[[nodiscard]] SessionStatusRecord session_status_from_event(const StatusEvent& event,
                                                            std::string_view channel_name,
                                                            std::uint64_t seq);

/// @brief CSV header row.
/// @return Comma-separated column names.
[[nodiscard]] std::string_view csv_header() noexcept;

/// @brief Render one CSV frame row.
/// @param record Frame to render.
/// @return Row without trailing newline.
[[nodiscard]] std::string csv_row(const SessionFrameRecord& record);

/// @brief Render one TXT frame row.
/// @param record Frame to render.
/// @return Fixed-column row without trailing newline.
[[nodiscard]] std::string txt_row(const SessionFrameRecord& record);

/// @brief Asynchronous export with one active writer.
class AsyncExportWriter {
public:
    /// @brief Queue capacity.
    static constexpr std::size_t queue_capacity{4096U};

    /// @brief Progress snapshot.
    struct Progress {
        std::uint64_t enqueued{0}; ///< Items accepted by the queue.
        std::uint64_t written{0};  ///< Items written to the partial file.
        std::uint64_t skipped{0};  ///< Items rejected because the record vanished/queue full.
        bool active{false};        ///< Writer running.
        bool cancelled{false};     ///< Export cancelled.
        bool failed{false};        ///< I/O failure.
    };

    AsyncExportWriter();
    ~AsyncExportWriter();
    AsyncExportWriter(const AsyncExportWriter&) = delete;
    AsyncExportWriter& operator=(const AsyncExportWriter&) = delete;

    /// @brief Start a new export.
    /// @param path Target path.
    /// @param format Container format.
    /// @param header Session header.
    /// @return Empty status, or `Conflict` when another export is active.
    [[nodiscard]] nexigen::core::Status start(const std::string& path, SessionFormat format,
                                              const CanViewSessionHeader& header);

    /// @brief Enqueue one frame.
    /// @param record Frame record.
    /// @return `true` when accepted.
    bool enqueue(const SessionFrameRecord& record);
    /// @brief Enqueue one gap.
    /// @param record Gap record.
    /// @return `true` when accepted.
    bool enqueue(const SessionGapRecord& record);
    /// @brief Enqueue one status.
    /// @param record Status record.
    /// @return `true` when accepted.
    bool enqueue(const SessionStatusRecord& record);
    /// @brief Enqueue one record by sequence when it is still resident.
    /// @param source History to read.
    /// @param seq Absolute record number.
    /// @param channel_name Channel display name.
    /// @return `true` when accepted; a missing record counts as skipped.
    bool enqueue_from_source(const RecordSource& source, std::uint64_t seq,
                             std::string_view channel_name);

    /// @brief Finish, join the writer, and rename `.partial` to the target.
    void finish();
    /// @brief Cancel, join the writer, and remove the partial file.
    void cancel();
    /// @brief Whether an export owns the writer.
    /// @return `true` between `start()` and finish/cancel.
    [[nodiscard]] bool active() const noexcept { return active_.load(std::memory_order_acquire); }
    /// @brief Progress snapshot.
    /// @return Counters and state.
    [[nodiscard]] Progress progress() const noexcept;

private:
    /// @brief One queued item.
    struct Item {
        CanViewRecordType type{CanViewRecordType::Frame};
        SessionFrameRecord frame{};
        SessionGapRecord gap{};
        SessionStatusRecord status{};
    };

    /// @brief Writer thread body.
    void run(std::stop_token token) noexcept;
    /// @brief Write one item in the selected format.
    bool write_item(const Item& item) noexcept;
    /// @brief Drop the partial file.
    void remove_partial() noexcept;

    std::string path_{};                         ///< Target path.
    std::string partial_path_{};                 ///< Temporary path.
    SessionFormat format_{SessionFormat::Jsonl}; ///< Container format.
    CanViewSessionHeader header_{};              ///< Header line.
    std::ofstream file_{};                       ///< Partial output.
    std::unique_ptr<nexigen::core::SpscRingBuffer<Item, queue_capacity>> queue_{};
    std::jthread worker_{};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> active_{false};
    std::atomic<bool> cancelled_{false};
    std::atomic<bool> failed_{false};
    std::atomic<std::uint64_t> enqueued_{0};
    std::atomic<std::uint64_t> written_{0};
    std::atomic<std::uint64_t> skipped_{0};
};

} // namespace nexigen::canview

namespace nexigen::core {

/// @brief Vocabulary table for `SessionFormat`.
template <> struct EnumTraits<nexigen::canview::SessionFormat> {
    /// @brief One value/name row.

    struct Entry {
        nexigen::canview::SessionFormat value; ///< Enumerator.
        std::string_view name;                 ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::canview::SessionFormat::Jsonl, "Jsonl"},
        {nexigen::canview::SessionFormat::Csv, "Csv"},
        {nexigen::canview::SessionFormat::Txt, "Txt"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canview {

/// @brief Canonical format name.
[[nodiscard]] constexpr std::string_view to_string(SessionFormat value) noexcept {
    return core::enum_name(value);
}
/// @brief Parse a format.
[[nodiscard]] constexpr bool from_string(std::string_view name, SessionFormat& out) noexcept {
    return core::enum_value(name, out);
}
/// @brief Write a format as JSON.
inline void to_json(nlohmann::json& j, const SessionFormat value) {
    core::mandate::enum_to_json(j, value);
}
/// @brief Read a format without throwing.
inline void from_json(const nlohmann::json& j, SessionFormat& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::canview
