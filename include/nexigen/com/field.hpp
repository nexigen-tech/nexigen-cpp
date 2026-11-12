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

/// @brief Fixed blackboard key of the Field pattern.
///
/// Every `Field` service has exactly one entry under this key. A dedicated
/// type keeps the blackboard contract distinct from plain-integer-key
/// services; `operator==` is required by the iceoryx2 creator for duplicate-key
/// detection.
///
/// @note Spec `com/field`, requirements "Field get-set-notify" and "Last value
///       on connect".
struct FieldKey {
    std::uint32_t entry{0}; ///< Entry index; always `0` for a field.
    /// @brief Compare two keys by their entry index.
    ///
    /// @return `true` when both keys name the same entry.
    bool operator==(const FieldKey&) const noexcept = default;
};

/// @brief The one and only entry key a Field service carries.
inline constexpr FieldKey k_field_entry_key{0};

/// @brief Suffix appended to the field service name for the notify channel.
inline constexpr std::string_view k_field_event_suffix = "/Event";

/// @brief Single-writer side of a field.
///
/// A field is a service-owned variable of type `T` combining Get, Set, and
/// Notify. `set()` writes the shared value and then wakes every field listener
/// through a companion event service; `get()` returns the last value this
/// writer published. The blackboard keeps history size at least one, so a late
/// `FieldListener` reads the current value immediately on connect.
///
/// Exactly one process owns the field (`create()`); other processes obtain
/// reader-only `FieldListener` handles. The writer owns the transport handles,
/// so copying is disabled and moving transfers them. `set()` must come from
/// one thread; it is a non-blocking, zero-copy shared-memory write followed by
/// a notification.
///
/// @tparam T Payload type; constrained by `com::concepts::Message`, i.e.
///         `std::is_trivially_copyable_v<T>`.
/// @note Implements `com/field`: "Field get-set-notify", "Last value on
///       connect", "Field is owned by one writer", and "Field diagnostics".
template <concepts::Message T> class Field {
public:
    /// @brief Create the field service with an initial value and become its writer.
    ///
    /// Creates the blackboard with the single entry under `k_field_entry_key`
    /// and the companion event service `<name>/Event`, then adopts the writer
    /// and entry handles used by `set()`.
    ///
    /// @param node Node that owns this process's transport membership; must
    ///        outlive the field.
    /// @param name Valid service name; listeners must use the same name.
    /// @param initial Value the field holds before the first `set()`; a reader
    ///        connecting immediately therefore sees it.
    /// @return The constructed field, or a mapped `com::Error`
    ///         (`ServiceAlreadyExists`, `CreationFailed`,
    ///         `InvalidArgument`, ...) when the blackboard or event service
    ///         cannot be created.
    /// @note Spec `com/field`, requirements "Field get-set-notify", "Last
    ///       value on connect", and "Field is owned by one writer"; the
    ///       single-writer contract starts here.
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

    /// @brief Copying is disabled: the field owns the writer handles.
    Field(const Field&) = delete;

    /// @brief Copy assignment is disabled: the field owns the writer handles.
    Field& operator=(const Field&) = delete;

    /// @brief Move-construct a field, transferring the writer handles.
    ///
    /// The update counter is copied because `std::atomic` is not movable; the
    /// moved-from field must not be used to publish.
    ///
    /// @param other Source field; after the move it no longer owns a writer.
    Field(Field&& other) noexcept
        : service_name_(std::move(other.service_name_)), writer_(std::move(other.writer_)),
          entry_(std::move(other.entry_)), notifier_(std::move(other.notifier_)),
          entry_id_(other.entry_id_), last_value_(std::move(other.last_value_)) {
        update_count_.store(other.update_count_.load(std::memory_order_relaxed),
                            std::memory_order_relaxed);
    }

    /// @brief Move-assign a field, transferring the writer handles.
    ///
    /// The destination's previous handles are released; self-assignment is a
    /// no-op.
    ///
    /// @param other Source field; left without a writer.
    /// @return `*this`.
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

    /// @brief The last value this writer has published.
    ///
    /// Single-writer semantics make the local copy exact: no shared-memory
    /// read or synchronization is involved.
    ///
    /// @return Copy of the last value passed to `set()` or `create()`.
    [[nodiscard]] T get() const noexcept { return last_value_; }

    /// @brief Update the shared value and notify every field listener.
    ///
    /// The update is visible to readers before the notification is sent, so a
    /// listener that wakes and reads sees the new value.
    ///
    /// @param value New value; copied into the shared entry and retained as the
    ///        writer's copy.
    /// @return Empty `Status` on success, or a mapped `com::Error` when the
    ///         notification cannot be delivered. The shared value is already
    ///         updated in that case.
    /// @note Spec `com/field`, requirement "Field get-set-notify": the write
    ///       and the event notification are zero-copy and non-blocking.
    ///       Single-writer: must be called from the owning process only.
    [[nodiscard]] Status set(T value) noexcept {
        entry_.update_with_copy(value);
        last_value_ = std::move(value);
        update_count_.fetch_add(1, std::memory_order_relaxed);
        return notifier_.notify_with(EventId{entry_id_});
    }

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return `Field(service=<name>, updates=<n>)`, where `updates` counts
    ///         successful `set()` calls by this writer.
    /// @note Mandate 9 (universal `to_string`); `com/field`, requirement "Field
    ///       diagnostics". Allocates, so cold path only.
    [[nodiscard]] std::string to_string() const {
        return std::format("Field(service={}, updates={})", service_name_.to_string(),
                           update_count_.load(std::memory_order_relaxed));
    }

private:
    /// @brief Adopt the created blackboard and event handles.
    ///
    /// @param name Service name retained for `to_string()`.
    /// @param writer Blackboard writer handle.
    /// @param entry Mutable handle of the single field entry.
    /// @param notifier Companion event notifier for field listeners.
    /// @param entry_id Transport entry id delivered with every notification.
    /// @param initial Initial value retained as the writer's copy.
    /// @note Private by design: the only supported construction path is
    ///       `create()`.
    Field(ServiceName name, iox2::Writer<iox2::ServiceType::Ipc, FieldKey> writer,
          iox2::EntryHandleMut<iox2::ServiceType::Ipc, FieldKey, T> entry, EventNotifier notifier,
          std::uint64_t entry_id, T initial) noexcept
        : service_name_(std::move(name)), writer_(std::move(writer)), entry_(std::move(entry)),
          notifier_(std::move(notifier)), entry_id_(entry_id), last_value_(std::move(initial)) {}

    ServiceName service_name_; ///< Service identity rendered by `to_string()`.
    iox2::Writer<iox2::ServiceType::Ipc, FieldKey> writer_;           ///< Owned writer handle.
    iox2::EntryHandleMut<iox2::ServiceType::Ipc, FieldKey, T> entry_; ///< Owned entry handle.
    EventNotifier notifier_;                                          ///< Companion notifier.
    std::uint64_t entry_id_{0};                                       ///< Notified entry id.
    T last_value_{};                                                  ///< Writer's last value.
    std::atomic<std::uint64_t> update_count_{0};                      ///< Successful set() count.
};

/// @brief Free helper mirroring `Field::to_string()`.
///
/// @tparam T Payload type satisfying `com::concepts::Message`.
/// @param field Field to render.
/// @return The field's diagnostic string.
/// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
template <concepts::Message T> [[nodiscard]] inline std::string to_string(const Field<T>& field) {
    return field.to_string();
}

/// @brief Reader-only side of a field.
///
/// `get()` always returns the current value: the blackboard keeps history size
/// at least one, so the value is available immediately on connect, even when
/// this listener joined after the last update. `wait_for_update()` blocks in
/// the kernel until the writer's next notification, so no polling is needed.
///
/// The listener cannot write the field (spec `com/field`, "Field is owned by
/// one writer"). It is move-only and must not outlive the `Node` used to open
/// it. `get()` and `wait_for_update()` must be called from one thread.
///
/// @tparam T Payload type; constrained by `com::concepts::Message`, i.e.
///         `std::is_trivially_copyable_v<T>`.
/// @note Implements `com/field`: "Field get-set-notify", "Last value on
///       connect", "Field is owned by one writer", and "Field diagnostics".
template <concepts::Message T> class FieldListener {
public:
    /// @brief Open the field service for reading and listening.
    ///
    /// Requires that `Field::create()` already created the blackboard and its
    /// companion event service; opening a missing service fails.
    ///
    /// @param node Node that owns this process's transport membership; must
    ///        outlive the listener.
    /// @param name Service name the writer used.
    /// @return The constructed listener, or a mapped `com::Error`
    ///         (`ServiceNotFound`, `ConnectionFailure`, ...) when the
    ///         blackboard or event service cannot be opened.
    /// @note Spec `com/field`, requirements "Last value on connect" and "Field
    ///       is owned by one writer": the handle is read-only.
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

    /// @brief Copying is disabled: the listener owns the reader handles.
    FieldListener(const FieldListener&) = delete;

    /// @brief Copy assignment is disabled: the listener owns the reader
    ///        handles.
    FieldListener& operator=(const FieldListener&) = delete;

    /// @brief Move-construct a listener, transferring the reader handles.
    ///
    /// The moved-from object no longer owns a handle and must not read.
    FieldListener(FieldListener&&) noexcept = default;

    /// @brief Move-assign a listener, transferring the reader handles.
    ///
    /// The destination's previous handles are released; self-assignment is a
    /// no-op.
    /// @return `*this`.
    FieldListener& operator=(FieldListener&&) noexcept = default;

    /// @brief The current value of the field.
    ///
    /// Always the latest value the writer published, including when this
    /// listener connected after the last update.
    ///
    /// @return Copy of the shared entry's current value.
    /// @note Spec `com/field`, requirement "Last value on connect".
    [[nodiscard]] T get() const noexcept {
        auto value = entry_.get();
        return *value;
    }

    /// @brief Block until the writer updates the field.
    ///
    /// The calling thread sleeps in the kernel; the shared value is not
    /// polled.
    ///
    /// @return Empty `Status` when a notification arrived, or a mapped
    ///         `com::Error` when the wait fails.
    /// @note Spec `com/field`, requirement "Field get-set-notify": listeners
    ///       wake on the change event without polling. Blocking, so do not
    ///       call it on a real-time hot path.
    [[nodiscard]] Status wait_for_update() noexcept {
        return listener_.blocking_wait([](nexigen::com::EventId) {});
    }

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return `FieldListener(service=<name>)`.
    /// @note Mandate 9 (universal `to_string`); `com/field`, requirement "Field
    ///       diagnostics". Allocates, so cold path only.
    [[nodiscard]] std::string to_string() const {
        return std::format("FieldListener(service={})", service_name_.to_string());
    }

private:
    /// @brief Adopt the opened blackboard and event handles.
    ///
    /// @param name Service name retained for `to_string()`.
    /// @param reader Blackboard reader handle.
    /// @param entry Const handle of the single field entry.
    /// @param listener Companion event listener for field updates.
    /// @note Private by design: the only supported construction path is
    ///       `open()`.
    FieldListener(ServiceName name, iox2::Reader<iox2::ServiceType::Ipc, FieldKey> reader,
                  iox2::EntryHandle<iox2::ServiceType::Ipc, FieldKey, T> entry,
                  EventListener listener) noexcept
        : service_name_(std::move(name)), reader_(std::move(reader)), entry_(std::move(entry)),
          listener_(std::move(listener)) {}

    ServiceName service_name_; ///< Service identity rendered by `to_string()`.
    iox2::Reader<iox2::ServiceType::Ipc, FieldKey> reader_;        ///< Owned reader handle.
    iox2::EntryHandle<iox2::ServiceType::Ipc, FieldKey, T> entry_; ///< Owned entry handle.
    EventListener listener_;                                       ///< Companion listener.
};

/// @brief Free helper mirroring `FieldListener::to_string()`.
///
/// @tparam T Payload type satisfying `com::concepts::Message`.
/// @param listener Listener to render.
/// @return The listener's diagnostic string.
/// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
template <concepts::Message T>
[[nodiscard]] inline std::string to_string(const FieldListener<T>& listener) {
    return listener.to_string();
}

} // namespace nexigen::com

/// @brief Formats a `Field` as its diagnostic string.
///
/// @tparam T Payload type satisfying `com::concepts::Message`.
/// @note Mandate 11 (universal `std::formatter`); delegates to
///       `Field::to_string()`, which allocates, so cold path only.
template <nexigen::com::concepts::Message T>
struct std::formatter<nexigen::com::Field<T>> : std::formatter<std::string_view> {
    /// @brief Write the field's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param field Field to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx> auto format(const nexigen::com::Field<T>& field, Ctx& ctx) const {
        const std::string text = field.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};

/// @brief Formats a `FieldListener` as its diagnostic string.
///
/// @tparam T Payload type satisfying `com::concepts::Message`.
/// @note Mandate 11 (universal `std::formatter`); delegates to
///       `FieldListener::to_string()`, which allocates, so cold path only.
template <nexigen::com::concepts::Message T>
struct std::formatter<nexigen::com::FieldListener<T>> : std::formatter<std::string_view> {
    /// @brief Write the listener's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param listener Listener to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx>
    auto format(const nexigen::com::FieldListener<T>& listener, Ctx& ctx) const {
        const std::string text = listener.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
