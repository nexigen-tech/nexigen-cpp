// nexigen::com — iox2 error → com::Error mapping (change add-com-module;
// spec com/service-management "Error vocabulary").
//
// Declarations only; the cold definitions live in src/com/error_mapping.cpp.
// Each overload collapses one iox2 error enum onto the small com::Error
// vocabulary; unmapped variants fall back to com::Error::Unknown.
#pragma once

#include <iox2/iceoryx2.hpp>

#include <nexigen/com/error.hpp>

namespace nexigen::com::detail {

/// @brief Map a semantic-string validation failure onto `com::Error`.
///
/// Invalid content and excessive length are both caller precondition
/// violations.
///
/// @param error Transport validation error.
/// @return `com::Error::InvalidArgument`.
/// @note Spec `com/service-management`, requirement "Error vocabulary".
[[nodiscard]] Error map_error(iox2::bb::SemanticStringError error) noexcept;

/// @brief Map a node-creation failure onto `com::Error`.
///
/// @param error Transport node-creation error.
/// @return `com::Error::CreationFailed`.
/// @note Spec `com/service-management`, requirement "Error vocabulary".
[[nodiscard]] Error map_error(iox2::NodeCreationFailure error) noexcept;

/// @brief Map a publish-subscribe creation failure onto `com::Error`.
///
/// @param error Transport service-creation error.
/// @return `ServiceAlreadyExists` when the name is taken, otherwise
///         `CreationFailed`.
/// @note Spec `com/service-management`, requirement "Error vocabulary".
[[nodiscard]] Error map_error(iox2::PublishSubscribeCreateError error) noexcept;

/// @brief Map a publish-subscribe open-or-create failure onto `com::Error`.
///
/// @param error Transport open-or-create error.
/// @return `ServiceNotFound` when an open found nothing,
///         `IncompatibleTypeLayout` on a payload mismatch, otherwise
///         `CreationFailed`.
/// @note Spec `com/service-management`, requirement "Error vocabulary"; spec
///       `com/publish-subscribe`, requirement "Type-layout mismatch
///       detection".
[[nodiscard]] Error map_error(iox2::PublishSubscribeOpenOrCreateError error) noexcept;

/// @brief Map a publisher-creation failure onto `com::Error`.
///
/// @param error Transport publisher-creation error.
/// @return `com::Error::CreationFailed`.
/// @note Spec `com/service-management`, requirement "Error vocabulary".
[[nodiscard]] Error map_error(iox2::PublisherCreateError error) noexcept;

/// @brief Map a subscriber-creation failure onto `com::Error`.
///
/// @param error Transport subscriber-creation error.
/// @return `com::Error::CreationFailed`.
/// @note Spec `com/service-management`, requirement "Error vocabulary".
[[nodiscard]] Error map_error(iox2::SubscriberCreateError error) noexcept;

/// @brief Map a shared-memory loan failure onto `com::Error`.
///
/// Out-of-memory, too many loans, and over-size loans all mean the shared pool
/// could not grant a slot.
///
/// @param error Transport loan error.
/// @return `com::Error::OutOfSamples`.
/// @note Spec `com/publish-subscribe`, requirement "Zero-copy publication".
[[nodiscard]] Error map_error(iox2::LoanError error) noexcept;

/// @brief Map a publish-send failure onto `com::Error`.
///
/// @param error Transport send error.
/// @return `ConnectionFailure` for a broken or corrupted connection,
///         `OutOfSamples` for a loan failure, otherwise `Unknown`.
/// @note Spec `com/publish-subscribe`, requirement "Non-blocking overflow".
[[nodiscard]] Error map_error(iox2::SendError error) noexcept;

/// @brief Map a receive failure onto `com::Error`.
///
/// @param error Transport receive error.
/// @return `ConnectionFailure` when the connection cannot be established or
///         mapped, otherwise `Unknown`.
/// @note Spec `com/publish-subscribe`, requirement "Subscriber receive".
[[nodiscard]] Error map_error(iox2::ReceiveError error) noexcept;

/// @brief Map a request-response open-or-create failure onto `com::Error`.
///
/// @param error Transport open-or-create error.
/// @return `ServiceNotFound` when an open found nothing,
///         `IncompatibleTypeLayout` on a request/response type mismatch,
///         otherwise `CreationFailed`.
/// @note Spec `com/request-response`, requirement "Typed client and server".
[[nodiscard]] Error map_error(iox2::RequestResponseOpenOrCreateError error) noexcept;

/// @brief Map a server-creation failure onto `com::Error`.
///
/// @param error Transport server-creation error.
/// @return `com::Error::CreationFailed`.
/// @note Spec `com/request-response`, requirement "Typed client and server".
[[nodiscard]] Error map_error(iox2::ServerCreateError error) noexcept;

/// @brief Map a client-creation failure onto `com::Error`.
///
/// @param error Transport client-creation error.
/// @return `com::Error::CreationFailed`.
/// @note Spec `com/request-response`, requirement "Typed client and server".
[[nodiscard]] Error map_error(iox2::ClientCreateError error) noexcept;

/// @brief Map an event open-or-create failure onto `com::Error`.
///
/// @param error Transport open-or-create error.
/// @return `ServiceNotFound` when an open found nothing, otherwise
///         `CreationFailed`.
/// @note Spec `com/publish-subscribe`, requirement "Event notification on
///       publish".
[[nodiscard]] Error map_error(iox2::EventOpenOrCreateError error) noexcept;

/// @brief Map a notifier-creation failure onto `com::Error`.
///
/// @param error Transport notifier-creation error.
/// @return `com::Error::CreationFailed`.
/// @note Spec `com/publish-subscribe`, requirement "Event notification on
///       publish".
[[nodiscard]] Error map_error(iox2::NotifierCreateError error) noexcept;

/// @brief Map a notification failure onto `com::Error`.
///
/// @param error Transport notify error.
/// @return `InvalidArgument` for an out-of-bounds event id, `Timeout` for a
///         missed deadline, otherwise `Unknown`.
/// @note Spec `com/publish-subscribe`, requirement "Event notification on
///       publish".
[[nodiscard]] Error map_error(iox2::NotifierNotifyError error) noexcept;

/// @brief Map a listener-creation failure onto `com::Error`.
///
/// @param error Transport listener-creation error.
/// @return `com::Error::CreationFailed`.
/// @note Spec `com/publish-subscribe`, requirement "Event notification on
///       publish".
[[nodiscard]] Error map_error(iox2::ListenerCreateError error) noexcept;

/// @brief Map a listener-wait failure onto `com::Error`.
///
/// @param error Transport listener-wait error.
/// @return `com::Error::Unknown`; the transport exposes no finer distinction.
/// @note Spec `com/publish-subscribe`, requirement "Event notification on
///       publish".
[[nodiscard]] Error map_error(iox2::ListenerWaitError error) noexcept;

/// @brief Map a request-send failure onto `com::Error`.
///
/// @param error Transport request-send error.
/// @return `OutOfSamples` when too many requests are active,
///         `ConnectionFailure` for a broken or corrupted connection, otherwise
///         `Unknown`.
/// @note Spec `com/request-response`, requirement "Asynchronous pending
///       responses".
[[nodiscard]] Error map_error(iox2::RequestSendError error) noexcept;

/// @brief Map a blackboard-creation failure onto `com::Error`.
///
/// @param error Transport blackboard-creation error.
/// @return `ServiceAlreadyExists` when the name is taken, otherwise
///         `CreationFailed`.
/// @note Spec `com/field`, requirement "Field get-set-notify".
[[nodiscard]] Error map_error(iox2::BlackboardCreateError error) noexcept;

/// @brief Map a blackboard-open failure onto `com::Error`.
///
/// @param error Transport blackboard-open error.
/// @return `ServiceNotFound` when nothing exists, `IncompatibleTypeLayout` for
///         mismatched keys, otherwise `CreationFailed`.
/// @note Spec `com/field`, requirement "Last value on connect".
[[nodiscard]] Error map_error(iox2::BlackboardOpenError error) noexcept;

/// @brief Map a writer-creation failure onto `com::Error`.
///
/// @param error Transport writer-creation error.
/// @return `com::Error::CreationFailed`.
/// @note Spec `com/field`, requirement "Field is owned by one writer".
[[nodiscard]] Error map_error(iox2::WriterCreateError error) noexcept;

/// @brief Map a reader-creation failure onto `com::Error`.
///
/// @param error Transport reader-creation error.
/// @return `com::Error::CreationFailed`.
/// @note Spec `com/field`, requirement "Field is owned by one writer".
[[nodiscard]] Error map_error(iox2::ReaderCreateError error) noexcept;

/// @brief Map a mutable-entry-handle failure onto `com::Error`.
///
/// @param error Transport entry-handle error.
/// @return `InvalidArgument` when the entry is missing or already handled,
///         otherwise `Unknown`.
/// @note Spec `com/field`, requirement "Field get-set-notify".
[[nodiscard]] Error map_error(iox2::EntryHandleMutError error) noexcept;

/// @brief Map a const-entry-handle failure onto `com::Error`.
///
/// @param error Transport entry-handle error.
/// @return `InvalidArgument` when the entry is missing, otherwise `Unknown`.
/// @note Spec `com/field`, requirement "Last value on connect".
[[nodiscard]] Error map_error(iox2::EntryHandleError error) noexcept;

/// @brief Map a wait-set creation failure onto `com::Error`.
///
/// @param error Transport wait-set creation error.
/// @return `com::Error::CreationFailed`.
/// @note Spec `com/waitset`, requirement "Multiplexed blocking wait".
[[nodiscard]] Error map_error(iox2::WaitSetCreateError error) noexcept;

/// @brief Map a wait-set attachment failure onto `com::Error`.
///
/// @param error Transport attachment error.
/// @return `OutOfSamples` when capacity is exhausted, `InvalidArgument` when
///         the source is already attached, otherwise `CreationFailed`.
/// @note Spec `com/waitset`, requirement "Attachment lifecycle".
[[nodiscard]] Error map_error(iox2::WaitSetAttachmentError error) noexcept;

/// @brief Map a wait-set run failure onto `com::Error`.
///
/// Signal-driven termination and interrupts map to `Ok` because they are
/// normal stops, not failures (design D3); the wait entry points treat that as
/// an empty success.
///
/// @param error Transport run error.
/// @return `InvalidArgument` when nothing is attached, `CreationFailed` on
///         insufficient permissions, `Ok` for termination/interrupt, otherwise
///         `Unknown`.
/// @note Spec `com/waitset`, requirement "Multiplexed blocking wait": an empty
///       wait set returns promptly instead of blocking or spinning.
[[nodiscard]] Error map_error(iox2::WaitSetRunError error) noexcept;

} // namespace nexigen::com::detail
