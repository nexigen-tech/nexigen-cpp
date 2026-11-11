// nexigen::com — the Field pattern (change add-com-module; spec com/field).
//
// A Field is a service-owned variable of type T combining Get (read the
// current value), Set (update it) and Notify (event on change), backed by two
// iceoryx2 services:
//   * a blackboard named <name> that holds the single entry (history >= 1 by
//     construction, so a late subscriber reads the latest value immediately);
//   * a companion event service named <name>/Event through which the writer
//     wakes field listeners on every update — no polling.
//
// Single-writer contract: exactly one process owns the field (Field::create);
// other processes obtain reader-only handles (FieldListener::open) and can
// read and listen but cannot write.
#pragma once

#include <atomic>
#include <cstdint>
#include <format>
#include <string>
#include <utility>

#include <iox2/iceoryx2.hpp>

#include <nexigen/com/concepts/message_concept.hpp>
#include <nexigen/com/detail/error_mapping.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/event_id.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>

namespace nexigen::com {

/// The fixed blackboard key of the Field pattern: every Field service has
/// exactly one entry under this key (a dedicated type keeps the blackboard
/// contract distinct from plain-integer-key services; operator== is required
/// by the iceoryx2 creator for duplicate-key detection).
struct FieldKey {
    std::uint32_t entry{0};
    bool operator==(const FieldKey&) const noexcept = default;
};

/// The one and only entry key a Field service carries.
inline constexpr FieldKey k_field_entry_key{0};

/// The suffix appended to the field service name for the notify channel.
inline constexpr std::string_view k_field_event_suffix = "/Event";

/// Single-writer side of a field: set() updates the shared value and notifies
/// every listener; get() returns the last value known to this writer.
template <concepts::Message T> class Field {
public:
    /// Create the field service with an initial value and become its writer.
    [[nodiscard]] static Result<Field> create(Node& node, const ServiceName& name,
                                              T initial) noexcept {
        auto service = node.handle()
                           .service_builder(name.handle())
                           .blackboard_creator<FieldKey>()
                           .template add<T>(k_field_entry_key, initial)
                           .create();
        if (!service) {
            return std::unexpected(detail::map_error(service.error()));
        }
        auto writer = service->writer_builder().create();
        if (!writer) {
            return std::unexpected(detail::map_error(writer.error()));
        }
        auto entry = writer->template entry<T>(k_field_entry_key);
        if (!entry) {
            return std::unexpected(detail::map_error(entry.error()));
        }
        const std::uint64_t entry_id = static_cast<std::uint64_t>(entry->entry_id().as_value());
        const std::string event_name = name.to_string() + std::string(k_field_event_suffix);
        const auto event_service = ServiceName::create(event_name.c_str());
        if (!event_service) {
            return std::unexpected(event_service.error());
        }
        auto notifier = EventNotifier::create(node, *event_service);
        if (!notifier) {
            return std::unexpected(notifier.error());
        }
        return Field{name,     std::move(*writer), std::move(*entry), std::move(*notifier),
                     entry_id, std::move(initial)};
    }

    Field(const Field&) = delete;
    Field& operator=(const Field&) = delete;

    /// Move transfers the transport handles and the last value; the update
    /// counter is re-loaded from the source (std::atomic is not movable).
    Field(Field&& other) noexcept
        : service_name_(std::move(other.service_name_)), writer_(std::move(other.writer_)),
          entry_(std::move(other.entry_)), notifier_(std::move(other.notifier_)),
          entry_id_(other.entry_id_), last_value_(std::move(other.last_value_)) {
        update_count_.store(other.update_count_.load(std::memory_order_relaxed),
                            std::memory_order_relaxed);
    }

    Field& operator=(Field&& other) noexcept {
        if (this != &other) {
            service_name_ = std::move(other.service_name_);
            writer_ = std::move(other.writer_);
            entry_ = std::move(other.entry_);
            notifier_ = std::move(other.notifier_);
            entry_id_ = other.entry_id_;
            last_value_ = std::move(other.last_value_);
            update_count_.store(other.update_count_.load(std::memory_order_relaxed),
                                std::memory_order_relaxed);
        }
        return *this;
    }

    /// The last value this writer has published (single-writer semantics make
    /// the local copy exact).
    [[nodiscard]] T get() const noexcept { return last_value_; }

    /// Update the shared value and notify every field listener. The update is
    /// visible to readers before the notification is sent.
    [[nodiscard]] Status set(T value) noexcept {
        entry_.update_with_copy(value);
        last_value_ = std::move(value);
        update_count_.fetch_add(1, std::memory_order_relaxed);
        return notifier_.notify_with(EventId{entry_id_});
    }

    /// Cold-path diagnostic (mandate 9): service name and the last-update
    /// sequence.
    [[nodiscard]] std::string to_string() const {
        return std::format("Field(service={}, updates={})", service_name_.to_string(),
                           update_count_.load(std::memory_order_relaxed));
    }

private:
    Field(ServiceName name, iox2::Writer<iox2::ServiceType::Ipc, FieldKey> writer,
          iox2::EntryHandleMut<iox2::ServiceType::Ipc, FieldKey, T> entry, EventNotifier notifier,
          std::uint64_t entry_id, T initial) noexcept
        : service_name_(std::move(name)), writer_(std::move(writer)), entry_(std::move(entry)),
          notifier_(std::move(notifier)), entry_id_(entry_id), last_value_(std::move(initial)) {}

    ServiceName service_name_;
    iox2::Writer<iox2::ServiceType::Ipc, FieldKey> writer_;
    iox2::EntryHandleMut<iox2::ServiceType::Ipc, FieldKey, T> entry_;
    EventNotifier notifier_;
    std::uint64_t entry_id_{0};
    T last_value_{};
    std::atomic<std::uint64_t> update_count_{0};
};

/// Free helper (mandate 9): mirrors the member.
template <concepts::Message T> [[nodiscard]] inline std::string to_string(const Field<T>& field) {
    return field.to_string();
}

/// Reader-only side of a field: get() always returns the current value
/// (history >= 1, so it is available immediately on connect); wait_for_update()
/// blocks until the writer's next notification — no polling.
template <concepts::Message T> class FieldListener {
public:
    /// Open the field service for reading and listening.
    [[nodiscard]] static Result<FieldListener> open(Node& node, const ServiceName& name) noexcept {
        auto service =
            node.handle().service_builder(name.handle()).blackboard_opener<FieldKey>().open();
        if (!service) {
            return std::unexpected(detail::map_error(service.error()));
        }
        auto reader = service->reader_builder().create();
        if (!reader) {
            return std::unexpected(detail::map_error(reader.error()));
        }
        auto entry = reader->template entry<T>(k_field_entry_key);
        if (!entry) {
            return std::unexpected(detail::map_error(entry.error()));
        }
        const std::string event_name = name.to_string() + std::string(k_field_event_suffix);
        const auto event_service = ServiceName::create(event_name.c_str());
        if (!event_service) {
            return std::unexpected(event_service.error());
        }
        auto listener = EventListener::create(node, *event_service);
        if (!listener) {
            return std::unexpected(listener.error());
        }
        return FieldListener{name, std::move(*reader), std::move(*entry), std::move(*listener)};
    }

    FieldListener(const FieldListener&) = delete;
    FieldListener& operator=(const FieldListener&) = delete;
    FieldListener(FieldListener&&) noexcept = default;
    FieldListener& operator=(FieldListener&&) noexcept = default;

    /// The current value — always the latest the writer published, even when
    /// this listener connected after the last update.
    [[nodiscard]] T get() const noexcept {
        auto value = entry_.get();
        return *value;
    }

    /// Block until the writer updates the field. The listener sleeps in the
    /// kernel; it does not poll the shared value.
    [[nodiscard]] Status wait_for_update() noexcept {
        return listener_.blocking_wait([](nexigen::com::EventId) {});
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("FieldListener(service={})", service_name_.to_string());
    }

private:
    FieldListener(ServiceName name, iox2::Reader<iox2::ServiceType::Ipc, FieldKey> reader,
                  iox2::EntryHandle<iox2::ServiceType::Ipc, FieldKey, T> entry,
                  EventListener listener) noexcept
        : service_name_(std::move(name)), reader_(std::move(reader)), entry_(std::move(entry)),
          listener_(std::move(listener)) {}

    ServiceName service_name_;
    iox2::Reader<iox2::ServiceType::Ipc, FieldKey> reader_;
    iox2::EntryHandle<iox2::ServiceType::Ipc, FieldKey, T> entry_;
    EventListener listener_;
};

/// Free helper (mandate 9): mirrors the member.
template <concepts::Message T>
[[nodiscard]] inline std::string to_string(const FieldListener<T>& listener) {
    return listener.to_string();
}

} // namespace nexigen::com

/// The field formats as its diagnostic string (mandate 11).
template <nexigen::com::concepts::Message T>
struct std::formatter<nexigen::com::Field<T>> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::com::Field<T>& field, Ctx& ctx) const {
        const std::string text = field.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};

/// The field listener formats as its diagnostic string (mandate 11).
template <nexigen::com::concepts::Message T>
struct std::formatter<nexigen::com::FieldListener<T>> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::com::FieldListener<T>& listener, Ctx& ctx) const {
        const std::string text = listener.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
