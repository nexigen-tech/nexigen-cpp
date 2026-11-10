// nexigen::exec — application readiness reporting (change
// add-em-orchestration; spec exec/application-state; design D1).
//
// The client API an application calls after its internal initialization and
// thread pinning: it publishes ReportApplicationState(ApplicationState::Running)
// over the iceoryx2 event pattern on the per-application channel
// `Nexigen/EM/State/<app>`. Until this notification arrives, the EM treats the
// application as still starting (dependency gating).
//
// Header-only by design (no .cpp): consumers link nexigen::com and include
// this header only behind NEXIGEN_ENABLE_COM.
#pragma once

#include <format>
#include <string>
#include <utility>

#include <nexigen/com/error.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/exec/app_state.hpp>

namespace nexigen::exec {

/// The state-channel service name of one application.
[[nodiscard]] inline std::string application_state_service(std::string_view app_name) {
    return "Nexigen/EM/State/" + std::string(app_name);
}

/// Publishes ReportApplicationState notifications for one application.
class ApplicationStateReporter {
public:
    /// Open (or create) the per-application readiness channel on the node.
    [[nodiscard]] static com::Result<ApplicationStateReporter> create(
        com::Node& node, std::string_view app_name) noexcept {
        const std::string service = application_state_service(app_name);
        const auto name = com::ServiceName::create(service.c_str());
        if (!name) {
            return std::unexpected(name.error());
        }
        auto notifier = com::EventNotifier::create(node, *name);
        if (!notifier) {
            return std::unexpected(notifier.error());
        }
        return ApplicationStateReporter{app_name, std::move(*notifier)};
    }

    ApplicationStateReporter(ApplicationStateReporter&&) noexcept = default;
    ApplicationStateReporter& operator=(ApplicationStateReporter&&) noexcept = default;

    /// Report the application's current state; `Running` releases its
    /// dependents in the EM's dependency gating.
    [[nodiscard]] com::Status report(AppState state) noexcept {
        const auto notified =
            notifier_.notify_with(com::EventId{static_cast<std::uint64_t>(state)});
        if (notified.has_value()) {
            last_state_ = state;
        }
        return notified;
    }

    /// The last state this reporter published.
    [[nodiscard]] AppState last_state() const noexcept { return last_state_; }

    /// Cold-path diagnostic (mandate 9): the application and its state.
    [[nodiscard]] std::string to_string() const {
        return std::format("ApplicationStateReporter(app={}, state={})", app_name_,
                           exec::to_string(last_state_));
    }

private:
    ApplicationStateReporter(std::string_view app_name, com::EventNotifier notifier) noexcept
        : app_name_(app_name), notifier_(std::move(notifier)) {}

    std::string app_name_;
    com::EventNotifier notifier_;
    AppState last_state_{AppState::Unknown};
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const ApplicationStateReporter& reporter) {
    return reporter.to_string();
}

} // namespace nexigen::exec

/// The reporter formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::exec::ApplicationStateReporter> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::exec::ApplicationStateReporter& reporter, Ctx& ctx) const {
        const std::string text = reporter.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
