// nexigen::phm — claim verification through /proc (change checkpoint-identity).
#include <nexigen/phm/checkpoint_identity.hpp>

#include <fstream>
#include <iterator>

namespace nexigen::phm {

namespace {

constexpr std::size_t k_name_length = sizeof(CheckpointClaim::name);

/// @brief Read `/proc/<pid>/status` text.
[[nodiscard]] std::optional<std::string> read_status_text(std::uint32_t pid) noexcept {
    std::ifstream input("/proc/" + std::to_string(pid) + "/status");
    if (!input.is_open()) {
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

} // namespace

std::optional<std::uint32_t> parse_status_real_uid(std::string_view status_text) noexcept {
    std::size_t position = 0;
    while (position < status_text.size()) {
        const std::size_t line_end = status_text.find('\n', position);
        const std::size_t length = line_end == std::string_view::npos
                                       ? status_text.size() - position
                                       : line_end - position;
        const std::string_view line = status_text.substr(position, length);
        position = line_end == std::string_view::npos ? status_text.size() : line_end + 1U;
        if (!line.starts_with("Uid:")) {
            continue;
        }
        const std::size_t start = line.find_first_of("0123456789");
        if (start == std::string_view::npos) {
            return std::nullopt;
        }
        const std::size_t end = line.find_first_not_of("0123456789", start);
        const std::string_view digits = line.substr(start, end - start);
        // Parse by hand: the field is small, and this keeps the function free
        // of errno, locale, and allocation.
        std::uint64_t value = 0;
        for (const char digit : digits) {
            value = value * 10U + static_cast<std::uint64_t>(digit - '0');
            if (value > 0xFFFFFFFFULL) {
                return std::nullopt;
            }
        }
        return static_cast<std::uint32_t>(value);
    }
    return std::nullopt;
}

ClaimVerdict checkpoint_claim_status_verdict(std::uint32_t expected_uid,
                                             std::string_view status_text) noexcept {
    ClaimVerdict verdict{};
    const std::optional<std::uint32_t> real_uid = parse_status_real_uid(status_text);
    if (!real_uid.has_value()) {
        verdict.reason = "process status is unreadable";
        return verdict;
    }
    if (*real_uid != expected_uid) {
        verdict.reason = "process uid does not match expected_uid";
        return verdict;
    }
    verdict.verified = true;
    return verdict;
}

ClaimVerdict checkpoint_claim_verdict(const CheckpointClaim& claim,
                                      std::uint32_t expected_uid) noexcept {
    ClaimVerdict verdict{};
    std::size_t length = 0;
    while (length < k_name_length && claim.name[length] != '\0') {
        ++length;
    }
    if (length == 0U) {
        verdict.reason = "empty process name";
        return verdict;
    }
    if (claim.pid == 0U) {
        verdict.reason = "missing pid";
        return verdict;
    }
    if (claim.uid != expected_uid) {
        verdict.reason = "claimed uid does not match expected_uid";
        return verdict;
    }
    const std::optional<std::string> status_text = read_status_text(claim.pid);
    if (!status_text.has_value()) {
        verdict.reason = "process status is unreadable";
        return verdict;
    }
    return checkpoint_claim_status_verdict(expected_uid, *status_text);
}

} // namespace nexigen::phm
