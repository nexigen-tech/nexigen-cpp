// nexigen::exec — PHM recovery request server implementation (change
// close-phm-safety-loop).
#include <nexigen/exec/recovery_handler.hpp>

#include <cstddef>
#include <format>
#include <utility>

namespace nexigen::exec {

namespace {

/// @brief View a fixed NUL-terminated request buffer.
template <std::size_t N>
[[nodiscard]] std::string_view view(const char (&buffer)[N]) noexcept {
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

nexigen::phm::RecoveryResponse
RecoveryHandler::handle(const nexigen::phm::RecoveryRequest& request) noexcept {
    nexigen::phm::RecoveryResponse response{};
    response.accepted = 0;
    if (request.action != nexigen::phm::RecoveryAction::RestartProcess) {
        // The watchdog belongs to PHM; the EM never actuates it.
        return response;
    }
    const std::string_view app = view(request.name);
    if (app.empty() || !suite_owns(*manifest_, app) || !restart_) {
        return response;
    }
    response.accepted = restart_(app) ? 1U : 0U;
    return response;
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
    return std::format("RecoveryHandler(apps={}, restart={})", manifest_->apps.size(),
                       restart_ ? "set" : "unset");
}

} // namespace nexigen::exec
