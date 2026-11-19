// nexigen::chrono — kernel clock-discipline status implementation.
#include <cerrno>
#include <format>

#include <nexigen/chrono/adjtimex_status.hpp>

#if defined(__linux__)
#include <sys/timex.h>
#endif

namespace nexigen::chrono {
namespace {

#if defined(__linux__)
/// @brief `STA_PLL` from `sys/timex.h`.
constexpr std::uint32_t kStaPll = 0x0001U;
/// @brief `STA_UNSYNC` from `sys/timex.h`.
constexpr std::uint32_t kStaUnsync = 0x0040U;
/// @brief The kernel reports frequency in scaled ppm (`ppm << 16`).
constexpr std::int64_t kScaledPpm = 1LL << 16U;
/// @brief Microseconds to nanoseconds.
constexpr std::int64_t kMicroseconds = 1'000LL;
#endif

} // namespace

core::Result<AdjtimexStatus> read_adjtimex() noexcept {
#if defined(__linux__)
    timex discipline{};
    const int result = ::adjtimex(&discipline);
    if (result < 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    AdjtimexStatus status{};
    status.kernel_status = static_cast<std::uint32_t>(discipline.status);
    status.offset_ns = static_cast<std::int64_t>(discipline.offset) * kMicroseconds;
    status.freq_ppb = static_cast<std::int64_t>(discipline.freq) * 1'000LL / kScaledPpm;
    status.max_error_ns = static_cast<std::int64_t>(discipline.maxerror) * kMicroseconds;
    status.estimated_error_ns = static_cast<std::int64_t>(discipline.esterror) * kMicroseconds;
    status.pll_engaged = (static_cast<std::uint32_t>(discipline.status) & kStaPll) != 0U;
    status.unsynchronized = (static_cast<std::uint32_t>(discipline.status) & kStaUnsync) != 0U;
    return status;
#else
    return std::unexpected(core::ErrorCode::UnsupportedFormat);
#endif
}

std::string AdjtimexStatus::to_string() const {
    return std::format("AdjtimexStatus(offset_ns={}, freq_ppb={}, max_error_ns={}, locked={}, "
                       "status=0x{:04X})",
                       offset_ns, freq_ppb, max_error_ns, locked(), kernel_status);
}

std::string to_string(const AdjtimexStatus& status) {
    return status.to_string();
}

void to_json(nlohmann::json& j, const AdjtimexStatus& status) {
    j = nlohmann::json{
        {"kernel_status", status.kernel_status},
        {"offset_ns", status.offset_ns},
        {"freq_ppb", status.freq_ppb},
        {"max_error_ns", status.max_error_ns},
        {"estimated_error_ns", status.estimated_error_ns},
        {"pll_engaged", status.pll_engaged},
        {"unsynchronized", status.unsynchronized},
    };
}

void from_json(const nlohmann::json& j, AdjtimexStatus& status) noexcept {
    if (!j.is_object()) {
        return;
    }
    AdjtimexStatus parsed = status;
    const auto read_int = [&j](const char* key, std::int64_t& out) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_number_integer()) {
            out = it->get<std::int64_t>();
        }
    };
    const auto read_bool = [&j](const char* key, bool& out) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_boolean()) {
            out = it->get<bool>();
        }
    };
    std::uint32_t kernel_status = parsed.kernel_status;
    const auto status_it = j.find("kernel_status");
    if (status_it != j.end() && status_it->is_number_unsigned()) {
        kernel_status = status_it->get<std::uint32_t>();
    }
    parsed.kernel_status = kernel_status;
    read_int("offset_ns", parsed.offset_ns);
    read_int("freq_ppb", parsed.freq_ppb);
    read_int("max_error_ns", parsed.max_error_ns);
    read_int("estimated_error_ns", parsed.estimated_error_ns);
    read_bool("pll_engaged", parsed.pll_engaged);
    read_bool("unsynchronized", parsed.unsynchronized);
    status = parsed;
}

} // namespace nexigen::chrono
