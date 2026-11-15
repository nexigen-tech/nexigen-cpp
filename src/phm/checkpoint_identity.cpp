// nexigen::phm — claim verification through /proc (change checkpoint-identity).
#include <nexigen/phm/checkpoint_identity.hpp>

#include <cstdlib>
#include <fstream>
#include <iterator>

namespace nexigen::phm {

namespace {

constexpr std::size_t k_name_length = sizeof(CheckpointClaim::name);

/// @brief Read the real uid from `/proc/<pid>/status`.
[[nodiscard]] bool read_real_uid(std::uint32_t pid, std::uint32_t& uid) noexcept {
    const std::string path = "/proc/" + std::to_string(pid) + "/status";
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }
    std::string line;
    while (std::getline(input, line)) {
        if (!line.starts_with("Uid:")) {
            continue;
        }
        const auto start = line.find_first_of("0123456789");
        if (start == std::string::npos) {
            return false;
        }
        const auto end = line.find_first_not_of("0123456789", start);
        const std::string digits = line.substr(start, end - start);
        const unsigned long long value = std::strtoull(digits.c_str(), nullptr, 10);
        if (value > 0xFFFFFFFFULL) {
            return false;
        }
        uid = static_cast<std::uint32_t>(value);
        return true;
    }
    return false;
}

} // namespace

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
    std::uint32_t real_uid = 0;
    if (!read_real_uid(claim.pid, real_uid)) {
        verdict.reason = "process status is unreadable";
        return verdict;
    }
    if (real_uid != expected_uid) {
        verdict.reason = "process uid does not match expected_uid";
        return verdict;
    }
    verdict.verified = true;
    return verdict;
}

} // namespace nexigen::phm
