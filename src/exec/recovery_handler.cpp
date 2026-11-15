// nexigen::exec — PHM recovery request server implementation (change
// close-phm-safety-loop, reworked by complete-phm-safety-loop).
#include <nexigen/exec/recovery_handler.hpp>

#include <cstddef>
#include <cstdio>
#include <format>
#include <utility>

namespace nexigen::exec {

namespace {

/// @brief View a fixed NUL-terminated request buffer.
template <std::size_t N> [[nodiscard]] std::string_view view(const char (&buffer)[N]) noexcept {
    std::size_t length = 0;
    while (length < N && buffer[length] != '\0') {
        ++length;
    }
    return std::string_view(buffer, length);
}

[[nodiscard]] bool suite_owns(const SuiteManifest& manifest, std::string_view app) noexcept {
    for (const AppSpec& candidate : manifest.apps) {
        if (candidate.app == app) {
            return true;
        }
    }
    return false;
}

} // namespace

com::Result<RecoveryHandler> RecoveryHandler::create(com::Node& node, const com::ServiceName& name,
                                                     const SuiteManifest& manifest) noexcept {
    using Server = com::Server<nexigen::phm::RecoveryRequest, nexigen::phm::RecoveryResponse>;
    auto server = Server::create(node, name);
    if (!server) {
        return std::unexpected(server.error());
    }
    return RecoveryHandler{std::move(*server), manifest};
}

RecoveryHandler::RecoveryHandler(
    com::Server<nexigen::phm::RecoveryRequest, nexigen::phm::RecoveryResponse> server,
    const SuiteManifest& manifest) noexcept
    : server_(std::move(server)), manifest_(&manifest) {}

void RecoveryHandler::set_restart(RestartFn restart) noexcept {
    restart_ = std::move(restart);
}

void RecoveryHandler::set_validate(ValidateFn validate) noexcept {
    validate_ = std::move(validate);
}

bool RecoveryHandler::enqueue(std::string_view app) noexcept {
    if (pending_count_ >= pending_.size()) {
        return false;
    }
    std::snprintf(pending_[pending_count_].app, sizeof(pending_[pending_count_].app), "%.*s",
                  static_cast<int>(app.size()), app.data());
    ++pending_count_;
    return true;
}

nexigen::phm::RecoveryResponse
RecoveryHandler::handle(const nexigen::phm::RecoveryRequest& request) noexcept {
    nexigen::phm::RecoveryResponse response{};
    response.request_id = request.request_id;
    response.outcome = nexigen::phm::RecoveryOutcome::Rejected;
    response.reason = nexigen::phm::RecoveryReason::None;

    if (request.action != nexigen::phm::RecoveryAction::RestartProcess) {
        // The watchdog belongs to PHM; the EM never actuates it.
        response.reason = nexigen::phm::RecoveryReason::InvalidAction;
        return response;
    }
    const std::string_view app = view(request.name);
    if (app.empty() || !suite_owns(*manifest_, app)) {
        response.reason = nexigen::phm::RecoveryReason::UnknownProcess;
        return response;
    }
    if (!restart_) {
        response.reason = nexigen::phm::RecoveryReason::SystemError;
        return response;
    }
    if (validate_) {
        const nexigen::phm::RecoveryReason verdict = validate_(app);
        if (verdict != nexigen::phm::RecoveryReason::None) {
            response.reason = verdict;
            return response;
        }
    }
    if (!enqueue(app)) {
        response.reason = nexigen::phm::RecoveryReason::AlreadyRestarting;
        return response;
    }
    response.outcome = nexigen::phm::RecoveryOutcome::Accepted;
    return response;
}

std::size_t RecoveryHandler::run_pending() noexcept {
    std::size_t attempted = 0;
    while (pending_count_ > 0U) {
        PendingRestart entry = pending_[0];
        --pending_count_;
        for (std::size_t i = 0; i < pending_count_; ++i) {
            pending_[i] = pending_[i + 1U];
        }
        if (restart_) {
            static_cast<void>(restart_(view(entry.app)));
            ++attempted;
        }
    }
    return attempted;
}

com::Status RecoveryHandler::serve_once() noexcept {
    auto received = server_.receive();
    if (!received) {
        return std::unexpected(received.error());
    }
    if (!received->has_value()) {
        return {};
    }
    const nexigen::phm::RecoveryResponse response = handle((*received)->payload());
    return (*received)->send_copy(response);
}

std::string RecoveryHandler::to_string() const {
    return std::format("RecoveryHandler(apps={}, restart={}, pending={})", manifest_->apps.size(),
                       restart_ ? "set" : "unset", pending_count_);
}

} // namespace nexigen::exec
