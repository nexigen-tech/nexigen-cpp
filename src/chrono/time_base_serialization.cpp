// nexigen::chrono — cold-path serialization of the time-base value types.
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/domain_coefficient.hpp>
#include <nexigen/chrono/time_base_block.hpp>
#include <nexigen/chrono/time_base_history_entry.hpp>
#include <nexigen/chrono/time_snapshot.hpp>

namespace nexigen::chrono {
namespace {

template <class UInt>
void read_uint(const nlohmann::json& document, const char* key, UInt& out) noexcept {
    static_assert(std::is_unsigned_v<UInt>);
    const auto it = document.find(key);
    if (it == document.end() || !it->is_number_unsigned()) {
        return;
    }
    const auto value = it->get<std::uint64_t>();
    if (value <= static_cast<std::uint64_t>(std::numeric_limits<UInt>::max())) {
        out = static_cast<UInt>(value);
    }
}

template <class SInt>
void read_int(const nlohmann::json& document, const char* key, SInt& out) noexcept {
    static_assert(std::is_signed_v<SInt>);
    const auto it = document.find(key);
    if (it == document.end() || !it->is_number_integer()) {
        return;
    }
    const auto value = it->get<std::int64_t>();
    if (value >= static_cast<std::int64_t>(std::numeric_limits<SInt>::min()) &&
        value <= static_cast<std::int64_t>(std::numeric_limits<SInt>::max())) {
        out = static_cast<SInt>(value);
    }
}

template <class Enum>
void read_enum_name(const nlohmann::json& document, const char* key, Enum& out) noexcept {
    const auto it = document.find(key);
    if (it == document.end() || !it->is_string()) {
        return;
    }
    const auto* const name = it->get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

template <std::size_t N>
void read_name(const nlohmann::json& document, const char* key, char (&out)[N]) noexcept {
    const auto it = document.find(key);
    if (it == document.end() || !it->is_string()) {
        return;
    }
    const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
    if (text == nullptr) {
        return;
    }
    const std::size_t take = text->size() < N - 1U ? text->size() : N - 1U;
    for (std::size_t i = 0; i < take; ++i) {
        out[i] = (*text)[i];
    }
    out[take] = '\0';
}

} // namespace

std::string_view DomainCoefficient::name_view() const noexcept {
    std::size_t length = 0;
    while (length < sizeof(name) && name[length] != '\0') {
        ++length;
    }
    return std::string_view{name, length};
}

std::string DomainCoefficient::to_string() const {
    return std::format(
        "DomainCoefficient(id={}, class={}, name={}, lock={}, epoch={}, native_anchor={}, "
        "canonical_anchor_ns={}, scale_fwd_q32={}, scale_rev_q32={}, unc_ns={}, "
        "valid_native=[{}, {}], valid_canonical=[{}, {}])",
        domain_id, nexigen::chrono::to_string(domain_class), name_view(),
        nexigen::chrono::to_string(lock), epoch, native_anchor, canonical_anchor_ns, scale_fwd_q32,
        scale_rev_q32, unc_ns, valid_from_native, valid_to_native, valid_from_canonical,
        valid_to_canonical);
}

std::string to_string(const DomainCoefficient& coefficient) {
    return coefficient.to_string();
}

void to_json(nlohmann::json& j, const DomainCoefficient& coefficient) {
    j = nlohmann::json{
        {"domain_id", coefficient.domain_id},
        {"domain_class", coefficient.domain_class},
        {"name", std::string(coefficient.name_view())},
        {"lock", coefficient.lock},
        {"flags", coefficient.flags},
        {"epoch", coefficient.epoch},
        {"native_anchor", coefficient.native_anchor},
        {"canonical_anchor_ns", coefficient.canonical_anchor_ns},
        {"scale_fwd_q32", coefficient.scale_fwd_q32},
        {"scale_rev_q32", coefficient.scale_rev_q32},
        {"unc_ns", coefficient.unc_ns},
        {"valid_from_native", coefficient.valid_from_native},
        {"valid_to_native", coefficient.valid_to_native},
        {"valid_from_canonical", coefficient.valid_from_canonical},
        {"valid_to_canonical", coefficient.valid_to_canonical},
    };
}

void from_json(const nlohmann::json& j, DomainCoefficient& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    read_uint(j, "domain_id", out.domain_id);
    read_enum_name(j, "domain_class", out.domain_class);
    read_name(j, "name", out.name);
    read_enum_name(j, "lock", out.lock);
    read_uint(j, "flags", out.flags);
    read_uint(j, "epoch", out.epoch);
    read_uint(j, "native_anchor", out.native_anchor);
    read_int(j, "canonical_anchor_ns", out.canonical_anchor_ns);
    read_uint(j, "scale_fwd_q32", out.scale_fwd_q32);
    read_uint(j, "scale_rev_q32", out.scale_rev_q32);
    read_uint(j, "unc_ns", out.unc_ns);
    read_uint(j, "valid_from_native", out.valid_from_native);
    read_uint(j, "valid_to_native", out.valid_to_native);
    read_uint(j, "valid_from_canonical", out.valid_from_canonical);
    read_uint(j, "valid_to_canonical", out.valid_to_canonical);
}

std::string TimeBaseBlock::to_string() const {
    return std::format(
        "TimeBaseBlock(status={}, mode={}, generation={}, domains={}, boot=0x{:016x}, "
        "tai_offset={}, utc_offset={}, heartbeat={})",
        nexigen::chrono::to_string(status), nexigen::chrono::to_string(sync_mode), generation,
        domain_count, boot_token, tai_offset_ns, utc_offset_ns, heartbeat_ns);
}

std::string to_string(const TimeBaseBlock& block) {
    return block.to_string();
}

void to_json(nlohmann::json& j, const TimeBaseBlock& block) {
    nlohmann::json rows = nlohmann::json::array();
    const std::uint32_t count = block.domain_count < block.capacity
                                    ? block.domain_count
                                    : static_cast<std::uint32_t>(time_base_max_domains);
    for (std::uint32_t i = 0; i < count; ++i) {
        rows.push_back(block.rows[i]);
    }
    j = nlohmann::json{
        {"magic", block.magic},
        {"version", block.version},
        {"capacity", block.capacity},
        {"status", block.status},
        {"sync_mode", block.sync_mode},
        {"service_flags", block.service_flags},
        {"domain_count", count},
        {"generation", block.generation},
        {"boot_token", block.boot_token},
        {"tai_offset_ns", block.tai_offset_ns},
        {"utc_offset_ns", block.utc_offset_ns},
        {"utc_unc_ns", block.utc_unc_ns},
        {"heartbeat_ns", block.heartbeat_ns},
        {"rows", std::move(rows)},
        {"last_snapshot", block.last_snapshot},
    };
}

void from_json(const nlohmann::json& j, TimeBaseBlock& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    read_uint(j, "magic", out.magic);
    read_uint(j, "version", out.version);
    read_uint(j, "capacity", out.capacity);
    read_enum_name(j, "status", out.status);
    read_enum_name(j, "sync_mode", out.sync_mode);
    const auto flags = j.find("service_flags");
    if (flags != j.end()) {
        from_json(*flags, out.service_flags);
    }
    read_uint(j, "generation", out.generation);
    read_uint(j, "boot_token", out.boot_token);
    read_int(j, "tai_offset_ns", out.tai_offset_ns);
    read_int(j, "utc_offset_ns", out.utc_offset_ns);
    read_uint(j, "utc_unc_ns", out.utc_unc_ns);
    read_uint(j, "heartbeat_ns", out.heartbeat_ns);
    const auto snapshot = j.find("last_snapshot");
    if (snapshot != j.end()) {
        from_json(*snapshot, out.last_snapshot);
    }

    const auto rows_it = j.find("rows");
    if (rows_it == j.end() || !rows_it->is_array()) {
        read_uint(j, "domain_count", out.domain_count);
        return;
    }
    std::uint32_t parsed = 0;
    for (const auto& row : *rows_it) {
        if (parsed >= time_base_max_domains) {
            break;
        }
        from_json(row, out.rows[parsed]);
        ++parsed;
    }
    out.domain_count = static_cast<std::uint16_t>(parsed);
}

std::string TimeSnapshot::to_string() const {
    return std::format(
        "TimeSnapshot(generation={}, boot=0x{:016x}, status={}, mode={}, domains={}, "
        "mono_raw={}, mono={}, tai={}, realtime={}, flags={})",
        generation, boot_token, nexigen::chrono::to_string(status),
        nexigen::chrono::to_string(sync_mode), domain_count, monotonic_raw_ns, monotonic_ns, tai_ns,
        realtime_ns, nexigen::chrono::to_string(service_flags));
}

std::string to_string(const TimeSnapshot& snapshot) {
    return snapshot.to_string();
}

void to_json(nlohmann::json& j, const TimeSnapshot& snapshot) {
    nlohmann::json domains = nlohmann::json::array();
    const std::uint32_t count = snapshot.domain_count < TimeSnapshot::max_domains
                                    ? snapshot.domain_count
                                    : static_cast<std::uint32_t>(TimeSnapshot::max_domains);
    for (std::uint32_t i = 0; i < count; ++i) {
        const SnapshotDomainValue& value = snapshot.domains[i];
        domains.push_back(nlohmann::json{{"domain_id", value.domain_id},
                                         {"domain_class", value.domain_class},
                                         {"lock", value.lock},
                                         {"flags", value.flags},
                                         {"epoch", value.epoch},
                                         {"value", value.value}});
    }
    j = nlohmann::json{
        {"generation", snapshot.generation},
        {"boot_token", snapshot.boot_token},
        {"status", snapshot.status},
        {"sync_mode", snapshot.sync_mode},
        {"service_flags", snapshot.service_flags},
        {"monotonic_raw_ns", snapshot.monotonic_raw_ns},
        {"monotonic_ns", snapshot.monotonic_ns},
        {"tai_ns", snapshot.tai_ns},
        {"realtime_ns", snapshot.realtime_ns},
        {"tai_offset_ns", snapshot.tai_offset_ns},
        {"utc_offset_ns", snapshot.utc_offset_ns},
        {"unc_ns", snapshot.unc_ns},
        {"domains", std::move(domains)},
    };
}

void from_json(const nlohmann::json& j, TimeSnapshot& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    read_uint(j, "generation", out.generation);
    read_uint(j, "boot_token", out.boot_token);
    read_enum_name(j, "status", out.status);
    read_enum_name(j, "sync_mode", out.sync_mode);
    const auto flags = j.find("service_flags");
    if (flags != j.end()) {
        from_json(*flags, out.service_flags);
    }
    read_uint(j, "monotonic_raw_ns", out.monotonic_raw_ns);
    read_uint(j, "monotonic_ns", out.monotonic_ns);
    read_uint(j, "tai_ns", out.tai_ns);
    read_uint(j, "realtime_ns", out.realtime_ns);
    read_int(j, "tai_offset_ns", out.tai_offset_ns);
    read_int(j, "utc_offset_ns", out.utc_offset_ns);
    read_uint(j, "unc_ns", out.unc_ns);

    const auto domains_it = j.find("domains");
    if (domains_it == j.end() || !domains_it->is_array()) {
        return;
    }
    std::uint32_t parsed = 0;
    for (const auto& item : *domains_it) {
        if (parsed >= TimeSnapshot::max_domains || !item.is_object()) {
            break;
        }
        SnapshotDomainValue& value = out.domains[parsed];
        read_uint(item, "domain_id", value.domain_id);
        read_enum_name(item, "domain_class", value.domain_class);
        read_enum_name(item, "lock", value.lock);
        read_uint(item, "flags", value.flags);
        read_uint(item, "epoch", value.epoch);
        read_uint(item, "value", value.value);
        ++parsed;
    }
    out.domain_count = parsed;
}

void TimeBaseHistoryEntry::set_boot_uuid(std::string_view uuid) noexcept {
    const std::size_t count =
        uuid.size() < boot_uuid.size() - 1U ? uuid.size() : boot_uuid.size() - 1U;
    for (std::size_t i = 0; i < count; ++i) {
        boot_uuid[i] = uuid[i];
    }
    boot_uuid[count] = '\0';
}

std::string_view TimeBaseHistoryEntry::boot_uuid_view() const noexcept {
    std::size_t length = 0;
    while (length < boot_uuid.size() && boot_uuid[length] != '\0') {
        ++length;
    }
    return std::string_view{boot_uuid.data(), length};
}

std::string TimeBaseHistoryEntry::to_string() const {
    return std::format("TimeBaseHistoryEntry(generation={}, status={}, mode={}, boot_uuid={}, {})",
                       generation, nexigen::chrono::to_string(status),
                       nexigen::chrono::to_string(sync_mode), boot_uuid_view(),
                       coefficient.to_string());
}

std::string to_string(const TimeBaseHistoryEntry& entry) {
    return entry.to_string();
}

void to_json(nlohmann::json& j, const TimeBaseHistoryEntry& entry) {
    j = nlohmann::json{
        {"generation", entry.generation},      {"status", entry.status},
        {"sync_mode", entry.sync_mode},        {"service_flags", entry.service_flags},
        {"boot_uuid", entry.boot_uuid_view()}, {"gap", entry.gap},
        {"coefficient", entry.coefficient},
    };
}

void from_json(const nlohmann::json& j, TimeBaseHistoryEntry& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    read_uint(j, "generation", out.generation);
    read_enum_name(j, "status", out.status);
    read_enum_name(j, "sync_mode", out.sync_mode);
    const auto flags = j.find("service_flags");
    if (flags != j.end()) {
        from_json(*flags, out.service_flags);
    }
    const auto gap_it = j.find("gap");
    if (gap_it != j.end() && gap_it->is_boolean()) {
        out.gap = gap_it->get<bool>();
    }
    const auto boot_it = j.find("boot_uuid");
    if (boot_it != j.end() && boot_it->is_string()) {
        const auto* const text = boot_it->get_ptr<const nlohmann::json::string_t*>();
        if (text != nullptr) {
            out.set_boot_uuid(std::string_view(*text));
        }
    }
    const auto coefficient_it = j.find("coefficient");
    if (coefficient_it != j.end()) {
        from_json(*coefficient_it, out.coefficient);
    }
}

} // namespace nexigen::chrono
