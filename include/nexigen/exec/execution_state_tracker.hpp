// nexigen::exec — readiness and completion tracker (change
// introduce-platformfg-process-model; spec exec/process-types).
//
// Tracks the readiness of every manifested process so a Function Group switch
// to `Run` can be confirmed only when all bound daemons have reported `Run`
// and all bound oneshots have exited with status 0. COM-free by design: the
// state channel feeds it from the control path and it can be unit-tested
// without the iceoryx2 gate.
#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nexigen/exec/app_state.hpp>
#include <nexigen/exec/process_type.hpp>

namespace nexigen::exec {

/// @brief Per-process readiness and completion bookkeeping.
///
/// One record per manifested application. A `daemon` starts in
/// `AppState::Unknown` and becomes `Running` when it reports readiness; an
/// `oneshot` never reports and becomes `Exited` when it exits 0. Any other
/// termination (including a daemon exiting without a stop request) marks the
/// record `Failed`. A requested stop resets the record, so a later start must
/// report readiness again. Single-threaded: owned by the EM control thread.
///
/// @note Spec `exec/process-types`, requirement "Daemon readiness is
///       reported".
/// @note Spec `exec/process-types`, requirement "State change is confirmed by
///       readiness and completion".
class ExecutionStateTracker {
public:
    /// @brief Register one manifested process.
    ///
    /// Registration is idempotent: an existing record keeps its state and only
    /// its declared type is refreshed, so a repeated `SetState` does not erase
    /// a readiness report.
    ///
    /// @param name Application name.
    /// @param type Declared termination behavior.
    void register_process(std::string_view name, ProcessType type) noexcept {
        if (Record* existing = find(name); existing != nullptr) {
            existing->type = type;
            return;
        }
        processes_.push_back(Record{std::string(name), type, AppState::Unknown});
    }

    /// @brief Report the readiness of a daemon.
    ///
    /// Only a `daemon` reporting `AppState::Running` is accepted; a `oneshot`
    /// does not report readiness (Non-reporting process).
    ///
    /// @param name Application name.
    /// @param state Reported state; only `Running` confirms readiness.
    /// @return `true` when the report was accepted and recorded.
    [[nodiscard]] bool report_execution_state(std::string_view name, AppState state) noexcept {
        Record* record = find(name);
        if (record == nullptr || record->type != ProcessType::Daemon ||
            state != AppState::Running) {
            return false;
        }
        record->state = AppState::Running;
        return true;
    }

    /// @brief Record a process exit and classify it by type and status.
    ///
    /// A `oneshot` exiting 0 is a normal termination; any other exit (a
    /// non-zero `oneshot`, or any `daemon` exit without a stop request) is an
    /// unexpected self-termination.
    ///
    /// @param name Application name.
    /// @param exit_code Process exit status.
    /// @return `true` for a normal oneshot termination, `false` for a failure
    ///         or an unknown name.
    [[nodiscard]] bool on_exit(std::string_view name, int exit_code) noexcept {
        Record* record = find(name);
        if (record == nullptr) {
            return false;
        }
        if (record->type == ProcessType::Oneshot && exit_code == 0) {
            record->state = AppState::Exited;
            return true;
        }
        record->state = AppState::Failed;
        return false;
    }

    /// @brief Record a requested stop.
    ///
    /// A deliberate stop is not a self-termination, so the record returns to
    /// the unready starting state instead of `Failed`.
    ///
    /// @param name Application name.
    void on_stop(std::string_view name) noexcept {
        if (Record* record = find(name); record != nullptr) {
            record->state = AppState::Unknown;
        }
    }

    /// @brief Reset one record before a relaunch.
    ///
    /// A repeated switch may relaunch a process whose previous record was
    /// `Exited`; resetting to `Unknown` prevents the stale success from
    /// confirming the new launch before it has reported readiness or exited.
    ///
    /// @param name Application name.
    void on_start(std::string_view name) noexcept {
        if (Record* record = find(name); record != nullptr) {
            record->state = AppState::Unknown;
        }
    }

    /// @brief Current state of one process.
    ///
    /// @param name Application name.
    /// @return The recorded state, or `AppState::Unknown` for an unknown name.
    [[nodiscard]] AppState state(std::string_view name) const noexcept {
        const Record* record = find(name);
        return record == nullptr ? AppState::Unknown : record->state;
    }

    /// @brief Whether a daemon has reported readiness.
    ///
    /// @param name Application name.
    /// @return `true` only for a `daemon` whose recorded state is `Running`.
    [[nodiscard]] bool ready(std::string_view name) const noexcept {
        const Record* record = find(name);
        return record != nullptr && record->type == ProcessType::Daemon &&
               record->state == AppState::Running;
    }

    /// @brief Number of registered processes.
    ///
    /// @return The record count.
    [[nodiscard]] std::size_t process_count() const noexcept { return processes_.size(); }

    /// @brief Whether every registered process satisfies the `Run` condition.
    ///
    /// @return `true` when every daemon has reported readiness and every
    ///         oneshot has exited 0; `true` when nothing is registered.
    [[nodiscard]] bool run_confirmed() const noexcept {
        for (const Record& record : processes_) {
            if (!confirmed(record)) {
                return false;
            }
        }
        return true;
    }

    /// @brief Whether a bound subset satisfies the `Run` condition.
    ///
    /// @param names Applications bound to the switching Function Group.
    /// @return `true` when every named daemon has reported readiness and every
    ///         named oneshot has exited 0; `false` for an unknown name.
    [[nodiscard]] bool run_confirmed(std::span<const std::string> names) const noexcept {
        for (const std::string& name : names) {
            const Record* record = find(name);
            if (record == nullptr || !confirmed(*record)) {
                return false;
            }
        }
        return true;
    }

    /// @brief Cold-path diagnostic (mandate 9).
    ///
    /// @return `ExecutionStateTracker(name=type/state, ...)`.
    [[nodiscard]] std::string to_string() const {
        std::string out = "ExecutionStateTracker(";
        for (std::size_t i = 0; i < processes_.size(); ++i) {
            if (i != 0U) {
                out += ", ";
            }
            out += std::format("{}/{}/{}", processes_[i].name,
                               nexigen::exec::to_string(processes_[i].type),
                               nexigen::exec::to_string(processes_[i].state));
        }
        out += ")";
        return out;
    }

private:
    /// @brief One process's declared type and current state.
    struct Record {
        std::string name{};                    ///< Application name.
        ProcessType type{ProcessType::Daemon}; ///< Declared termination behavior.
        AppState state{AppState::Unknown};     ///< Current readiness/completion state.
    };

    /// @brief Mutable lookup by application name.
    ///
    /// @param name Application name.
    /// @return The record, or `nullptr` when unknown.
    [[nodiscard]] Record* find(std::string_view name) noexcept {
        for (Record& record : processes_) {
            if (record.name == name) {
                return &record;
            }
        }
        return nullptr;
    }

    /// @brief Const lookup by application name.
    ///
    /// @param name Application name.
    /// @return The record, or `nullptr` when unknown.
    [[nodiscard]] const Record* find(std::string_view name) const noexcept {
        for (const Record& record : processes_) {
            if (record.name == name) {
                return &record;
            }
        }
        return nullptr;
    }

    /// @brief Whether one record satisfies the confirmation condition.
    ///
    /// @param record Record to inspect.
    /// @return `true` for a ready daemon or a successfully exited oneshot.
    [[nodiscard]] static bool confirmed(const Record& record) noexcept {
        if (record.type == ProcessType::Daemon) {
            return record.state == AppState::Running;
        }
        return record.state == AppState::Exited;
    }

    std::vector<Record> processes_{}; ///< Records in registration order.
};

/// @brief Free helper (mandate 9): mirrors the member.
///
/// @param tracker Tracker to render.
/// @return The same text as `tracker.to_string()`.
[[nodiscard]] inline std::string to_string(const ExecutionStateTracker& tracker) {
    return tracker.to_string();
}

} // namespace nexigen::exec
