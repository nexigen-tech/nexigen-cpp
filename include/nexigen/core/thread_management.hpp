// nexigen::core — cold-path CPU placement and background-thread attributes
// (change add-rt-thread-management; spec core/thread-management).
//
// CpuSet is a fixed 1024-bit value: it removes the legacy uint64 mask limit
// without introducing heap storage or a dynamic kernel-bitset dependency.
// Conversion, parsing, formatting, and application are initialization-time
// operations; no producer or worker hot path reads this type.
#pragma once

#include <array>
#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

#if defined(__linux__)
#if !defined(_GNU_SOURCE)
#error "nexigen::core::thread_management requires _GNU_SOURCE on Linux"
#endif
#include <pthread.h>
#include <sched.h>
#endif

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/detail/fault_injection.hpp>

namespace nexigen::core {

/// @brief Fixed-capacity set of CPU ids 0..1023.
///
/// A 1024-bit value with no heap storage and no dynamic kernel-bitset
/// dependency: conversion, parsing, formatting, and application are
/// initialization-time operations, and no producer or worker hot path reads
/// this type. An empty set means "preserve inherited affinity" when used by a
/// `ThreadSpec`; duplicate and out-of-range ids are configuration errors, never
/// silently deduplicated or truncated.
///
/// @note Spec `core/thread-management`, requirement "CpuSet represents CPUs
///       without the 64-bit mask limit".
class alignas(64) CpuSet {
public:
    /// @brief Number of 64-bit words in the fixed 1024-bit bitmask.
    static constexpr std::size_t word_count = 16;
    /// @brief Highest CPU id representable by this set (1023).
    static constexpr std::uint32_t max_cpu_id = 1023;

    /// @brief Construct the empty set (inherited placement).
    constexpr CpuSet() noexcept = default;

    /// @brief Add one CPU.
    ///
    /// @param cpu CPU id to add.
    /// @return `Status{}` when added; `InvalidArgument` for a negative id, an id
    ///         above 1023, or a duplicate.
    /// @note Spec `core/thread-management`, scenario "Duplicate and out-of-range IDs are rejected".
    [[nodiscard]] constexpr Status add(std::int64_t cpu) noexcept {
        if (cpu < 0 || cpu > static_cast<std::int64_t>(max_cpu_id)) {
            return std::unexpected(ErrorCode::InvalidArgument);
        }
        const auto id = static_cast<std::uint32_t>(cpu);
        const std::size_t word = id / 64U;
        const std::uint64_t bit = 1ULL << (id % 64U);
        if ((words_[word] & bit) != 0ULL) {
            return std::unexpected(ErrorCode::InvalidArgument);
        }
        words_[word] |= bit;
        return {};
    }

    /// @brief Whether `cpu` is a member; ids above 1023 are always absent.
    ///
    /// @param cpu CPU id to test.
    /// @return `true` when the bit for `cpu` is set.
    [[nodiscard]] constexpr bool contains(std::uint32_t cpu) const noexcept {
        return cpu <= max_cpu_id && (words_[cpu / 64U] & (1ULL << (cpu % 64U))) != 0ULL;
    }

    /// @brief Whether no CPU is selected.
    ///
    /// @return `true` for the empty set, which means inherited placement in a
    ///         `ThreadSpec`.
    [[nodiscard]] constexpr bool empty() const noexcept {
        for (const std::uint64_t word : words_) {
            if (word != 0ULL) {
                return false;
            }
        }
        return true;
    }

    /// @brief Number of selected CPUs (population count over all words).
    ///
    /// @return Count of set bits across the 1024-bit value.
    [[nodiscard]] constexpr std::size_t count() const noexcept {
        std::size_t total = 0;
        for (const std::uint64_t word : words_) {
            total += static_cast<std::size_t>(std::popcount(word));
        }
        return total;
    }

    /// @brief Exact conversion of the legacy 64-bit mask representation.
    ///
    /// @param mask Legacy core mask; bits 0..63 map to CPUs 0..63.
    /// @return Set with exactly the selected CPUs; a zero mask yields the valid
    ///         empty set (the legacy `pin_self(0)` error is a separate API).
    /// @note Spec `core/thread-management`, scenario "Legacy mask conversion is exact".
    [[nodiscard]] static constexpr CpuSet from_core_mask(std::uint64_t mask) noexcept {
        CpuSet out{};
        for (std::uint32_t cpu = 0; cpu < 64U; ++cpu) {
            if ((mask & (1ULL << cpu)) != 0ULL) {
                const std::size_t word = cpu / 64U;
                out.words_[word] |= 1ULL << (cpu % 64U);
            }
        }
        return out;
    }

    /// @brief Allocation-free conversion from a span of CPU ids.
    ///
    /// @param cpus CPU ids to add, in order.
    /// @return The built set on success; `InvalidArgument` when any id is
    ///         negative, above 1023, or duplicated — nothing partial is
    ///         returned.
    /// @note Spec `core/thread-management`, scenario "Duplicate and out-of-range IDs are rejected".
    [[nodiscard]] static constexpr Result<CpuSet>
    from_cpus(std::span<const std::uint32_t> cpus) noexcept {
        CpuSet out{};
        for (const std::uint32_t cpu : cpus) {
            const Status added = out.add(static_cast<std::int64_t>(cpu));
            if (!added) {
                return std::unexpected(added.error());
            }
        }
        return out;
    }

    /// @brief Cold-path compact form: ascending ids and inclusive ranges.
    ///
    /// @return Text such as `0,2-4,70`, or `[]` for the empty set.
    /// @note Spec `core/thread-management`, requirement "CpuSet has canonical
    ///       diagnostics and JSON".
    [[nodiscard]] std::string to_string() const {
        if (empty()) {
            return "[]";
        }
        std::string out{};
        std::uint32_t cpu = 0;
        bool first = true;
        while (cpu <= max_cpu_id) {
            while (cpu <= max_cpu_id && !contains(cpu)) {
                ++cpu;
            }
            if (cpu > max_cpu_id) {
                break;
            }
            const std::uint32_t begin = cpu;
            while (cpu < max_cpu_id && contains(cpu + 1U)) {
                ++cpu;
            }
            const std::uint32_t end = cpu;
            if (!first) {
                out += ',';
            }
            first = false;
            out += std::to_string(begin);
            if (end != begin) {
                out += '-';
                out += std::to_string(end);
            }
            ++cpu;
        }
        return out;
    }

    /// @brief Bit-exact equality over the full 1024-bit value.
    ///
    /// @return `true` when every word matches.
    friend constexpr bool operator==(const CpuSet&, const CpuSet&) noexcept = default;

private:
    /// @brief Raw 1024-bit storage, word 0 holding CPUs 0..63.
    std::array<std::uint64_t, word_count> words_{};
};

static_assert(sizeof(CpuSet) == 128, "CpuSet must remain a fixed 1024-bit value");
static_assert(alignof(CpuSet) == 64, "CpuSet layout is cache-line aligned");

/// @brief Free diagnostic mirror of the member `to_string` (mandate 9).
///
/// @param cpus Set to render.
/// @return The same text as `cpus.to_string()`.
/// @note Spec `core/thread-management`, requirement "CpuSet has canonical
///       diagnostics and JSON".
[[nodiscard]] inline std::string to_string(const CpuSet& cpus) {
    return cpus.to_string();
}

/// @brief Linux scheduling policies supported by the background-thread contract.
///
/// `Other` is the normal time-sharing policy, `FIFO` fixed-priority real-time,
/// and `RR` round-robin real-time.
///
/// @note Spec `core/thread-management`, requirement "Thread enums and specs
///       follow the serialization mandates".
enum class SchedulingPolicy : std::uint8_t {
    Other, ///< Normal time-sharing scheduling (`SCHED_OTHER`).
    FIFO,  ///< Fixed-priority real-time scheduling (`SCHED_FIFO`).
    RR     ///< Round-robin real-time scheduling (`SCHED_RR`).
};

/// @brief Vocabulary table for `SchedulingPolicy`
///        (`core::enum_name`/`enum_value`).
template <> struct EnumTraits<SchedulingPolicy> {
    /// @brief One table row: a policy and its canonical spelling.
    struct Entry {
        SchedulingPolicy value; ///< Policy described by the row.
        std::string_view name;  ///< Canonical lowercase wire name.
    };
    /// @brief Name table covering every enumerator.
    static constexpr std::array<Entry, 3> entries{{
        {SchedulingPolicy::Other, "other"},
        {SchedulingPolicy::FIFO, "fifo"},
        {SchedulingPolicy::RR, "rr"},
    }};
};

/// @brief Compile-time name of a scheduling policy.
///
/// @param policy Policy to render (`other`, `fifo`, `rr`).
/// @return Static-storage canonical lowercase name.
/// @note Spec `core/thread-management`, requirement "Thread enums and specs
///       follow the serialization mandates"; mandate 9.
[[nodiscard]] constexpr std::string_view to_string(SchedulingPolicy policy) noexcept {
    return enum_name(policy);
}

/// @brief Parse a canonical scheduling-policy name.
///
/// @param name Candidate spelling (the exact `to_string` output).
/// @param out Destination, untouched on any other spelling.
/// @return `true` with the policy on an exact match; `false` otherwise.
/// @note Spec `core/thread-management`, requirement "Thread enums and specs
///       follow the serialization mandates".
[[nodiscard]] constexpr bool from_string(std::string_view name, SchedulingPolicy& out) noexcept {
    return enum_value(name, out);
}

/// @brief Whether an OS rejection aborts startup or is reported as partial.
///
/// `BestEffort` attempts every requested attribute and exposes failures in the
/// report; `Required` rolls back and reports an error status.
///
/// @note Spec `core/thread-management`, requirement "Best-effort application
///       exposes partial success".
enum class ThreadApplyMode : std::uint8_t {
    BestEffort, ///< Attempt every attribute; report failures and keep running.
    Required    ///< Abort startup and roll back on any attribute failure.
};

/// @brief Vocabulary table for `ThreadApplyMode`
///        (`core::enum_name`/`enum_value`).
template <> struct EnumTraits<ThreadApplyMode> {
    /// @brief One table row: an apply mode and its canonical spelling.
    struct Entry {
        ThreadApplyMode value; ///< Mode described by the row.
        std::string_view name; ///< Canonical lowercase wire name.
    };
    /// @brief Name table covering every enumerator.
    static constexpr std::array<Entry, 2> entries{{
        {ThreadApplyMode::BestEffort, "best_effort"},
        {ThreadApplyMode::Required, "required"},
    }};
};

/// @brief Compile-time name of an apply mode.
///
/// @param mode Mode to render (`best_effort`, `required`).
/// @return Static-storage canonical lowercase name.
/// @note Spec `core/thread-management`, requirement "Thread enums and specs
///       follow the serialization mandates"; mandate 9.
[[nodiscard]] constexpr std::string_view to_string(ThreadApplyMode mode) noexcept {
    return enum_name(mode);
}

/// @brief Parse a canonical apply-mode name.
///
/// @param name Candidate spelling (the exact `to_string` output).
/// @param out Destination, untouched on any other spelling.
/// @return `true` with the mode on an exact match; `false` otherwise.
/// @note Spec `core/thread-management`, requirement "Thread enums and specs
///       follow the serialization mandates".
[[nodiscard]] constexpr bool from_string(std::string_view name, ThreadApplyMode& out) noexcept {
    return enum_value(name, out);
}

/// @brief Fixed cold-path description of background-thread attributes.
///
/// The default requests no syscall: inherited name, affinity, and scheduler are
/// preserved. Empty name and CPU set skip those operations, and the scheduler is
/// unconfigured until `set_scheduler` is called.
///
/// @note Spec `core/thread-management`, requirement "ThreadSpec validates names
///       and scheduling attributes".
class alignas(64) ThreadSpec {
public:
    /// @brief Construct an empty spec that preserves every inherited attribute.
    constexpr ThreadSpec() noexcept = default;

    /// @brief Replace the requested CPU set.
    ///
    /// @param cpus New CPU set; an empty set preserves inherited affinity.
    void set_cpus(const CpuSet& cpus) noexcept { cpus_ = cpus; }
    /// @brief Requested CPU set; empty means inherited placement.
    ///
    /// @return Reference to the stored set.
    [[nodiscard]] constexpr const CpuSet& cpus() const noexcept { return cpus_; }

    /// @brief Request a thread name.
    ///
    /// @param name Requested name; empty clears it. Linux permits at most 15
    ///        name bytes.
    /// @return `Status{}` when stored; `InvalidArgument` for input longer than
    ///         15 bytes or containing an embedded NUL (never truncated).
    /// @note Spec `core/thread-management`, scenario "Long and empty names
    ///       have deterministic behavior".
    [[nodiscard]] Status set_name(std::string_view name) noexcept {
        if (name.size() > 15U || name.find('\0') != std::string_view::npos) {
            return std::unexpected(ErrorCode::InvalidArgument);
        }
        name_.fill('\0');
        for (std::size_t i = 0; i < name.size(); ++i) {
            name_[i] = name[i];
        }
        return {};
    }

    /// @brief Requested thread name; empty means inherited name.
    ///
    /// @return View over the inline NUL-terminated storage.
    [[nodiscard]] std::string_view name() const noexcept { return std::string_view(name_.data()); }

    /// @brief NUL-terminated view for the native naming syscall.
    ///
    /// @return Pointer to the inline storage; storage remains owned by the spec.
    [[nodiscard]] const char* name_c_str() const noexcept { return name_.data(); }

    /// @brief Explicitly configure scheduling policy and priority.
    ///
    /// `Other` requires priority 0; `FIFO`/`RR` use Linux's portable real-time
    /// range 1..99. A separate presence bit allows an explicit `Other,0` reset
    /// while the default preserves inheritance.
    ///
    /// @param policy Requested scheduling policy.
    /// @param priority Requested priority (0 for `Other`, 1..99 for `FIFO`/`RR`).
    /// @return `Status{}` when stored; `InvalidArgument` for an invalid pair.
    /// @note Spec `core/thread-management`, requirement "ThreadSpec validates
    ///       names and scheduling attributes".
    [[nodiscard]] constexpr Status set_scheduler(SchedulingPolicy policy,
                                                 std::int32_t priority) noexcept {
        const Status valid = validate_scheduler(policy, priority);
        if (!valid) {
            return valid;
        }
        policy_ = policy;
        priority_ = priority;
        scheduler_configured_ = true;
        return {};
    }

    /// @brief Drop an explicit scheduler request, restoring inheritance.
    void clear_scheduler() noexcept {
        policy_ = SchedulingPolicy::Other;
        priority_ = 0;
        scheduler_configured_ = false;
    }

    /// @brief Choose best-effort (report and continue) or required (abort).
    ///
    /// @param mode Application mode to store.
    void set_apply_mode(ThreadApplyMode mode) noexcept { mode_ = mode; }

    /// @brief Requested scheduling policy; meaningful only when configured.
    ///
    /// @return The stored policy (defaults to `Other`).
    [[nodiscard]] constexpr SchedulingPolicy policy() const noexcept { return policy_; }
    /// @brief Requested real-time priority in 0..99 (0 for `Other`).
    ///
    /// @return The stored priority.
    [[nodiscard]] constexpr std::int32_t priority() const noexcept { return priority_; }
    /// @brief Best-effort or required application mode.
    ///
    /// @return The stored apply mode (defaults to `BestEffort`).
    [[nodiscard]] constexpr ThreadApplyMode apply_mode() const noexcept { return mode_; }
    /// @brief Whether `set_scheduler` requested an explicit policy.
    ///
    /// @return `true` after any successful `set_scheduler`, including an
    ///         explicit `Other,0` reset.
    [[nodiscard]] constexpr bool scheduler_configured() const noexcept {
        return scheduler_configured_;
    }

    /// @brief Whether the spec requests at least one attribute.
    ///
    /// @return `true` for a non-empty name, a non-empty CPU set, or an explicit
    ///         scheduler request; an empty spec preserves everything inherited.
    /// @note Spec `core/thread-management`, requirement "Spec emptiness and
    ///       fallback are explicit".
    [[nodiscard]] constexpr bool has_requests() const noexcept {
        return name_[0] != '\0' || !cpus_.empty() || scheduler_configured_;
    }

    /// @brief Full validation before any operating-system operation.
    ///
    /// @return `Status{}` when the spec is applicable; `InvalidArgument` for an
    ///         unknown apply mode or an invalid policy/priority pair.
    /// @note Spec `core/thread-management`, scenario "Invalid priority is
    ///       rejected before application".
    [[nodiscard]] constexpr Status validate() const noexcept {
        if (mode_ != ThreadApplyMode::BestEffort && mode_ != ThreadApplyMode::Required) {
            return std::unexpected(ErrorCode::InvalidArgument);
        }
        if (scheduler_configured_) {
            return validate_scheduler(policy_, priority_);
        }
        return {};
    }

    /// @brief Cold-path diagnostic rendering of every attribute.
    ///
    /// Includes whether the scheduler was explicitly configured.
    ///
    /// @return Text such as `ThreadSpec(name=..., cpus=..., policy=...,
    ///         priority=..., mode=..., scheduler_configured=...)`.
    /// @note Spec `core/thread-management`, requirement "Thread enums and specs
    ///       follow the serialization mandates"; mandate 9.
    [[nodiscard]] std::string to_string() const {
        return "ThreadSpec(name=" + std::string(name()) + ", cpus=" + cpus_.to_string() +
               ", policy=" + std::string(core::to_string(policy_)) +
               ", priority=" + std::to_string(priority_) +
               ", mode=" + std::string(core::to_string(mode_)) + ", scheduler_configured=" +
               (scheduler_configured_ ? std::string("true") : std::string("false")) + ")";
    }

    /// @brief Value equality over all requested attributes.
    ///
    /// Compares requested configuration, not application state.
    ///
    /// @return `true` when every stored attribute is equal.
    friend constexpr bool operator==(const ThreadSpec&, const ThreadSpec&) noexcept = default;

private:
    /// @brief Validate one policy/priority pair.
    ///
    /// @param policy Policy to validate.
    /// @param priority Priority to validate against the policy.
    /// @return `Status{}` when the pair is valid; `InvalidArgument` otherwise.
    [[nodiscard]] static constexpr Status validate_scheduler(SchedulingPolicy policy,
                                                             std::int32_t priority) noexcept {
        if (policy == SchedulingPolicy::Other) {
            return priority == 0 ? Status{} : Status{std::unexpected(ErrorCode::InvalidArgument)};
        }
        if (policy == SchedulingPolicy::FIFO || policy == SchedulingPolicy::RR) {
            return priority >= 1 && priority <= 99
                       ? Status{}
                       : Status{std::unexpected(ErrorCode::InvalidArgument)};
        }
        return std::unexpected(ErrorCode::InvalidArgument);
    }

    CpuSet cpus_{};                                     ///< Requested CPUs (offset 0, 128 bytes).
    std::array<char, 16> name_{};                       ///< Name storage, 15 bytes + NUL.
    std::int32_t priority_{0};                          ///< Scheduler priority (offset 144).
    SchedulingPolicy policy_{SchedulingPolicy::Other};  ///< Requested policy.
    ThreadApplyMode mode_{ThreadApplyMode::BestEffort}; ///< Failure semantics.
    bool scheduler_configured_{false};                  ///< Scheduler presence bit.
};

static_assert(sizeof(ThreadSpec) == 192, "ThreadSpec must keep its fixed cold layout");
static_assert(alignof(ThreadSpec) == 64, "ThreadSpec layout is cache-line aligned");

/// @brief Variant-A fallback: primary spec when it requests anything.
///
/// Otherwise the fallback is inherited (for example a daemon's `bg_thread`).
/// Neither argument is modified.
///
/// @param primary Preferred spec.
/// @param fallback Spec returned when `primary.has_requests()` is false.
/// @return `primary` when it requests at least one attribute, else `fallback`.
/// @note Spec `core/thread-management`, requirement "Spec emptiness and
///       fallback are explicit".
[[nodiscard]] constexpr ThreadSpec effective_thread_spec(const ThreadSpec& primary,
                                                         const ThreadSpec& fallback) noexcept {
    return primary.has_requests() ? primary : fallback;
}

/// @brief Cold JSON parser for a CPU array with all-or-nothing semantics.
///
/// Shared by the ADL `from_json` below and `parse_thread_spec_json`, so invalid
/// input never leaves a partially updated destination.
///
/// @param j JSON value to parse; must be an array of unsigned CPU ids.
/// @param out Destination, assigned only when the whole array is valid.
/// @return `true` when `j` is a valid array and `out` was replaced; `false` for
///         a non-array, a non-numeric entry, an id above 1023, or a duplicate.
/// @note Spec `core/thread-management`, scenario "Invalid CpuSet JSON leaves
///       the destination unchanged".
[[nodiscard]] inline bool parse_cpu_set_json(const nlohmann::json& j, CpuSet& out) noexcept {
    if (!j.is_array()) {
        return false;
    }
    CpuSet parsed{};
    for (const auto& item : j) {
        if (!item.is_number_unsigned()) {
            return false;
        }
        const std::uint64_t cpu = item.get<std::uint64_t>();
        if (cpu > CpuSet::max_cpu_id) {
            return false;
        }
        if (!parsed.add(static_cast<std::int64_t>(cpu))) {
            return false;
        }
    }
    out = parsed;
    return true;
}

/// @brief ADL JSON writer for a CPU set: a sorted array of numeric CPU ids.
///
/// @param j JSON value overwritten with the array.
/// @param cpus Set to serialize.
/// @note Spec `core/thread-management`, requirement "CpuSet has canonical
///       diagnostics and JSON"; mandate 10.
inline void to_json(nlohmann::json& j, const CpuSet& cpus) {
    j = nlohmann::json::array();
    for (std::uint32_t cpu = 0; cpu <= CpuSet::max_cpu_id; ++cpu) {
        if (cpus.contains(cpu)) {
            j.push_back(cpu);
        }
    }
}

/// @brief ADL JSON reader for a CPU set, non-throwing and all-or-nothing.
///
/// @param j JSON value to parse.
/// @param out Destination, untouched when parsing rejects the input.
/// @note Spec `core/thread-management`, scenario "Invalid CpuSet JSON leaves
///       the destination unchanged"; mandate 10.
inline void from_json(const nlohmann::json& j, CpuSet& out) noexcept {
    static_cast<void>(parse_cpu_set_json(j, out));
}

/// @brief ADL JSON writer for a scheduling policy: its canonical name.
///
/// @param j JSON value overwritten with the string.
/// @param policy Policy to serialize.
/// @note Spec `core/thread-management`, requirement "Thread enums and specs
///       follow the serialization mandates"; mandate 10.
inline void to_json(nlohmann::json& j, const SchedulingPolicy policy) {
    mandate::enum_to_json(j, policy);
}

/// @brief ADL JSON reader for a scheduling policy, non-throwing.
///
/// @param j JSON value to parse.
/// @param out Destination, untouched on a non-string or unknown name.
/// @note Spec `core/thread-management`, requirement "Thread enums and specs
///       follow the serialization mandates"; mandate 10.
inline void from_json(const nlohmann::json& j, SchedulingPolicy& out) noexcept {
    mandate::enum_from_json(j, out);
}

/// @brief ADL JSON writer for an apply mode: its canonical name.
///
/// @param j JSON value overwritten with the string.
/// @param mode Apply mode to serialize.
/// @note Spec `core/thread-management`, requirement "Thread enums and specs
///       follow the serialization mandates"; mandate 10.
inline void to_json(nlohmann::json& j, const ThreadApplyMode mode) {
    mandate::enum_to_json(j, mode);
}

/// @brief ADL JSON reader for an apply mode, non-throwing.
///
/// @param j JSON value to parse.
/// @param out Destination, untouched on a non-string or unknown name.
/// @note Spec `core/thread-management`, requirement "Thread enums and specs
///       follow the serialization mandates"; mandate 10.
inline void from_json(const nlohmann::json& j, ThreadApplyMode& out) noexcept {
    mandate::enum_from_json(j, out);
}

/// @brief ADL JSON writer for a thread spec.
///
/// Emits `name`, `cpus`, and `mode`, plus `policy`/`priority` only when the
/// scheduler was explicitly configured: an omitted pair preserves inheritance,
/// while an explicit `Other,0` pair is a real reset request and round-trips as
/// configured.
///
/// @param j JSON value overwritten with the object.
/// @param spec Spec to serialize.
/// @note Spec `core/thread-management`, scenario "ThreadSpec round-trips all
///       attributes"; mandate 10.
inline void to_json(nlohmann::json& j, const ThreadSpec& spec) {
    j = nlohmann::json{
        {"name", std::string(spec.name())},
        {"cpus", spec.cpus()},
        {"mode", spec.apply_mode()},
    };
    // Scheduler presence is represented by the presence of these fields: an
    // omitted pair preserves inheritance, while an explicit Other,0 pair is a
    // real reset request and therefore round-trips as configured.
    if (spec.scheduler_configured()) {
        j["policy"] = spec.policy();
        j["priority"] = spec.priority();
    }
}

namespace thread_management_detail {

/// @brief Parse the optional `name` field into a staging spec.
///
/// @param j JSON object holding the field.
/// @param parsed Staging spec updated only when the field is valid.
/// @return `true` when the field is absent or a valid name; `false` for a
///         non-string, overlong, or NUL-containing name.
[[nodiscard]] inline bool parse_name(const nlohmann::json& j, ThreadSpec& parsed) noexcept {
    const auto name = j.find("name");
    if (name == j.end()) {
        return true;
    }
    const auto* const value = name->get_ptr<const nlohmann::json::string_t*>();
    if (value == nullptr) {
        return false;
    }
    return parsed.set_name(std::string_view(*value)).has_value();
}

/// @brief Parse the optional `cpus` field into a staging spec.
///
/// @param j JSON object holding the field.
/// @param parsed Staging spec updated only when the array is fully valid.
/// @return `true` when the field is absent or a valid CPU array; `false` when
///         the set parser rejects it (duplicate, out-of-range, non-numeric).
[[nodiscard]] inline bool parse_cpus(const nlohmann::json& j, ThreadSpec& parsed) noexcept {
    const auto cpus = j.find("cpus");
    if (cpus == j.end()) {
        return true;
    }
    CpuSet converted = parsed.cpus();
    if (!parse_cpu_set_json(*cpus, converted)) {
        return false;
    }
    parsed.set_cpus(converted);
    return true;
}

/// @brief Parse the optional `mode` field into a staging spec.
///
/// @param j JSON object holding the field.
/// @param parsed Staging spec updated only when the mode parses.
/// @return `true` when the field is absent or a canonical mode name; `false`
///         for a non-string or unknown name.
[[nodiscard]] inline bool parse_mode(const nlohmann::json& j, ThreadSpec& parsed) noexcept {
    const auto mode = j.find("mode");
    if (mode == j.end()) {
        return true;
    }
    const auto* const value = mode->get_ptr<const nlohmann::json::string_t*>();
    ThreadApplyMode converted = parsed.apply_mode();
    if (value == nullptr || !from_string(std::string_view(*value), converted)) {
        return false;
    }
    parsed.set_apply_mode(converted);
    return true;
}

/// @brief Parse the optional `policy`/`priority` pair into a staging spec.
///
/// Both fields participate: a present pair is validated against the
/// `Other=0`, `FIFO`/`RR=1..99` rule and recorded as explicitly configured.
///
/// @param j JSON object holding the fields.
/// @param parsed Staging spec updated only when the pair is valid.
/// @return `true` when both fields are absent or the pair is valid; `false`
///         for an unknown policy or an out-of-range priority.
[[nodiscard]] inline bool parse_scheduler(const nlohmann::json& j, ThreadSpec& parsed) noexcept {
    const auto policy = j.find("policy");
    const auto priority = j.find("priority");
    if (policy == j.end() && priority == j.end()) {
        return true;
    }
    SchedulingPolicy converted_policy = parsed.policy();
    std::int64_t converted_priority = parsed.priority();
    if (policy != j.end()) {
        const auto* const value = policy->get_ptr<const nlohmann::json::string_t*>();
        if (value == nullptr || !from_string(std::string_view(*value), converted_policy)) {
            return false;
        }
    }
    if (priority != j.end()) {
        if (!priority->is_number_integer() && !priority->is_number_unsigned()) {
            return false;
        }
        if (priority->is_number_unsigned() && priority->get<std::uint64_t>() > 99ULL) {
            return false;
        }
        converted_priority = priority->get<std::int64_t>();
        if (converted_priority < 0 || converted_priority > 99) {
            return false;
        }
    }
    return parsed.set_scheduler(converted_policy, static_cast<std::int32_t>(converted_priority))
        .has_value();
}

} // namespace thread_management_detail

/// @brief Cold JSON parser for a thread spec with all-or-nothing semantics.
///
/// Parses each field through its dedicated helper against a staging copy, so a
/// bad field never mutates the caller's spec.
///
/// @param j JSON value to parse; must be an object.
/// @param out Destination, replaced only when every present field is valid.
/// @return `true` when `j` was parsed and `out` replaced; `false` for a
///         non-object or any invalid field.
/// @note Spec `core/thread-management`, requirement "parse_thread_spec_json
///       delegates to field parsers".
[[nodiscard]] inline bool parse_thread_spec_json(const nlohmann::json& j,
                                                 ThreadSpec& out) noexcept {
    if (!j.is_object()) {
        return false;
    }
    ThreadSpec parsed = out;
    if (!thread_management_detail::parse_name(j, parsed) ||
        !thread_management_detail::parse_cpus(j, parsed) ||
        !thread_management_detail::parse_mode(j, parsed) ||
        !thread_management_detail::parse_scheduler(j, parsed)) {
        return false;
    }
    out = parsed;
    return true;
}

/// @brief ADL JSON reader for a thread spec, non-throwing.
///
/// @param j JSON value to parse.
/// @param out Destination, untouched when parsing rejects the input.
/// @note Spec `core/thread-management`, scenario "Invalid spec JSON is non-throwing"; mandate 10.
inline void from_json(const nlohmann::json& j, ThreadSpec& out) noexcept {
    static_cast<void>(parse_thread_spec_json(j, out));
}

/// @brief Free diagnostic mirror of the member `to_string` (mandate 9).
///
/// @param spec Spec to render.
/// @return The same text as `spec.to_string()`.
/// @note Spec `core/thread-management`, requirement "Thread enums and specs
///       follow the serialization mandates".
[[nodiscard]] inline std::string to_string(const ThreadSpec& spec) {
    return spec.to_string();
}

namespace thread_management_detail {

#if defined(__linux__)
/// @brief Convert a `CpuSet` into the native kernel `cpu_set_t`.
///
/// The single value-to-native conversion used by the legacy affinity adapter and
/// the full thread application path; fixed `CPU_SETSIZE` storage, no heap.
///
/// @param cpus Source set to translate.
/// @param native Native set overwritten with the selected CPUs.
inline void fill_native_cpu_set(const CpuSet& cpus, cpu_set_t& native) noexcept {
    CPU_ZERO(&native);
    for (std::uint32_t cpu = 0; cpu <= CpuSet::max_cpu_id; ++cpu) {
        if (cpus.contains(cpu)) {
            CPU_SET(cpu, &native);
        }
    }
}
#endif

} // namespace thread_management_detail

class ThreadApplyReport;

namespace thread_management_detail {

#if defined(__linux__)
/// @brief Snapshot of the attributes inherited before an application.
struct ThreadStateSnapshot;

/// @brief Record one failed attribute class in the report.
void record_apply_failure(ThreadApplyReport&, std::uint32_t) noexcept;
/// @brief Apply the requested thread name, recording the outcome.
void apply_name(const ThreadSpec&, ThreadApplyReport&) noexcept;
/// @brief Apply the requested CPU affinity, recording the outcome.
void apply_affinity(const ThreadSpec&, ThreadApplyReport&) noexcept;
/// @brief Apply the requested scheduling policy, recording the outcome.
void apply_scheduler(const ThreadSpec&, ThreadApplyReport&) noexcept;
/// @brief Restore previously applied attributes in reverse order.
void rollback_applied_attributes(const ThreadStateSnapshot&, std::uint32_t,
                                 ThreadApplyReport&) noexcept;
#endif

} // namespace thread_management_detail

/// @brief Immutable startup result of applying a `ThreadSpec`.
///
/// Bit masks describe Name/Affinity/Scheduler operations; `applied` means the
/// operation succeeded during this application, even when a later `Required`
/// failure rolled it back. Published once at startup, so it adds no per-record
/// atomic traffic.
///
/// @note Spec `core/thread-management`, requirement "ThreadApplyReport is a
///       serializable startup snapshot".
class alignas(64) ThreadApplyReport {
public:
    /// @brief Bit set in `requested`/`applied`/`failed` for the thread name.
    static constexpr std::uint32_t name_mask = 1U;
    /// @brief Bit set in `requested`/`applied`/`failed` for CPU affinity.
    static constexpr std::uint32_t affinity_mask = 2U;
    /// @brief Bit set in `requested`/`applied`/`failed` for scheduling.
    static constexpr std::uint32_t scheduler_mask = 4U;

    /// @brief Attributes the spec asked to change.
    ///
    /// @return Union of the mask constants the spec requested.
    [[nodiscard]] constexpr std::uint32_t requested() const noexcept { return requested_; }
    /// @brief Attributes successfully applied (even if later rolled back).
    ///
    /// @return Union of the mask constants that succeeded.
    [[nodiscard]] constexpr std::uint32_t applied() const noexcept { return applied_; }
    /// @brief Attributes the kernel rejected during this attempt.
    ///
    /// @return Union of the mask constants that failed.
    [[nodiscard]] constexpr std::uint32_t failed() const noexcept { return failed_; }
    /// @brief Applied attributes that could not be restored after a failure.
    ///
    /// @return Union of the mask constants whose rollback failed.
    [[nodiscard]] constexpr std::uint32_t rollback_failed() const noexcept {
        return rollback_failed_;
    }
    /// @brief First error observed; `Ok` when every requested attribute applied.
    ///
    /// @return The first failure code, or `ErrorCode::Ok` for a clean start.
    [[nodiscard]] constexpr ErrorCode first_error() const noexcept { return first_error_; }
    /// @brief Whether no attribute failed and no rollback failed.
    ///
    /// @return `true` when `failed()` and `rollback_failed()` are both zero.
    [[nodiscard]] constexpr bool complete() const noexcept {
        return failed_ == 0U && rollback_failed_ == 0U;
    }

    /// @brief Cold-path mask/error rendering.
    ///
    /// See `nexigen::exec::describe_thread_apply_report` for the
    /// operator-facing attribute names.
    ///
    /// @return Text naming every mask and the first error.
    /// @note Spec `core/thread-management`, requirement "ThreadApplyReport is a
    ///       serializable startup snapshot"; mandate 9.
    [[nodiscard]] std::string to_string() const {
        return "ThreadApplyReport(requested=" + std::to_string(requested_) +
               ", applied=" + std::to_string(applied_) + ", failed=" + std::to_string(failed_) +
               ", rollback_failed=" + std::to_string(rollback_failed_) +
               ", first_error=" + std::string(core::to_string(first_error_)) + ")";
    }

    /// @brief Value equality over every mask and the first error.
    ///
    /// @return `true` when all fields match.
    friend constexpr bool operator==(const ThreadApplyReport&,
                                     const ThreadApplyReport&) noexcept = default;
    /// @brief All-or-nothing JSON restore; invalid input leaves the target
    ///        untouched.
    friend void from_json(const nlohmann::json&, ThreadApplyReport&) noexcept;
    /// @brief Apply a spec to the calling thread and fill this report.
    friend Status apply_thread_spec_self(const ThreadSpec&, ThreadApplyReport&) noexcept;
#if defined(__linux__)
    /// @brief Record one failed attribute class (implementation detail).
    friend void thread_management_detail::record_apply_failure(ThreadApplyReport&,
                                                               std::uint32_t) noexcept;
    /// @brief Apply the thread name (implementation detail).
    friend void thread_management_detail::apply_name(const ThreadSpec&,
                                                     ThreadApplyReport&) noexcept;
    /// @brief Apply CPU affinity (implementation detail).
    friend void thread_management_detail::apply_affinity(const ThreadSpec&,
                                                         ThreadApplyReport&) noexcept;
    /// @brief Apply the scheduler (implementation detail).
    friend void thread_management_detail::apply_scheduler(const ThreadSpec&,
                                                          ThreadApplyReport&) noexcept;
    /// @brief Roll back applied attributes (implementation detail).
    friend void thread_management_detail::rollback_applied_attributes(
        const thread_management_detail::ThreadStateSnapshot&, std::uint32_t,
        ThreadApplyReport&) noexcept;
#endif

private:
    std::uint32_t requested_{0};           ///< Mask of requested attributes.
    std::uint32_t applied_{0};             ///< Mask of successfully applied attributes.
    std::uint32_t failed_{0};              ///< Mask of rejected attributes.
    std::uint32_t rollback_failed_{0};     ///< Mask of attributes whose rollback failed.
    ErrorCode first_error_{ErrorCode::Ok}; ///< First error observed, `Ok` when clean.
};

static_assert(sizeof(ThreadApplyReport) == 64,
              "ThreadApplyReport must remain one cold-start cache line");
static_assert(alignof(ThreadApplyReport) == 64);

/// @brief Free diagnostic mirror of the member `to_string` (mandate 9).
///
/// @param report Report to render.
/// @return The same text as `report.to_string()`.
/// @note Spec `core/thread-management`, requirement "ThreadApplyReport is a
///       serializable startup snapshot".
[[nodiscard]] inline std::string to_string(const ThreadApplyReport& report) {
    return report.to_string();
}

/// @brief ADL JSON writer for a startup report.
///
/// @param j JSON value overwritten with the five report fields.
/// @param report Report to serialize.
/// @note Spec `core/thread-management`, requirement "ThreadApplyReport is a
///       serializable startup snapshot"; mandate 10.
inline void to_json(nlohmann::json& j, const ThreadApplyReport& report) {
    j = nlohmann::json{
        {"requested", report.requested()},     {"applied", report.applied()},
        {"failed", report.failed()},           {"rollback_failed", report.rollback_failed()},
        {"first_error", report.first_error()},
    };
}

/// @brief ADL JSON reader for a startup report, all-or-nothing and
///        non-throwing.
///
/// @param j JSON value to parse; a non-object is ignored.
/// @param out Destination, untouched unless every present field parses.
/// @note Spec `core/thread-management`, requirement "ThreadApplyReport is a
///       serializable startup snapshot"; mandate 10.
inline void from_json(const nlohmann::json& j, ThreadApplyReport& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    ThreadApplyReport parsed = out;
    auto read_mask = [&](const char* key, std::uint32_t& destination) {
        const auto it = j.find(key);
        if (it == j.end()) {
            return true;
        }
        if (!it->is_number_unsigned() || it->get<std::uint64_t>() > 0xffffffffULL) {
            return false;
        }
        destination = static_cast<std::uint32_t>(it->get<std::uint64_t>());
        return true;
    };
    if (!read_mask("requested", parsed.requested_) || !read_mask("applied", parsed.applied_) ||
        !read_mask("failed", parsed.failed_) ||
        !read_mask("rollback_failed", parsed.rollback_failed_)) {
        return;
    }
    const auto error = j.find("first_error");
    if (error != j.end()) {
        const auto* const name = error->get_ptr<const nlohmann::json::string_t*>();
        if (name == nullptr || !core::from_string(std::string_view(*name), parsed.first_error_)) {
            return;
        }
    }
    out = parsed;
}

#if defined(__linux__)
namespace thread_management_detail {

/// @brief Attributes captured before an application, for rollback.
struct ThreadStateSnapshot {
    cpu_set_t affinity{};     ///< Affinity inherited before application.
    char name[16]{};          ///< Thread name before application.
    int policy = SCHED_OTHER; ///< Native scheduling policy before application.
    sched_param scheduler{};  ///< Native scheduling parameters before application.
};

/// @brief Set the calling thread's name, honoring the fault hook.
///
/// @param name NUL-terminated name to apply.
/// @return 0 on success, or the pthread/native error number.
[[nodiscard]] inline int set_name(const char* name) noexcept {
    if (fault_injection::fault(fault_injection::FaultPoint::Thread_set_name)) {
        return EPERM;
    }
    return ::pthread_setname_np(::pthread_self(), name);
}

/// @brief Set the calling thread's affinity, honoring the fault hook.
///
/// @param cpus Native set to apply.
/// @return 0 on success, or the pthread/native error number.
[[nodiscard]] inline int set_affinity(const cpu_set_t& cpus) noexcept {
    if (fault_injection::fault(fault_injection::FaultPoint::Thread_set_affinity)) {
        return EINVAL;
    }
    return ::pthread_setaffinity_np(::pthread_self(), sizeof(cpus), &cpus);
}

/// @brief Apply a scheduling policy to the calling thread, honoring the hook.
///
/// @param policy Native policy constant (for example `SCHED_FIFO`).
/// @param scheduler Native scheduling parameters.
/// @return 0 on success, or the native error number.
[[nodiscard]] inline int set_scheduler(int policy, const sched_param& scheduler) noexcept {
    if (fault_injection::fault(fault_injection::FaultPoint::Thread_set_scheduler)) {
        return EPERM;
    }
    return ::sched_setscheduler(0, policy, &scheduler);
}

/// @brief Restore a thread name, honoring the fault hook.
///
/// @param name NUL-terminated name to restore.
/// @return `true` when the name was restored.
[[nodiscard]] inline bool restore_name(const char* name) noexcept {
    if (fault_injection::fault(fault_injection::FaultPoint::Thread_restore_name)) {
        return false;
    }
    return ::pthread_setname_np(::pthread_self(), name) == 0;
}

/// @brief Restore a CPU affinity, honoring the fault hook.
///
/// @param cpus Native set to restore.
/// @return `true` when the affinity was restored.
[[nodiscard]] inline bool restore_affinity(const cpu_set_t& cpus) noexcept {
    if (fault_injection::fault(fault_injection::FaultPoint::Thread_restore_affinity)) {
        return false;
    }
    return ::pthread_setaffinity_np(::pthread_self(), sizeof(cpus), &cpus) == 0;
}

/// @brief Restore a scheduling policy, honoring the fault hook.
///
/// @param policy Native policy constant to restore.
/// @param scheduler Native scheduling parameters to restore.
/// @return `true` when the scheduler was restored.
[[nodiscard]] inline bool restore_scheduler(int policy, const sched_param& scheduler) noexcept {
    if (fault_injection::fault(fault_injection::FaultPoint::Thread_restore_scheduler)) {
        return false;
    }
    return ::sched_setscheduler(0, policy, &scheduler) == 0;
}

/// @brief Capture the calling thread's name, affinity, and scheduler.
///
/// @param out Snapshot filled on success; untouched on failure.
/// @return `true` when all three attributes were read.
[[nodiscard]] inline bool snapshot_current(ThreadStateSnapshot& out) noexcept {
    if (fault_injection::fault(fault_injection::FaultPoint::Thread_snapshot)) {
        return false;
    }
    const pthread_t self = ::pthread_self();
    if (::pthread_getname_np(self, out.name, sizeof(out.name)) != 0 ||
        ::pthread_getaffinity_np(self, sizeof(out.affinity), &out.affinity) != 0) {
        return false;
    }
    const int policy = ::sched_getscheduler(0);
    if (policy < 0) {
        return false;
    }
    out.policy = policy;
    out.scheduler.sched_priority = 0;
    return ::sched_getparam(0, &out.scheduler) == 0;
}

/// @brief Restore the scheduler from a snapshot.
///
/// @param snapshot Captured state to restore from.
/// @return `true` when the scheduler was restored.
[[nodiscard]] inline bool restore_scheduler(const ThreadStateSnapshot& snapshot) noexcept {
    return restore_scheduler(snapshot.policy, snapshot.scheduler);
}

/// @brief Restore the affinity from a snapshot.
///
/// @param snapshot Captured state to restore from.
/// @return `true` when the affinity was restored.
[[nodiscard]] inline bool restore_affinity(const ThreadStateSnapshot& snapshot) noexcept {
    return restore_affinity(snapshot.affinity);
}

/// @brief Restore the thread name from a snapshot.
///
/// @param snapshot Captured state to restore from.
/// @return `true` when the name was restored.
[[nodiscard]] inline bool restore_name(const ThreadStateSnapshot& snapshot) noexcept {
    return restore_name(snapshot.name);
}

/// @brief Map a `SchedulingPolicy` onto its native constant.
///
/// @param policy Policy to translate.
/// @return `SCHED_OTHER`, `SCHED_FIFO`, or `SCHED_RR`.
[[nodiscard]] inline int native_policy(SchedulingPolicy policy) noexcept {
    switch (policy) {
        case SchedulingPolicy::Other: return SCHED_OTHER;
        case SchedulingPolicy::FIFO:  return SCHED_FIFO;
        case SchedulingPolicy::RR:    return SCHED_RR;
        default:                      return SCHED_OTHER;
    }
}

/// @brief Compute the requested-attribute mask for a spec.
///
/// @param spec Spec whose requests are translated into report bits.
/// @return Union of `name_mask`, `affinity_mask`, and `scheduler_mask`.
[[nodiscard]] inline std::uint32_t compute_requested_mask(const ThreadSpec& spec) noexcept {
    std::uint32_t requested = 0U;
    if (!spec.name().empty()) {
        requested |= ThreadApplyReport::name_mask;
    }
    if (!spec.cpus().empty()) {
        requested |= ThreadApplyReport::affinity_mask;
    }
    if (spec.scheduler_configured()) {
        requested |= ThreadApplyReport::scheduler_mask;
    }
    return requested;
}

/// @brief Record one failed attribute class and the first error.
///
/// @param report Report receiving the failed bit.
/// @param mask Attribute mask that failed.
inline void record_apply_failure(ThreadApplyReport& report, std::uint32_t mask) noexcept {
    report.failed_ |= mask;
    if (report.first_error_ == ErrorCode::Ok) {
        report.first_error_ = ErrorCode::SystemError;
    }
}

/// @brief Apply the requested thread name and record the outcome.
///
/// @param spec Spec carrying the name to apply.
/// @param report Report updated with the applied or failed name bit.
inline void apply_name(const ThreadSpec& spec, ThreadApplyReport& report) noexcept {
    if (set_name(spec.name_c_str()) == 0) {
        report.applied_ |= ThreadApplyReport::name_mask;
    } else {
        record_apply_failure(report, ThreadApplyReport::name_mask);
    }
}

/// @brief Apply the requested CPU affinity and record the outcome.
///
/// @param spec Spec carrying the CPU set to apply.
/// @param report Report updated with the applied or failed affinity bit.
inline void apply_affinity(const ThreadSpec& spec, ThreadApplyReport& report) noexcept {
    cpu_set_t native{};
    fill_native_cpu_set(spec.cpus(), native);
    if (set_affinity(native) == 0) {
        report.applied_ |= ThreadApplyReport::affinity_mask;
    } else {
        record_apply_failure(report, ThreadApplyReport::affinity_mask);
    }
}

/// @brief Apply the requested scheduling policy and record the outcome.
///
/// @param spec Spec carrying the policy/priority to apply.
/// @param report Report updated with the applied or failed scheduler bit.
inline void apply_scheduler(const ThreadSpec& spec, ThreadApplyReport& report) noexcept {
    sched_param scheduler{};
    scheduler.sched_priority = spec.priority();
    if (set_scheduler(native_policy(spec.policy()), scheduler) == 0) {
        report.applied_ |= ThreadApplyReport::scheduler_mask;
    } else {
        record_apply_failure(report, ThreadApplyReport::scheduler_mask);
    }
}

/// @brief Restore previously applied attributes in reverse mutation order.
///
/// Scheduler is restored first, then affinity, then the name; every failed
/// restoration is recorded in `rollback_failed`.
///
/// @param snapshot Captured pre-application state.
/// @param applied Mask of attributes that had been applied.
/// @param report Report updated with any rollback failures.
/// @note Spec `core/thread-management`, requirement "Required-mode rollback
///       remains centralized".
inline void rollback_applied_attributes(const ThreadStateSnapshot& snapshot, std::uint32_t applied,
                                        ThreadApplyReport& report) noexcept {
    if ((applied & ThreadApplyReport::scheduler_mask) != 0U) {
        if (!restore_scheduler(snapshot)) {
            report.rollback_failed_ |= ThreadApplyReport::scheduler_mask;
        }
    }
    if ((applied & ThreadApplyReport::affinity_mask) != 0U) {
        if (!restore_affinity(snapshot)) {
            report.rollback_failed_ |= ThreadApplyReport::affinity_mask;
        }
    }
    if ((applied & ThreadApplyReport::name_mask) != 0U) {
        if (!restore_name(snapshot)) {
            report.rollback_failed_ |= ThreadApplyReport::name_mask;
        }
    }
}

} // namespace thread_management_detail
#endif

/// @brief Apply a specification to the CALLING thread only.
///
/// Validation completes before any change. `Required` failures restore earlier
/// changes and return an error status; `BestEffort` attempts every requested
/// attribute and exposes all failures in `report`.
///
/// @param spec Attributes to apply; the caller must be the target thread.
/// @param report Overwritten with requested/applied/failed/rollback masks and
///        the first error.
/// @return `Status{}` when validation passed and (in `Required` mode) every
///         requested attribute applied; `InvalidArgument` from validation; the
///         attribute failure `ErrorCode` (typically `SystemError`) when a
///         `Required` attribute was rejected or could not be snapshotted.
/// @note Spec `core/thread-management`, requirement "Threads apply their own
///       specs before starting work" and "`apply_thread_spec_self` delegates to
///       single-attribute applicators".
/// @warning Never call this on another thread's behalf: affinity and scheduler
///          apply to the caller.
[[nodiscard]] inline Status apply_thread_spec_self(const ThreadSpec& spec,
                                                   ThreadApplyReport& report) noexcept {
    report = ThreadApplyReport{};
    const Status valid = spec.validate();
    if (!valid) {
        report.first_error_ = valid.error();
        return valid;
    }

    report.requested_ = thread_management_detail::compute_requested_mask(spec);
    if (report.requested_ == 0U) {
        return {};
    }

#if !defined(__linux__)
    report.failed_ = report.requested_;
    report.first_error_ = ErrorCode::SystemError;
    return spec.apply_mode() == ThreadApplyMode::Required ? std::unexpected(ErrorCode::SystemError)
                                                          : Status{};
#else
    thread_management_detail::ThreadStateSnapshot snapshot{};
    if (!thread_management_detail::snapshot_current(snapshot)) {
        report.first_error_ = ErrorCode::SystemError;
        return std::unexpected(ErrorCode::SystemError);
    }

    const bool required = spec.apply_mode() == ThreadApplyMode::Required;
    if ((report.requested_ & ThreadApplyReport::name_mask) != 0U) {
        thread_management_detail::apply_name(spec, report);
    }
    if ((!required || report.failed_ == 0U) &&
        (report.requested_ & ThreadApplyReport::affinity_mask) != 0U) {
        thread_management_detail::apply_affinity(spec, report);
    }
    if ((!required || report.failed_ == 0U) &&
        (report.requested_ & ThreadApplyReport::scheduler_mask) != 0U) {
        thread_management_detail::apply_scheduler(spec, report);
    }

    if (required && report.failed_ != 0U) {
        thread_management_detail::rollback_applied_attributes(snapshot, report.applied_, report);
        return std::unexpected(ErrorCode::SystemError);
    }
    return {};
#endif
}

} // namespace nexigen::core
