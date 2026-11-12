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

/// Fixed-capacity set of CPU ids 0..1023. Empty means "preserve inherited
/// affinity" when used by a ThreadSpec. Duplicate and out-of-range ids are
/// configuration errors, never silently deduplicated or truncated.
class alignas(64) CpuSet {
public:
    /// Number of 64-bit words in the fixed 1024-bit bitmask.
    static constexpr std::size_t word_count = 16;
    /// Highest CPU id representable by this set (1023).
    static constexpr std::uint32_t max_cpu_id = 1023;

    constexpr CpuSet() noexcept = default;

    /// Add one CPU. Negative ids, ids above 1023, and duplicates are rejected.
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

    /// Whether `cpu` is a member; ids above 1023 are always absent.
    [[nodiscard]] constexpr bool contains(std::uint32_t cpu) const noexcept {
        return cpu <= max_cpu_id && (words_[cpu / 64U] & (1ULL << (cpu % 64U))) != 0ULL;
    }

    /// Whether no CPU is selected (inherited placement for a `ThreadSpec`).
    [[nodiscard]] constexpr bool empty() const noexcept {
        for (const std::uint64_t word : words_) {
            if (word != 0ULL) {
                return false;
            }
        }
        return true;
    }

    /// Number of selected CPUs (population count over all words).
    [[nodiscard]] constexpr std::size_t count() const noexcept {
        std::size_t total = 0;
        for (const std::uint64_t word : words_) {
            total += static_cast<std::size_t>(std::popcount(word));
        }
        return total;
    }

    /// Exact conversion of the legacy 64-bit representation. A zero mask is a
    /// valid empty set here (the legacy pin_self(0) error is a separate API).
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

    /// Allocation-free view conversion. Any duplicate or out-of-range id makes
    /// the whole conversion fail instead of returning a partially built set.
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

    /// Cold-path compact form: ascending ids and inclusive ranges, with [] for
    /// an empty set (for example "0,2-4,70").
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

    /// Bit-exact equality over the full 1024-bit value.
    friend constexpr bool operator==(const CpuSet&, const CpuSet&) noexcept = default;

private:
    std::array<std::uint64_t, word_count> words_{};
};

static_assert(sizeof(CpuSet) == 128, "CpuSet must remain a fixed 1024-bit value");
static_assert(alignof(CpuSet) == 64, "CpuSet layout is cache-line aligned");

/// Free diagnostic mirror (mandate 9).
[[nodiscard]] inline std::string to_string(const CpuSet& cpus) {
    return cpus.to_string();
}

/// Linux scheduling policies supported by the background-thread contract.
enum class SchedulingPolicy : std::uint8_t { Other, FIFO, RR };

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
template <> struct EnumTraits<SchedulingPolicy> {
    struct Entry {
        SchedulingPolicy value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 3> entries{{
        {SchedulingPolicy::Other, "other"},
        {SchedulingPolicy::FIFO, "fifo"},
        {SchedulingPolicy::RR, "rr"},
    }};
};

[[nodiscard]] constexpr std::string_view to_string(SchedulingPolicy policy) noexcept {
    return enum_name(policy);
}

[[nodiscard]] constexpr bool from_string(std::string_view name, SchedulingPolicy& out) noexcept {
    return enum_value(name, out);
}

/// Whether an OS rejection aborts startup or is reported as a partial state.
enum class ThreadApplyMode : std::uint8_t { BestEffort, Required };

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
template <> struct EnumTraits<ThreadApplyMode> {
    struct Entry {
        ThreadApplyMode value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 2> entries{{
        {ThreadApplyMode::BestEffort, "best_effort"},
        {ThreadApplyMode::Required, "required"},
    }};
};

[[nodiscard]] constexpr std::string_view to_string(ThreadApplyMode mode) noexcept {
    return enum_name(mode);
}

[[nodiscard]] constexpr bool from_string(std::string_view name, ThreadApplyMode& out) noexcept {
    return enum_value(name, out);
}

/// Fixed cold-path description of background-thread attributes. The default
/// requests no syscall: inherited name, affinity, and scheduler are preserved.
class alignas(64) ThreadSpec {
public:
    constexpr ThreadSpec() noexcept = default;

    /// Replace the requested CPU set; an empty set preserves inherited affinity.
    void set_cpus(const CpuSet& cpus) noexcept { cpus_ = cpus; }
    /// Requested CPU set; empty means inherited placement.
    [[nodiscard]] constexpr const CpuSet& cpus() const noexcept { return cpus_; }

    /// Empty clears the requested name. Linux permits at most 15 name bytes;
    /// embedded NUL and overlong input are rejected without truncation.
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

    /// Requested thread name; empty means inherited name.
    [[nodiscard]] std::string_view name() const noexcept { return std::string_view(name_.data()); }

    /// NUL-terminated view for the native naming syscall (storage is inline).
    [[nodiscard]] const char* name_c_str() const noexcept { return name_.data(); }

    /// Explicitly configure scheduling. Other requires priority 0; FIFO/RR use
    /// Linux's portable real-time range 1..99. A separate presence bit allows
    /// an explicit Other,0 reset while the default preserves inheritance.
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

    /// Drop an explicit scheduler request, restoring inherited scheduling.
    void clear_scheduler() noexcept {
        policy_ = SchedulingPolicy::Other;
        priority_ = 0;
        scheduler_configured_ = false;
    }

    /// Choose best-effort (report and continue) or required (abort) application.
    void set_apply_mode(ThreadApplyMode mode) noexcept { mode_ = mode; }

    /// Requested scheduling policy; meaningful only when configured.
    [[nodiscard]] constexpr SchedulingPolicy policy() const noexcept { return policy_; }
    /// Requested real-time priority in 0..99 (0 for Other).
    [[nodiscard]] constexpr std::int32_t priority() const noexcept { return priority_; }
    /// Best-effort or required application mode.
    [[nodiscard]] constexpr ThreadApplyMode apply_mode() const noexcept { return mode_; }
    /// Whether `set_scheduler` requested an explicit policy (including Other,0).
    [[nodiscard]] constexpr bool scheduler_configured() const noexcept {
        return scheduler_configured_;
    }

    /// True when the spec requests at least one attribute (name, CPUs, or an
    /// explicit scheduler). An empty spec preserves every inherited attribute.
    [[nodiscard]] constexpr bool has_requests() const noexcept {
        return name_[0] != '\0' || !cpus_.empty() || scheduler_configured_;
    }

    /// Full validation before any operating-system operation.
    [[nodiscard]] constexpr Status validate() const noexcept {
        if (mode_ != ThreadApplyMode::BestEffort && mode_ != ThreadApplyMode::Required) {
            return std::unexpected(ErrorCode::InvalidArgument);
        }
        if (scheduler_configured_) {
            return validate_scheduler(policy_, priority_);
        }
        return {};
    }

    /// Cold-path diagnostic rendering of every attribute, including whether
    /// the scheduler was explicitly configured.
    [[nodiscard]] std::string to_string() const {
        return "ThreadSpec(name=" + std::string(name()) + ", cpus=" + cpus_.to_string() +
               ", policy=" + std::string(core::to_string(policy_)) +
               ", priority=" + std::to_string(priority_) +
               ", mode=" + std::string(core::to_string(mode_)) + ", scheduler_configured=" +
               (scheduler_configured_ ? std::string("true") : std::string("false")) + ")";
    }

    /// Value equality over all requested attributes (not application state).
    friend constexpr bool operator==(const ThreadSpec&, const ThreadSpec&) noexcept = default;

private:
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

    CpuSet cpus_{};                                     // offset 0, 128 bytes
    std::array<char, 16> name_{};                       // offset 128, 15 bytes + NUL
    std::int32_t priority_{0};                          // offset 144
    SchedulingPolicy policy_{SchedulingPolicy::Other};  // offset 148
    ThreadApplyMode mode_{ThreadApplyMode::BestEffort}; // offset 149
    bool scheduler_configured_{false};                  // offset 150
};

static_assert(sizeof(ThreadSpec) == 192, "ThreadSpec must keep its fixed cold layout");
static_assert(alignof(ThreadSpec) == 64, "ThreadSpec layout is cache-line aligned");

/// Variant-A fallback: use the primary spec when it requests anything,
/// otherwise inherit the fallback (for example a daemon's bg_thread).
/// Neither argument is modified.
[[nodiscard]] constexpr ThreadSpec effective_thread_spec(const ThreadSpec& primary,
                                                         const ThreadSpec& fallback) noexcept {
    return primary.has_requests() ? primary : fallback;
}

/// Cold JSON parser with all-or-nothing semantics. The ADL from_json below and
/// ThreadSpec's parser share this helper so invalid input never leaves a
/// partially updated destination.
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

inline void to_json(nlohmann::json& j, const CpuSet& cpus) {
    j = nlohmann::json::array();
    for (std::uint32_t cpu = 0; cpu <= CpuSet::max_cpu_id; ++cpu) {
        if (cpus.contains(cpu)) {
            j.push_back(cpu);
        }
    }
}

inline void from_json(const nlohmann::json& j, CpuSet& out) noexcept {
    static_cast<void>(parse_cpu_set_json(j, out));
}

inline void to_json(nlohmann::json& j, const SchedulingPolicy policy) {
    mandate::enum_to_json(j, policy);
}

inline void from_json(const nlohmann::json& j, SchedulingPolicy& out) noexcept {
    mandate::enum_from_json(j, out);
}

inline void to_json(nlohmann::json& j, const ThreadApplyMode mode) {
    mandate::enum_to_json(j, mode);
}

inline void from_json(const nlohmann::json& j, ThreadApplyMode& out) noexcept {
    mandate::enum_from_json(j, out);
}

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

inline void from_json(const nlohmann::json& j, ThreadSpec& out) noexcept {
    static_cast<void>(parse_thread_spec_json(j, out));
}

/// Free diagnostic mirror (mandate 9).
[[nodiscard]] inline std::string to_string(const ThreadSpec& spec) {
    return spec.to_string();
}

namespace thread_management_detail {

#if defined(__linux__)
/// The single value-to-native conversion used by the legacy affinity adapter
/// and the full thread application path. Fixed CPU_SETSIZE storage: no heap.
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
struct ThreadStateSnapshot;

void record_apply_failure(ThreadApplyReport&, std::uint32_t) noexcept;
void apply_name(const ThreadSpec&, ThreadApplyReport&) noexcept;
void apply_affinity(const ThreadSpec&, ThreadApplyReport&) noexcept;
void apply_scheduler(const ThreadSpec&, ThreadApplyReport&) noexcept;
void rollback_applied_attributes(const ThreadStateSnapshot&, std::uint32_t,
                                 ThreadApplyReport&) noexcept;
#endif

} // namespace thread_management_detail

/// Immutable startup result. Bit masks describe Name/Affinity/Scheduler
/// operations; applied means the operation succeeded during this application,
/// even when a later Required failure rolled it back.
class alignas(64) ThreadApplyReport {
public:
    /// Bit set in `requested`/`applied`/`failed` for the thread name.
    static constexpr std::uint32_t name_mask = 1U;
    /// Bit set in `requested`/`applied`/`failed` for CPU affinity.
    static constexpr std::uint32_t affinity_mask = 2U;
    /// Bit set in `requested`/`applied`/`failed` for scheduling policy/priority.
    static constexpr std::uint32_t scheduler_mask = 4U;

    /// Attributes the spec asked to change.
    [[nodiscard]] constexpr std::uint32_t requested() const noexcept { return requested_; }
    /// Attributes successfully applied during this attempt (even if later rolled back).
    [[nodiscard]] constexpr std::uint32_t applied() const noexcept { return applied_; }
    /// Attributes the kernel rejected during this attempt.
    [[nodiscard]] constexpr std::uint32_t failed() const noexcept { return failed_; }
    /// Applied attributes that could not be restored after a Required failure.
    [[nodiscard]] constexpr std::uint32_t rollback_failed() const noexcept {
        return rollback_failed_;
    }
    /// First error observed; `Ok` when every requested attribute applied.
    [[nodiscard]] constexpr ErrorCode first_error() const noexcept { return first_error_; }
    /// Whether no attribute failed and no rollback failed.
    [[nodiscard]] constexpr bool complete() const noexcept {
        return failed_ == 0U && rollback_failed_ == 0U;
    }

    /// Cold-path mask/error rendering; see `nexigen::exec::describe_thread_apply_report`
    /// for the operator-facing attribute names.
    [[nodiscard]] std::string to_string() const {
        return "ThreadApplyReport(requested=" + std::to_string(requested_) +
               ", applied=" + std::to_string(applied_) + ", failed=" + std::to_string(failed_) +
               ", rollback_failed=" + std::to_string(rollback_failed_) +
               ", first_error=" + std::string(core::to_string(first_error_)) + ")";
    }

    /// Value equality over every mask and the first error.
    friend constexpr bool operator==(const ThreadApplyReport&,
                                     const ThreadApplyReport&) noexcept = default;
    /// All-or-nothing JSON restore; invalid input leaves the destination untouched.
    friend void from_json(const nlohmann::json&, ThreadApplyReport&) noexcept;
    friend Status apply_thread_spec_self(const ThreadSpec&, ThreadApplyReport&) noexcept;
#if defined(__linux__)
    friend void thread_management_detail::record_apply_failure(ThreadApplyReport&,
                                                               std::uint32_t) noexcept;
    friend void thread_management_detail::apply_name(const ThreadSpec&,
                                                     ThreadApplyReport&) noexcept;
    friend void thread_management_detail::apply_affinity(const ThreadSpec&,
                                                         ThreadApplyReport&) noexcept;
    friend void thread_management_detail::apply_scheduler(const ThreadSpec&,
                                                          ThreadApplyReport&) noexcept;
    friend void thread_management_detail::rollback_applied_attributes(
        const thread_management_detail::ThreadStateSnapshot&, std::uint32_t,
        ThreadApplyReport&) noexcept;
#endif

private:
    std::uint32_t requested_{0};
    std::uint32_t applied_{0};
    std::uint32_t failed_{0};
    std::uint32_t rollback_failed_{0};
    ErrorCode first_error_{ErrorCode::Ok};
};

static_assert(sizeof(ThreadApplyReport) == 64,
              "ThreadApplyReport must remain one cold-start cache line");
static_assert(alignof(ThreadApplyReport) == 64);

[[nodiscard]] inline std::string to_string(const ThreadApplyReport& report) {
    return report.to_string();
}

inline void to_json(nlohmann::json& j, const ThreadApplyReport& report) {
    j = nlohmann::json{
        {"requested", report.requested()},     {"applied", report.applied()},
        {"failed", report.failed()},           {"rollback_failed", report.rollback_failed()},
        {"first_error", report.first_error()},
    };
}

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

struct ThreadStateSnapshot {
    cpu_set_t affinity{};
    char name[16]{};
    int policy = SCHED_OTHER;
    sched_param scheduler{};
};

[[nodiscard]] inline int set_name(const char* name) noexcept {
    if (fault_injection::fault(fault_injection::FaultPoint::Thread_set_name)) {
        return EPERM;
    }
    return ::pthread_setname_np(::pthread_self(), name);
}

[[nodiscard]] inline int set_affinity(const cpu_set_t& cpus) noexcept {
    if (fault_injection::fault(fault_injection::FaultPoint::Thread_set_affinity)) {
        return EINVAL;
    }
    return ::pthread_setaffinity_np(::pthread_self(), sizeof(cpus), &cpus);
}

[[nodiscard]] inline int set_scheduler(int policy, const sched_param& scheduler) noexcept {
    if (fault_injection::fault(fault_injection::FaultPoint::Thread_set_scheduler)) {
        return EPERM;
    }
    return ::sched_setscheduler(0, policy, &scheduler);
}

[[nodiscard]] inline bool restore_name(const char* name) noexcept {
    if (fault_injection::fault(fault_injection::FaultPoint::Thread_restore_name)) {
        return false;
    }
    return ::pthread_setname_np(::pthread_self(), name) == 0;
}

[[nodiscard]] inline bool restore_affinity(const cpu_set_t& cpus) noexcept {
    if (fault_injection::fault(fault_injection::FaultPoint::Thread_restore_affinity)) {
        return false;
    }
    return ::pthread_setaffinity_np(::pthread_self(), sizeof(cpus), &cpus) == 0;
}

[[nodiscard]] inline bool restore_scheduler(int policy, const sched_param& scheduler) noexcept {
    if (fault_injection::fault(fault_injection::FaultPoint::Thread_restore_scheduler)) {
        return false;
    }
    return ::sched_setscheduler(0, policy, &scheduler) == 0;
}

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

[[nodiscard]] inline bool restore_scheduler(const ThreadStateSnapshot& snapshot) noexcept {
    return restore_scheduler(snapshot.policy, snapshot.scheduler);
}

[[nodiscard]] inline bool restore_affinity(const ThreadStateSnapshot& snapshot) noexcept {
    return restore_affinity(snapshot.affinity);
}

[[nodiscard]] inline bool restore_name(const ThreadStateSnapshot& snapshot) noexcept {
    return restore_name(snapshot.name);
}

[[nodiscard]] inline int native_policy(SchedulingPolicy policy) noexcept {
    switch (policy) {
        case SchedulingPolicy::Other: return SCHED_OTHER;
        case SchedulingPolicy::FIFO:  return SCHED_FIFO;
        case SchedulingPolicy::RR:    return SCHED_RR;
        default:                      return SCHED_OTHER;
    }
}

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

inline void record_apply_failure(ThreadApplyReport& report, std::uint32_t mask) noexcept {
    report.failed_ |= mask;
    if (report.first_error_ == ErrorCode::Ok) {
        report.first_error_ = ErrorCode::SystemError;
    }
}

inline void apply_name(const ThreadSpec& spec, ThreadApplyReport& report) noexcept {
    if (set_name(spec.name_c_str()) == 0) {
        report.applied_ |= ThreadApplyReport::name_mask;
    } else {
        record_apply_failure(report, ThreadApplyReport::name_mask);
    }
}

inline void apply_affinity(const ThreadSpec& spec, ThreadApplyReport& report) noexcept {
    cpu_set_t native{};
    fill_native_cpu_set(spec.cpus(), native);
    if (set_affinity(native) == 0) {
        report.applied_ |= ThreadApplyReport::affinity_mask;
    } else {
        record_apply_failure(report, ThreadApplyReport::affinity_mask);
    }
}

inline void apply_scheduler(const ThreadSpec& spec, ThreadApplyReport& report) noexcept {
    sched_param scheduler{};
    scheduler.sched_priority = spec.priority();
    if (set_scheduler(native_policy(spec.policy()), scheduler) == 0) {
        report.applied_ |= ThreadApplyReport::scheduler_mask;
    } else {
        record_apply_failure(report, ThreadApplyReport::scheduler_mask);
    }
}

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

/// Apply a specification to the CALLING thread only. Validation is completed
/// before any change. Required failures restore earlier changes; BestEffort
/// attempts every requested attribute and exposes all failures in the report.
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
