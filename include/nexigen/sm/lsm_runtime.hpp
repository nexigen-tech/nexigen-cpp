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
class LsmRuntime {
public:
    /// @brief Build the runtime for every configured state machine.
    [[nodiscard]] static core::Result<LsmRuntime>
    create(const SmManifest& manifest, StateManager& manager,
           com::Client<exec::StateCommand, exec::StateAck>& em_client,
           StatePublication& publication);

    LsmRuntime(LsmRuntime&&) noexcept;
    LsmRuntime& operator=(LsmRuntime&&) noexcept;
    ~LsmRuntime();

    /// @brief Whether one group is controlled by an LSM.
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
