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

/// @brief The state-channel service name of one application.
///
/// @param app_name Application name reported by the child.
/// @return `Nexigen/EM/State/<app_name>`.
/// @note Spec `exec/application-state`, requirement "Report application state".
[[nodiscard]] inline std::string application_state_service(std::string_view app_name) {
    return "Nexigen/EM/State/" + std::string(app_name);
}

/// @brief Publishes ReportApplicationState notifications for one application.
///
/// Wraps a `com::EventNotifier` bound to `Nexigen/EM/State/<app>`; the event id
/// is the numeric `AppState`, so the EM's listener receives the readiness
/// notification promptly without polling. The value is move-only and owned by
/// the application's initialization path. Reporting happens once after
/// initialization and thread pinning, not from a hot callback, and `to_string`
/// is cold path only.
///
/// @note Spec `exec/application-state`, requirement "Report application state".
/// @note Spec `exec/application-state`, requirement "Application state
///       diagnostics".
/// @warning The notifier borrows the node; it must not outlive `com::Node`.
class ApplicationStateReporter {
public:
    /// @brief Open (or create) the per-application readiness channel on the
    ///        node.
    ///
    /// @param node Com node that owns the transport; must outlive the reporter.
    /// @param app_name Application name used to derive the service name.
    /// @return A ready reporter, or the com error from name creation or
    ///         notifier creation.
    /// @note Spec `exec/application-state`, requirement "Report application
    ///       state".
    [[nodiscard]] static com::Result<ApplicationStateReporter>
    create(com::Node& node, std::string_view app_name) noexcept {
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

    /// @brief Move construct a reporter, transferring the notifier.
    ApplicationStateReporter(ApplicationStateReporter&&) noexcept = default;

    /// @brief Move assign a reporter, transferring the notifier.
    /// @return The destination reporter.
    ApplicationStateReporter& operator=(ApplicationStateReporter&&) noexcept = default;

    /// @brief Report the application's current state.
    ///
    /// `Running` releases its dependents in the EM's dependency gating. The
    /// last successfully published state is cached for diagnostics.
    ///
    /// @param state State to publish; the event id is its numeric value.
    /// @return The notifier result; on success `last_state()` becomes `state`.
    /// @note Spec `exec/application-state`, requirement "Report application
    ///       state".
    /// @note Spec `exec/process-supervision`, requirement "Dependency-ordered
    ///       startup".
    [[nodiscard]] com::Status report(AppState state) noexcept {
        const auto notified =
            notifier_.notify_with(com::EventId{static_cast<std::uint64_t>(state)});
        if (notified.has_value()) {
            last_state_ = state;
        }
        return notified;
    }

    /// @brief The last state this reporter published.
    ///
    /// @return The cached state, or `AppState::Unknown` before the first
    ///         successful report.
    [[nodiscard]] AppState last_state() const noexcept { return last_state_; }

    /// @brief Cold-path diagnostic (mandate 9): the application and its state.
    ///
    /// @return `ApplicationStateReporter(app=<name>, state=<state>)`.
    /// @note Spec `exec/application-state`, requirement "Application state
    ///       diagnostics".
    [[nodiscard]] std::string to_string() const {
        return std::format("ApplicationStateReporter(app={}, state={})", app_name_,
                           exec::to_string(last_state_));
    }

private:
    /// @brief Construct a reporter from a prepared notifier.
    ///
    /// @param app_name Application name kept for diagnostics.
    /// @param notifier Bound event notifier.
    ApplicationStateReporter(std::string_view app_name, com::EventNotifier notifier) noexcept
        : app_name_(app_name), notifier_(std::move(notifier)) {}

    std::string app_name_;                   ///< Application name used in diagnostics.
    com::EventNotifier notifier_;            ///< Per-application event notifier.
    AppState last_state_{AppState::Unknown}; ///< Last successfully published state.
};

/// @brief Free helper (mandate 9): mirrors the member.
///
/// @param reporter Reporter to render.
/// @return The same text as `reporter.to_string()`.
[[nodiscard]] inline std::string to_string(const ApplicationStateReporter& reporter) {
    return reporter.to_string();
}

} // namespace nexigen::exec

/// @brief The reporter formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::exec::ApplicationStateReporter> : std::formatter<std::string_view> {
    /// @brief Format the reporter through its `to_string()` member.
    ///
    /// @tparam Ctx `std::formatter` context type.
    /// @param reporter Reporter to format.
    /// @param ctx Format context receiving the text.
    /// @return The context's output iterator.
    template <class Ctx>
    auto format(const nexigen::exec::ApplicationStateReporter& reporter, Ctx& ctx) const {
        const std::string text = reporter.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
