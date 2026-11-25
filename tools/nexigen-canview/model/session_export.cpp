#include "session_export.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>

#include <pthread.h>
#include <sys/resource.h>

#include "record_meta.hpp"

namespace nexigen::canview {

namespace {

std::string quote_csv(std::string_view value) {
    const bool needs_quotes = value.find_first_of(",\"\n\r") != std::string_view::npos;
    if (!needs_quotes) {
        return std::string(value);
    }
    std::string out{"\""};
    for (const char character : value) {
        if (character == '"') {
            out += "\"\"";
        } else {
            out.push_back(character);
        }
    }
    out.push_back('"');
    return out;
}

void append_field(std::string& out, std::string_view value) {
    if (!out.empty()) {
        out.push_back(',');
    }
    out += quote_csv(value);
}

} // namespace

SessionFrameRecord session_frame_from_view(const CanRecordView& record,
                                           std::string_view channel_name, std::uint64_t seq) {
    SessionFrameRecord out{};
    out.seq = seq;
    if (record.message != nullptr) {
        out.message = *record.message;
        out.hw_ts_ns = record.message->hardware_timestamp_ns;
    }
    out.delta_id_ns = record.delta_same_id_ns;
    out.channel_name = std::string(channel_name);
    return out;
}

SessionGapRecord session_gap_from_view(const CanRecordView& record, std::string_view channel_name,
                                       std::uint64_t seq) {
    SessionGapRecord out{};
    out.seq = seq;
    out.kind = record.kind();
    out.reason = meta_gap_reason(record.meta);
    out.lost = record.gap_count();
    out.count_exact = meta_count_exact(record.meta);
    out.channel_id = record.message != nullptr ? record.message->channel_id : 0U;
    out.channel_name = std::string(channel_name);
    out.time_epoch = record.time_epoch;
    out.hw_ts_ns = record.message != nullptr ? record.message->hardware_timestamp_ns : 0U;
    return out;
}

SessionStatusRecord session_status_from_event(const StatusEvent& event,
                                              std::string_view channel_name, std::uint64_t seq) {
    SessionStatusRecord out{};
    out.seq = seq;
    out.channel_id = event.channel_id;
    out.channel_name = std::string(channel_name);
    out.status = event.status;
    return out;
}

std::string_view csv_header() noexcept {
    return "seq,hw_ts_ns,utc_est_ns,channel,channel_name,can_id,extended,kind,dlc,delta_id_ns,data,"
           "co_decoded";
}

std::string csv_row(const SessionFrameRecord& record) {
    std::string out;
    append_field(out, std::to_string(record.seq));
    append_field(out, std::to_string(record.hw_ts_ns));
    if (record.utc_est_ns.has_value()) {
        append_field(out, std::to_string(*record.utc_est_ns));
    } else {
        append_field(out, "-");
    }
    append_field(out, std::to_string(record.message.channel_id));
    append_field(out, record.channel_name);
    append_field(out, std::to_string(record.message.can_id));
    append_field(out, record.message.extended ? "true" : "false");
    append_field(out, to_string(record.message.kind));
    append_field(out, std::to_string(record.message.dlc));
    append_field(out, std::to_string(record.delta_id_ns));
    append_field(out, payload_to_hex(record.message));
    append_field(out, record.co_decoded);
    return out;
}

std::string txt_row(const SessionFrameRecord& record) {
    const std::string utc =
        record.utc_est_ns.has_value() ? std::to_string(*record.utc_est_ns) : "-";
    const std::string decoded = record.co_decoded.empty() ? "-" : record.co_decoded;
    return std::to_string(record.seq) + " | " + std::to_string(record.hw_ts_ns) + " | " + utc +
           " | " + std::to_string(record.message.channel_id) + " | " + record.channel_name + " | " +
           std::to_string(record.message.can_id) + " | " + payload_to_hex(record.message) + " | " +
           decoded;
}

AsyncExportWriter::AsyncExportWriter() = default;

AsyncExportWriter::~AsyncExportWriter() {
    cancel();
}

nexigen::core::Status AsyncExportWriter::start(const std::string& path, SessionFormat format,
                                               const CanViewSessionHeader& header) {
    if (active_.load(std::memory_order_acquire)) {
        return std::unexpected(nexigen::core::ErrorCode::Conflict);
    }
    path_ = path;
    partial_path_ = path + ".partial";
    format_ = format;
    header_ = header;
    enqueued_.store(0, std::memory_order_relaxed);
    written_.store(0, std::memory_order_relaxed);
    skipped_.store(0, std::memory_order_relaxed);
    cancelled_.store(false, std::memory_order_release);
    failed_.store(false, std::memory_order_release);
    stopping_.store(false, std::memory_order_release);
    queue_ = std::make_unique<nexigen::core::SpscRingBuffer<Item, queue_capacity>>();
    file_.open(partial_path_, std::ios::binary | std::ios::trunc);
    if (!file_) {
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    if (format_ == SessionFormat::Jsonl) {
        nlohmann::json line = header_;
        file_ << line.dump() << '\n';
    } else if (format_ == SessionFormat::Csv) {
        file_ << csv_header() << '\n';
    } else {
        file_ << "seq | hw_ts_ns | utc_est_ns | channel | channel_name | can_id | data | decoded\n";
    }
    if (!file_.good()) {
        file_.close();
        remove_partial();
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    active_.store(true, std::memory_order_release);
    worker_ = std::jthread([this](std::stop_token token) { run(token); });
    return {};
}

bool AsyncExportWriter::enqueue(const SessionFrameRecord& record) {
    if (!active_.load(std::memory_order_acquire) || queue_ == nullptr) {
        return false;
    }
    Item item{};
    item.type = CanViewRecordType::Frame;
    item.frame = record;
    if (!queue_->try_push(std::move(item))) {
        skipped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    enqueued_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool AsyncExportWriter::enqueue(const SessionGapRecord& record) {
    if (!active_.load(std::memory_order_acquire) || queue_ == nullptr) {
        return false;
    }
    Item item{};
    item.type = CanViewRecordType::Gap;
    item.gap = record;
    if (!queue_->try_push(std::move(item))) {
        skipped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    enqueued_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool AsyncExportWriter::enqueue(const SessionStatusRecord& record) {
    if (!active_.load(std::memory_order_acquire) || queue_ == nullptr) {
        return false;
    }
    Item item{};
    item.type = CanViewRecordType::Status;
    item.status = record;
    if (!queue_->try_push(std::move(item))) {
        skipped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    enqueued_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool AsyncExportWriter::enqueue_from_source(const RecordSource& source, std::uint64_t seq,
                                            std::string_view channel_name) {
    const auto record = source.find(seq);
    if (!record.has_value() || record->message == nullptr) {
        skipped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (record->is_frame()) {
        return enqueue(session_frame_from_view(*record, channel_name, seq));
    }
    return enqueue(session_gap_from_view(*record, channel_name, seq));
}

void AsyncExportWriter::finish() {
    if (!active_.load(std::memory_order_acquire)) {
        return;
    }
    stopping_.store(true, std::memory_order_release);
    if (worker_.joinable()) {
        worker_.join();
    }
    file_.flush();
    file_.close();
    active_.store(false, std::memory_order_release);
    if (cancelled_.load(std::memory_order_acquire) || failed_.load(std::memory_order_acquire)) {
        remove_partial();
        return;
    }
    if (std::rename(partial_path_.c_str(), path_.c_str()) != 0) {
        failed_.store(true, std::memory_order_release);
        remove_partial();
    }
}

void AsyncExportWriter::cancel() {
    if (!active_.load(std::memory_order_acquire)) {
        return;
    }
    cancelled_.store(true, std::memory_order_release);
    stopping_.store(true, std::memory_order_release);
    if (worker_.joinable()) {
        worker_.join();
    }
    file_.close();
    active_.store(false, std::memory_order_release);
    remove_partial();
}

AsyncExportWriter::Progress AsyncExportWriter::progress() const noexcept {
    Progress out{};
    out.enqueued = enqueued_.load(std::memory_order_relaxed);
    out.written = written_.load(std::memory_order_relaxed);
    out.skipped = skipped_.load(std::memory_order_relaxed);
    out.active = active_.load(std::memory_order_acquire);
    out.cancelled = cancelled_.load(std::memory_order_acquire);
    out.failed = failed_.load(std::memory_order_acquire);
    return out;
}

bool AsyncExportWriter::write_item(const Item& item) noexcept {
    if (format_ == SessionFormat::Jsonl) {
        nlohmann::json line;
        if (item.type == CanViewRecordType::Frame) {
            to_json(line, item.frame);
        } else if (item.type == CanViewRecordType::Gap) {
            to_json(line, item.gap);
        } else {
            to_json(line, item.status);
        }
        file_ << line.dump() << '\n';
        return file_.good();
    }
    if (item.type != CanViewRecordType::Frame) {
        file_ << "gap/status\n";
        return file_.good();
    }
    file_ << (format_ == SessionFormat::Csv ? csv_row(item.frame) : txt_row(item.frame)) << '\n';
    return file_.good();
}

void AsyncExportWriter::run(std::stop_token token) noexcept {
    static_cast<void>(::setpriority(PRIO_PROCESS, 0, 19));
    while (!token.stop_requested() && queue_ != nullptr) {
        Item item{};
        bool any = false;
        while (queue_->try_pop(item)) {
            any = true;
            if (!write_item(item)) {
                failed_.store(true, std::memory_order_release);
                stopping_.store(true, std::memory_order_release);
                return;
            }
            written_.fetch_add(1, std::memory_order_relaxed);
        }
        if (stopping_.load(std::memory_order_acquire) && !any && queue_->is_empty()) {
            return;
        }
        if (!any) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void AsyncExportWriter::remove_partial() noexcept {
    static_cast<void>(std::remove(partial_path_.c_str()));
}

} // namespace nexigen::canview
