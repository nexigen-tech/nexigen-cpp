// nexigen::com — cold-path iox2 error → com::Error mapping (change
// add-com-module). Compiles only when NEXIGEN_ENABLE_COM=ON (the com library
// target is gated).
#include <nexigen/com/detail/error_mapping.hpp>

namespace nexigen::com::detail {

Error map_error(iox2::bb::SemanticStringError) noexcept {
    // InvalidContent / ExceedsMaximumLength are both caller precondition
    // violations.
    return Error::InvalidArgument;
}

Error map_error(iox2::NodeCreationFailure) noexcept {
    return Error::CreationFailed;
}

Error map_error(iox2::PublishSubscribeCreateError error) noexcept {
    switch (error) {
        case iox2::PublishSubscribeCreateError::AlreadyExists: return Error::ServiceAlreadyExists;
        default:                                               return Error::CreationFailed;
    }
}

Error map_error(iox2::PublishSubscribeOpenOrCreateError error) noexcept {
    switch (error) {
        case iox2::PublishSubscribeOpenOrCreateError::OpenDoesNotExist:
            return Error::ServiceNotFound;
        case iox2::PublishSubscribeOpenOrCreateError::OpenIncompatibleTypes:
            return Error::IncompatibleTypeLayout;
        default: return Error::CreationFailed;
    }
}

Error map_error(iox2::PublisherCreateError) noexcept {
    return Error::CreationFailed;
}

Error map_error(iox2::SubscriberCreateError) noexcept {
    return Error::CreationFailed;
}

Error map_error(iox2::LoanError) noexcept {
    // OutOfMemory / ExceedsMaxLoanedSamples / ExceedsMaxLoanSize all mean the
    // shared pool could not grant a slot.
    return Error::OutOfSamples;
}

Error map_error(iox2::SendError error) noexcept {
    switch (error) {
        case iox2::SendError::ConnectionBrokenSinceSenderNoLongerExists:
        case iox2::SendError::ConnectionCorrupted:                       return Error::ConnectionFailure;
        case iox2::SendError::LoanErrorOutOfMemory:
        case iox2::SendError::LoanErrorExceedsMaxLoans:                  return Error::OutOfSamples;
        default:                                                         return Error::Unknown;
    }
}

Error map_error(iox2::ReceiveError error) noexcept {
    switch (error) {
        case iox2::ReceiveError::FailedToEstablishConnection:
        case iox2::ReceiveError::UnableToMapSendersDataSegment: return Error::ConnectionFailure;
        default:                                                return Error::Unknown;
    }
}

Error map_error(iox2::RequestResponseOpenOrCreateError error) noexcept {
    switch (error) {
        case iox2::RequestResponseOpenOrCreateError::OpenDoesNotExist:
            return Error::ServiceNotFound;
        case iox2::RequestResponseOpenOrCreateError::OpenIncompatibleRequestOrResponseType:
            return Error::IncompatibleTypeLayout;
        default: return Error::CreationFailed;
    }
}

Error map_error(iox2::ServerCreateError) noexcept {
    return Error::CreationFailed;
}

Error map_error(iox2::ClientCreateError) noexcept {
    return Error::CreationFailed;
}

Error map_error(iox2::EventOpenOrCreateError error) noexcept {
    switch (error) {
        case iox2::EventOpenOrCreateError::OpenDoesNotExist: return Error::ServiceNotFound;
        default:                                             return Error::CreationFailed;
    }
}

Error map_error(iox2::NotifierCreateError) noexcept {
    return Error::CreationFailed;
}

Error map_error(iox2::NotifierNotifyError error) noexcept {
    switch (error) {
        case iox2::NotifierNotifyError::EventIdOutOfBounds: return Error::InvalidArgument;
        case iox2::NotifierNotifyError::MissedDeadline:     return Error::Timeout;
        default:                                            return Error::Unknown;
    }
}

Error map_error(iox2::RequestSendError error) noexcept {
    switch (error) {
        case iox2::RequestSendError::ExceedsMaxActiveRequests:                  return Error::OutOfSamples;
        case iox2::RequestSendError::ConnectionBrokenSinceSenderNoLongerExists:
        case iox2::RequestSendError::ConnectionCorrupted:                       return Error::ConnectionFailure;
        default:                                                                return Error::Unknown;
    }
}

Error map_error(iox2::BlackboardCreateError error) noexcept {
    switch (error) {
        case iox2::BlackboardCreateError::AlreadyExists: return Error::ServiceAlreadyExists;
        default:                                         return Error::CreationFailed;
    }
}

Error map_error(iox2::BlackboardOpenError error) noexcept {
    switch (error) {
        case iox2::BlackboardOpenError::DoesNotExist:     return Error::ServiceNotFound;
        case iox2::BlackboardOpenError::IncompatibleKeys: return Error::IncompatibleTypeLayout;
        default:                                          return Error::CreationFailed;
    }
}

Error map_error(iox2::WriterCreateError) noexcept {
    return Error::CreationFailed;
}

Error map_error(iox2::ReaderCreateError) noexcept {
    return Error::CreationFailed;
}

Error map_error(iox2::EntryHandleMutError error) noexcept {
    switch (error) {
        case iox2::EntryHandleMutError::EntryDoesNotExist:
        case iox2::EntryHandleMutError::HandleAlreadyExists: return Error::InvalidArgument;
        default:                                             return Error::Unknown;
    }
}

Error map_error(iox2::EntryHandleError error) noexcept {
    switch (error) {
        case iox2::EntryHandleError::EntryDoesNotExist: return Error::InvalidArgument;
        default:                                        return Error::Unknown;
    }
}

Error map_error(iox2::WaitSetCreateError) noexcept {
    return Error::CreationFailed;
}

Error map_error(iox2::WaitSetAttachmentError error) noexcept {
    switch (error) {
        case iox2::WaitSetAttachmentError::InsufficientCapacity: return Error::OutOfSamples;
        case iox2::WaitSetAttachmentError::AlreadyAttached:      return Error::InvalidArgument;
        default:
            // InternalError / InsufficientResources
            return Error::CreationFailed;
    }
}

Error map_error(iox2::WaitSetRunError error) noexcept {
    switch (error) {
        case iox2::WaitSetRunError::NoAttachments:           return Error::InvalidArgument;
        case iox2::WaitSetRunError::InsufficientPermissions: return Error::CreationFailed;
        case iox2::WaitSetRunError::InternalError:           return Error::Unknown;
        case iox2::WaitSetRunError::TerminationRequest:
        case iox2::WaitSetRunError::Interrupt:
            // Signal-driven termination is a normal stop, not a failure (D3).
            return Error::Ok;
        default: return Error::Unknown;
    }
}

Error map_error(iox2::ListenerCreateError) noexcept {
    return Error::CreationFailed;
}

Error map_error(iox2::ListenerWaitError) noexcept {
    return Error::Unknown;
}

} // namespace nexigen::com::detail
