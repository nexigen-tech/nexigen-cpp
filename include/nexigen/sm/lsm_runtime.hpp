// nexigen::sm — LSM runtime binding to the arbitration loop
// (change p3-lsm-runtime-integration; spec sm/lsm-runtime).
//
// One LightweightStateMachine per Function Group plus the EM action sink. The
// runtime is owned by the SM arbitration thread; no atomics or locks.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <nexigen/com/client.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/state_ack.hpp>
#include <nexigen/exec/state_command.hpp>
#include <nexigen/sm/lightweight_state_machine.hpp>
#include <nexigen/sm/sm_manifest.hpp>
#include <nexigen/sm/state_manager.hpp>
#include <nexigen/sm/state_publication.hpp>

namespace nexigen::sm {

/// @brief LSM bindings and their EM/StatePublication action sink.
///
/// @note Spec `sm/lsm-runtime`, requirement "LSM Function Group binding".
class LsmRuntime {
public:
    /// @brief Build the runtime for every configured state machine.
    ///
    /// @param manifest Manifest selecting the Function Groups that use an LSM.
    /// @param manager SM arbiter; borrowed and must outlive the runtime.
    /// @param em_client EM `StateCommand` client; borrowed.
    /// @param publication State publication sink used by the action handler.
    /// @return The constructed runtime, or `core::ErrorCode::InvalidArgument`
    ///         when a manifest group is unknown or its machine cannot be built.
    [[nodiscard]] static core::Result<LsmRuntime>
    create(const SmManifest& manifest, StateManager& manager,
           com::Client<exec::StateCommand, exec::StateAck>& em_client,
           StatePublication& publication);

    /// @brief Move construction; the sink and bindings move with the runtime.
    LsmRuntime(LsmRuntime&&) noexcept;
    /// @brief Move assignment; replaces every binding owned by this runtime.
    /// @return This runtime.
    LsmRuntime& operator=(LsmRuntime&&) noexcept;
    /// @brief Release the bindings and the action sink.
    ~LsmRuntime();

    /// @brief Whether one group is controlled by an LSM.
    ///
    /// @param group_index Group index.
    /// @return `true` when a binding exists for `group_index`.
    [[nodiscard]] bool handles(std::size_t group_index) const noexcept;

    /// @brief Run the transition table for one bound group.
    ///
    /// @param group_index Group index.
    /// @param target Requested state.
    /// @param requester_id Originating requester id.
    /// @return `Success`, `InvalidGraph`, or `ActionFailed` after recovery.
    [[nodiscard]] TransitionResult request(std::size_t group_index, std::string_view target,
                                           std::string_view requester_id) noexcept;

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return A human-readable summary of the bound groups.
    [[nodiscard]] std::string to_string() const;

private:
    class Sink;

    struct Binding {
        std::size_t group_index{0};
        LightweightStateMachine machine;
    };

    LsmRuntime(std::unique_ptr<Sink> sink, StateManager& manager);

    std::unique_ptr<Sink> sink_;
    std::vector<Binding> bindings_;
    StateManager* manager_{nullptr};
};

} // namespace nexigen::sm

template <> struct std::formatter<nexigen::sm::LsmRuntime> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::sm::LsmRuntime& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};
