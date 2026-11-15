// nexigen::exec — PHM recovery request server (change close-phm-safety-loop).
//
// The EM side of the PHM→EM recovery channel. The suite supervisor installs a
// restart callback; this handler validates the request against the suite
// manifest and answers accepted only when the application was actually
// relaunched. The watchdog action is rejected here because the watchdog belongs
// to PHM.
#pragma once

#include <functional>
#include <string>
#include <string_view>

#include <nexigen/com/error.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/server.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/suite_manifest.hpp>
#include <nexigen/phm/recovery_request.hpp>
#include <nexigen/phm/recovery_response.hpp>

namespace nexigen::exec {

/// @brief Serves `RecoveryRequest`/`RecoveryResponse` on the PHM recovery channel.
///
/// Owns the request-response server and a non-owning reference to the suite
/// manifest. Cold path only: one request per recovery action.
///
/// @note Spec `exec/app-suite`, requirement "PHM recovery requests are served".
class RecoveryHandler {
public:
    /// @brief Forced restart of one supervised application.
    ///
    /// Returns success only when the application was relaunched; the caller
    /// escalates on any error.
    using RestartFn = std::function<core::Status(std::string_view app)>;

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
    RecoveryHandler(RecoveryHandler&&) noexcept = default;
    RecoveryHandler& operator=(RecoveryHandler&&) noexcept = default;

    /// @brief Install the restart callback used by `RestartProcess`.
    void set_restart(RestartFn restart) noexcept;

    /// @brief Answer one request.
    ///
    /// @param request Request payload.
    /// @return `accepted=1` only when `RestartProcess` named a suite
    ///         application and the restart callback succeeded; every other
    ///         action, an unknown name, or an unset callback yields
    ///         `accepted=0`.
    [[nodiscard]] nexigen::phm::RecoveryResponse
    handle(const nexigen::phm::RecoveryRequest& request) noexcept;

    /// @brief Receive and answer one pending request, if any.
    ///
    /// @return Empty status when no request was pending or the response was
    ///         sent, or the mapped transport error.
    [[nodiscard]] com::Status serve_once() noexcept;

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const;

private:
    RecoveryHandler(
        com::Server<nexigen::phm::RecoveryRequest, nexigen::phm::RecoveryResponse> server,
        const SuiteManifest& manifest) noexcept;

    com::Server<nexigen::phm::RecoveryRequest, nexigen::phm::RecoveryResponse> server_;
    const SuiteManifest* manifest_; ///< Suite manifest; must outlive the handler.
    RestartFn restart_{};           ///< Installed restart callback.
};

/// @brief Free helper mirroring `RecoveryHandler::to_string()` (mandate 9).
[[nodiscard]] inline std::string to_string(const RecoveryHandler& handler) {
    return handler.to_string();
}

} // namespace nexigen::exec

/// @brief `std::formatter` for the recovery handler (project mandate 11).
template <>
struct std::formatter<nexigen::exec::RecoveryHandler> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::exec::RecoveryHandler& handler, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(handler.to_string(), ctx);
    }
};
