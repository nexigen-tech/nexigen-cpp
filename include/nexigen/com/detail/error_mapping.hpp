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

[[nodiscard]] Error map_error(iox2::bb::SemanticStringError error) noexcept;
[[nodiscard]] Error map_error(iox2::NodeCreationFailure error) noexcept;
[[nodiscard]] Error map_error(iox2::PublishSubscribeCreateError error) noexcept;
[[nodiscard]] Error map_error(iox2::PublishSubscribeOpenOrCreateError error) noexcept;
[[nodiscard]] Error map_error(iox2::PublisherCreateError error) noexcept;
[[nodiscard]] Error map_error(iox2::SubscriberCreateError error) noexcept;
[[nodiscard]] Error map_error(iox2::LoanError error) noexcept;
[[nodiscard]] Error map_error(iox2::SendError error) noexcept;
[[nodiscard]] Error map_error(iox2::ReceiveError error) noexcept;
[[nodiscard]] Error map_error(iox2::RequestResponseOpenOrCreateError error) noexcept;
[[nodiscard]] Error map_error(iox2::ServerCreateError error) noexcept;
[[nodiscard]] Error map_error(iox2::ClientCreateError error) noexcept;
[[nodiscard]] Error map_error(iox2::EventOpenOrCreateError error) noexcept;
[[nodiscard]] Error map_error(iox2::NotifierCreateError error) noexcept;
[[nodiscard]] Error map_error(iox2::NotifierNotifyError error) noexcept;
[[nodiscard]] Error map_error(iox2::ListenerCreateError error) noexcept;
[[nodiscard]] Error map_error(iox2::ListenerWaitError error) noexcept;
[[nodiscard]] Error map_error(iox2::RequestSendError error) noexcept;
[[nodiscard]] Error map_error(iox2::BlackboardCreateError error) noexcept;
[[nodiscard]] Error map_error(iox2::BlackboardOpenError error) noexcept;
[[nodiscard]] Error map_error(iox2::WriterCreateError error) noexcept;
[[nodiscard]] Error map_error(iox2::ReaderCreateError error) noexcept;
[[nodiscard]] Error map_error(iox2::EntryHandleMutError error) noexcept;
[[nodiscard]] Error map_error(iox2::EntryHandleError error) noexcept;
[[nodiscard]] Error map_error(iox2::WaitSetCreateError error) noexcept;
[[nodiscard]] Error map_error(iox2::WaitSetAttachmentError error) noexcept;
[[nodiscard]] Error map_error(iox2::WaitSetRunError error) noexcept;

} // namespace nexigen::com::detail
