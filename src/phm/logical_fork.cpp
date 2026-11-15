// nexigen::phm — fork/join region serialization (change close-phm-safety-loop).
#include <nexigen/phm/logical_fork.hpp>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::phm {

std::string LogicalFork::to_string() const {
    std::string entries;
    for (std::size_t i = 0; i < branch_entries.size(); ++i) {
        if (i != 0U) {
            entries += ",";
        }
        entries += std::to_string(branch_entries[i]);
    }
    return std::format("LogicalFork(fork_id={}, branches=[{}], join_id={})", fork_id, entries,
                       join_id);
}

void to_json(nlohmann::json& j, const LogicalFork& fork) {
    auto branches = nlohmann::json::array();
    for (const std::uint32_t id : fork.branch_entries) {
        branches.push_back(id);
    }
    j = nlohmann::json{
        {"fork_id", fork.fork_id}, {"branch_entries", std::move(branches)}, {"join_id", fork.join_id}};
}

void from_json(const nlohmann::json& j, LogicalFork& fork) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_unsigned(j, "fork_id", fork.fork_id);
    core::detail::read_unsigned(j, "join_id", fork.join_id);
    static_cast<void>(core::detail::collect_array_into<std::uint32_t>(
        j, "branch_entries", fork.branch_entries,
        [](const nlohmann::json& item, std::uint32_t& id) noexcept {
            return core::detail::to_unsigned(item, id);
        }));
}

} // namespace nexigen::phm
