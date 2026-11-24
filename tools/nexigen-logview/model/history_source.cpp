#include "history_source.hpp"

#include <algorithm>

namespace nexigen::logview {

namespace {

constexpr std::size_t kMaxSnapshotRecords = kMaxSnapshotRecordBytes / kSnapshotRecordBytes;

bool in_scope(const RecordView& view, std::string_view source, bool all) noexcept {
    if (!view.is_frame()) {
        return true;
    }
    if (all) {
        return true;
    }
    return view.frame->app_id_text() == source.substr(0, 16);
}

} // namespace

std::shared_ptr<FrozenHistory> FrozenHistory::capture(const RecordRing& ring,
                                                      std::string_view source, bool all) {
    auto frozen = std::shared_ptr<FrozenHistory>(new FrozenHistory());
    if (ring.size() == 0U) {
        return frozen;
    }

    // Select the newest records of the requested scope. The resulting range is
    // later materialised together with every gap record that falls inside it,
    // so gaps count toward the same budget as frames.
    std::size_t selected_frames = 0;
    std::uint64_t low = ring.tail();
    for (std::uint64_t seq = ring.tail(); seq > ring.head();) {
        --seq;
        const auto view = ring.find(seq);
        if (!view.has_value() || !view->is_frame() || !in_scope(*view, source, all)) {
            continue;
        }
        low = seq;
        ++selected_frames;
        if (selected_frames >= kMaxSnapshotRecords) {
            break;
        }
    }
    if (selected_frames == 0U) {
        if (!all || ring.size() == 0U) {
            return frozen;
        }
        low = ring.head();
    }

    frozen->frames_.reserve(kMaxSnapshotRecords);
    frozen->kinds_.reserve(kMaxSnapshotRecords);
    frozen->prev_.reserve(kMaxSnapshotRecords);
    frozen->seqs_.reserve(kMaxSnapshotRecords);

    for (std::uint64_t seq = low; seq < ring.tail(); ++seq) {
        const auto view = ring.find(seq);
        if (!view.has_value() || !in_scope(*view, source, all)) {
            continue;
        }
        frozen->frames_.push_back(*view->frame);
        frozen->kinds_.push_back(static_cast<std::uint8_t>(view->kind));
        frozen->prev_.push_back(view->is_frame() ? view->prev_same_app_ns
                                                 : static_cast<std::int64_t>(view->gap_count));
        frozen->seqs_.push_back(seq);
    }

    if (frozen->seqs_.size() > kMaxSnapshotRecords) {
        const std::size_t excess = frozen->seqs_.size() - kMaxSnapshotRecords;
        frozen->frames_.erase(frozen->frames_.begin(),
                              frozen->frames_.begin() + static_cast<std::ptrdiff_t>(excess));
        frozen->kinds_.erase(frozen->kinds_.begin(),
                             frozen->kinds_.begin() + static_cast<std::ptrdiff_t>(excess));
        frozen->prev_.erase(frozen->prev_.begin(),
                            frozen->prev_.begin() + static_cast<std::ptrdiff_t>(excess));
        frozen->seqs_.erase(frozen->seqs_.begin(),
                            frozen->seqs_.begin() + static_cast<std::ptrdiff_t>(excess));
    }
    return frozen;
}

std::optional<RecordView> FrozenHistory::find(std::uint64_t seq) const noexcept {
    const auto slot = slot_for(seq);
    if (!slot.has_value()) {
        return std::nullopt;
    }
    RecordView out{};
    out.seq = seq;
    out.kind = static_cast<RecordKind>(kinds_[*slot]);
    out.frame = &frames_[*slot];
    if (out.kind == RecordKind::Frame) {
        out.prev_same_app_ns = prev_[*slot];
    } else {
        out.gap_count = static_cast<std::uint64_t>(prev_[*slot]);
    }
    return out;
}

std::optional<std::size_t> FrozenHistory::slot_for(std::uint64_t seq) const noexcept {
    const auto it = std::lower_bound(seqs_.begin(), seqs_.end(), seq);
    if (it == seqs_.end() || *it != seq) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(seqs_.begin(), it));
}

HistorySource HistorySource::live(const RecordRing& ring) noexcept {
    HistorySource out{};
    out.live_ = &ring;
    return out;
}

HistorySource HistorySource::frozen(const FrozenHistory& history) noexcept {
    HistorySource out{};
    out.frozen_ = &history;
    return out;
}

std::uint64_t HistorySource::head() const noexcept {
    return live_ != nullptr ? live_->head() : (frozen_ != nullptr ? frozen_->head() : 0U);
}

std::uint64_t HistorySource::tail() const noexcept {
    return live_ != nullptr ? live_->tail() : (frozen_ != nullptr ? frozen_->tail() : 0U);
}

std::size_t HistorySource::size() const noexcept {
    if (live_ != nullptr) {
        return live_->tail() - live_->head();
    }
    return frozen_ != nullptr ? frozen_->size() : 0U;
}

std::size_t HistorySource::slot_count() const noexcept {
    if (live_ != nullptr) {
        return live_->capacity();
    }
    return frozen_ != nullptr ? frozen_->size() : 0U;
}

std::uint64_t HistorySource::seq_at(std::size_t index) const noexcept {
    if (live_ != nullptr) {
        return live_->head() + index;
    }
    return frozen_ != nullptr ? frozen_->seq_at(index) : 0U;
}

std::optional<RecordView> HistorySource::find(std::uint64_t seq) const noexcept {
    if (live_ != nullptr) {
        return live_->find(seq);
    }
    return frozen_ != nullptr ? frozen_->find(seq) : std::nullopt;
}

std::optional<std::size_t> HistorySource::slot_for(std::uint64_t seq) const noexcept {
    if (live_ != nullptr) {
        if (seq < live_->head() || seq >= live_->tail()) {
            return std::nullopt;
        }
        return seq & (live_->capacity() - 1U);
    }
    return frozen_ != nullptr ? frozen_->slot_for(seq) : std::nullopt;
}

bool HistorySource::resident(std::uint64_t seq) const noexcept {
    return find(seq).has_value();
}

} // namespace nexigen::logview
