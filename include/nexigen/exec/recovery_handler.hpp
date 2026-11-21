// nexigen::exec — PHM recovery request server (change close-phm-safety-loop,
// reworked by complete-phm-safety-loop).
//
// The EM side of the PHM→EM recovery channel. The answer means "admitted", not
// "restarted": admission validates the request and queues it, and the restart
// itself runs later on the control thread. A failed restart reports its outcome
// through process lifecycle events, not through the recovery answer.
#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include <nexigen/com/error.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/server.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/suite_manifest.hpp>
#include <nexigen/phm/recovery_reason.hpp>
#include <nexigen/phm/recovery_request.hpp>
#include <nexigen/phm/recovery_response.hpp>

namespace nexigen::exec {

/// @brief Serves `RecoveryRequest`/`RecoveryResponse` on the PHM recovery channel.
///
/// Owns the request-response server, a non-owning reference to the suite
/// manifest, and a small fixed queue of admitted restarts. Admission answers
/// immediately; `run_pending()` executes the queued work on the control thread.
///
/// @note Spec `exec/process-supervision`, requirement "Recovery restart is
///       admitted asynchronously".
class RecoveryHandler {
public:
    /// @brief Forced restart of one supervised application.
    ///
    /// Returns success only when the application was relaunched; the caller
    /// escalates on any error.
    using RestartFn = std::function<core::Status(std::string_view app)>;

    /// @brief Side-effect-free admission check for one application.
    ///
    /// @param app Application name from the request.
    /// @return `None` when the restart may be queued, or the reason it cannot.
    using ValidateFn = std::function<nexigen::phm::RecoveryReason(std::string_view app)>;

    /// @brief Open the recovery service.
    ///
    /// @param node Node owning the endpoint; must outlive the handler.
    /// @param name Service name shared with PHM.
    /// @param manifest Suite manifest used to resolve application names.
    /// @return The handler, or the mapped transport error.
    [[nodiscard]] static com::Result<RecoveryHandler>
    create(com::Node& node, const com::ServiceName& name, const SuiteManifest& manifest) noexcept;

    RecoveryHandler(const RecoveryHandler&) = delete;
    RecoveryHandler& operator=(const RecoveryHandler&) = delete;
    /// @brief Move-construct the handler.
    RecoveryHandler(RecoveryHandler&&) noexcept = default;
    /// @brief Move-assign the handler.
    /// @return This handler.
    RecoveryHandler& operator=(RecoveryHandler&&) noexcept = default;

    /// @brief Install the restart callback used when a queued restart executes.
    ///
    /// @param restart Callback invoked on the control thread.
    void set_restart(RestartFn restart) noexcept;

    /// @brief Install the admission check consulted before queueing a restart.
    ///
    /// @param validate Check returning `RecoveryReason::None` when the restart
    ///                 may be queued.
    void set_validate(ValidateFn validate) noexcept;

    /// @brief Admit one request and answer immediately.
    ///
    /// A `RestartProcess` naming a known application is queued and answered
    /// `Accepted`; every other shape is answered `Rejected` with a reason. The
    /// answer echoes the request's correlation id.
    ///
    /// @param request Request payload.
    /// @return The admission answer.
    /// @note Spec `exec/process-supervision`, requirements "Recovery restart is
    ///       admitted asynchronously" and "Recovery response carries a
    ///       rejection reason".
    [[nodiscard]] nexigen::phm::RecoveryResponse
    handle(const nexigen::phm::RecoveryRequest& request) noexcept;

    /// @brief Receive and answer one pending request, if any.
    ///
    /// @return Empty status when no request was pending or the response was
    ///         sent, or the mapped transport error.
    [[nodiscard]] com::Status serve_once() noexcept;

    /// @brief Execute every admitted restart on the calling thread.
    ///
    /// @return The number of restarts attempted.
    /// @note Spec `exec/process-supervision`, requirement "Recovery restart is
    ///       admitted asynchronously".
    std::size_t run_pending() noexcept;

    /// @brief Number of admitted restarts still waiting to execute.
    ///
    /// @return The occupied queue slots.
    [[nodiscard]] std::size_t pending() const noexcept { return pending_count_; }

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return Human-readable text with the service name and queue depth.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief One admitted restart waiting for the control thread.
    struct PendingRestart {
        char app[nexigen::phm::k_recovery_name_length]{}; ///< NUL-terminated application name.
    };

    /// @brief Capacity of the admitted-restart queue.
    static constexpr std::size_t k_pending_capacity = 8;

    RecoveryHandler(
        com::Server<nexigen::phm::RecoveryRequest, nexigen::phm::RecoveryResponse> server,
        const SuiteManifest& manifest) noexcept;

    /// @brief Queue one admitted application name.
    [[nodiscard]] bool enqueue(std::string_view app) noexcept;

    com::Server<nexigen::phm::RecoveryRequest, nexigen::phm::RecoveryResponse> server_;
    const SuiteManifest* manifest_; ///< Suite manifest; must outlive the handler.
    RestartFn restart_{};           ///< Installed restart callback.
    ValidateFn validate_{};         ///< Installed admission check.
    std::array<PendingRestart, k_pending_capacity> pending_{}; ///< Admitted restarts.
    std::size_t pending_count_{0};                             ///< Occupied queue slots.
};

/// @brief Free helper mirroring `RecoveryHandler::to_string()` (mandate 9).
[[nodiscard]] inline std::string to_string(const RecoveryHandler& handler) {
    return handler.to_string();
}

} // namespace nexigen::exec
