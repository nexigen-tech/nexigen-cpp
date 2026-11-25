// EDS parsing, cache, registry, routing and reindex tests.
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

#include <nexigen/hw/can/can_message.hpp>

#include "can_record_ring.hpp"
#include "canopen_registry.hpp"
#include "canopen_reindex.hpp"
#include "canopen_types.hpp"
#include "cob_id_router.hpp"
#include "eds_library.hpp"
#include "eds_parser.hpp"
#include "suite.hpp"
#include "support/check.hpp"

namespace {

namespace canview = nexigen::canview;
namespace hwcan = nexigen::hw::can;

constexpr std::string_view kEds = R"([FileInfo]
FileName=test.eds
[DeviceInfo]
VendorName=Test
[1017]
ParameterName=Producer heartbeat time
DataType=0x0006
AccessType=rw
DefaultValue=1000
[1400]
ParameterName=RPDO1 communication
ObjectType=0x9
SubNumber=2
[1400sub0]
ParameterName=Number of entries
DataType=0x0005
DefaultValue=2
[1400sub1]
ParameterName=COB-ID
DataType=0x0007
AccessType=rw
DefaultValue=$NODEID+0x200
[1800]
ParameterName=TPDO1 communication
ObjectType=0x9
SubNumber=2
[1800sub1]
ParameterName=COB-ID
DataType=0x0007
DefaultValue=$NODEID+0x300
[1A00]
ParameterName=TPDO1 mapping
ObjectType=0x9
SubNumber=3
[1A00sub0]
DataType=0x0005
DefaultValue=2
[1A00sub1]
DataType=0x0007
DefaultValue=0x20000020
[1A00sub2]
DataType=0x0007
DefaultValue=0x20010110
[2000]
ParameterName=Compact
ObjectType=0x8
DataType=0x0007
SubNumber=3
CompactSubObj=3
)";

canview::EdsParseResult parse_reference() {
    return canview::EdsParser::parse(kEds, "test.eds");
}

std::uint64_t push_frame(canview::CanRecordRing& ring, std::uint16_t channel, std::uint32_t can_id,
                         bool extended = false) {
    hwcan::CanMessage message{};
    message.channel_id = channel;
    message.can_id = can_id;
    message.extended = extended;
    message.dlc = 8U;
    message.hardware_timestamp_ns = ring.tail() * 1'000U;
    const std::uint8_t meta = canview::pack_frame_meta(canview::CanRecordKind::Frame);
    return ring.push(message, 0, canview::CanRecordView::no_node, meta, 0U, 0U);
}

void test_eds_parser() {
    const auto parsed = parse_reference();
    NX_CHECK(parsed.ok());
    NX_CHECK(parsed.diagnostics.empty());
    if (!parsed.ok()) {
        return;
    }
    const canview::EdsDictionary& dictionary = *parsed.dictionary;
    NX_CHECK(dictionary.object_count() >= 11U);
    NX_CHECK(dictionary.source_path() == "test.eds");
    NX_CHECK(dictionary.content_hash() != 0U);

    const auto* heartbeat = dictionary.find(0x1017U, 0U);
    NX_CHECK(heartbeat != nullptr);
    if (heartbeat != nullptr) {
        NX_CHECK(heartbeat->name == "Producer heartbeat time");
        NX_CHECK(heartbeat->data_type == 0x0006U);
        NX_CHECK(heartbeat->access == canview::EdsAccess::ReadWrite);
        NX_CHECK(heartbeat->has_default);
        NX_CHECK(heartbeat->default_value.value == 1000U);
    }
    const auto* compact = dictionary.find(0x2000U, 2U);
    NX_CHECK(compact != nullptr);
    if (compact != nullptr) {
        NX_CHECK(compact->name == "Compact_2");
    }
    NX_CHECK(dictionary.find(0x2000U, 0U) != nullptr);

    const auto& mappings = dictionary.mappings();
    NX_CHECK(!mappings.empty());
    if (!mappings.empty()) {
        const canview::EdsPdoMapping& mapping = mappings.front();
        NX_CHECK(mapping.transmit);
        NX_CHECK(mapping.pdo == 1U);
        NX_CHECK(mapping.count == 2U);
        NX_CHECK(mapping.entries[0].index == 0x2000U);
        NX_CHECK(mapping.entries[0].bits == 32U);
        NX_CHECK(mapping.entries[1].index == 0x2001U);
        NX_CHECK(mapping.entries[1].subindex == 1U);
        NX_CHECK(mapping.entries[1].bits == 16U);
    }

    const auto& cob_ids = dictionary.cob_ids();
    NX_CHECK(cob_ids.size() == 2U);
    for (const auto& entry : cob_ids) {
        if (entry.transmit) {
            NX_CHECK(entry.pdo == 1U);
            NX_CHECK(entry.cob_id.node_relative);
            NX_CHECK(entry.cob_id.resolve(2U) == 0x302U);
        } else {
            NX_CHECK(entry.cob_id.resolve(2U) == 0x202U);
        }
    }

    NX_CHECK(canview::eds_data_type(0x0006U).bits == 16U);
    NX_CHECK(canview::eds_data_type(0x0004U).signed_value);
    NX_CHECK(canview::eds_data_type(0x0008U).kind == canview::EdsDataKind::Real);
    NX_CHECK(canview::eds_data_type(0x9999U).kind == canview::EdsDataKind::Unknown);
    NX_CHECK(canview::parse_eds_access("rww") == canview::EdsAccess::ReadWriteOutput);

    const auto malformed = canview::EdsParser::parse(
        "[2000]\nParameterName=Test\nthis line has no equals\nObjectType=not-a-number\n",
        "bad.eds");
    NX_CHECK(malformed.ok());
    NX_CHECK(malformed.diagnostics.size() >= 2U);
    NX_CHECK(!malformed.diagnostics.front().to_string().empty());

    canview::EdsLimits tiny{};
    tiny.max_objects = 2U;
    NX_CHECK(!canview::EdsParser::parse(kEds, "tiny.eds", tiny).ok());

    canview::EdsLimits name_limit{};
    name_limit.max_name_bytes = 4U;
    const auto truncated =
        canview::EdsParser::parse("[2000]\nParameterName=LongName\n", "name.eds", name_limit);
    NX_CHECK(truncated.ok());
    NX_CHECK(!truncated.diagnostics.empty());
    if (truncated.ok()) {
        const auto* object = truncated.dictionary->find(0x2000U, 0U);
        NX_CHECK(object != nullptr && object->name.size() == 4U);
    }

    canview::EdsLimits size_limit{};
    size_limit.max_file_bytes = 16U;
    NX_CHECK(!canview::EdsParser::parse(kEds, "size.eds", size_limit).ok());
}

void write_file(const std::string& path, std::string_view content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
}

void test_eds_library() {
    const std::string path = "/tmp/opencode/canview-eds-test.eds";
    write_file(path, kEds);
    canview::EdsLibrary library;
    auto first = library.load(path);
    NX_CHECK(first.dictionary != nullptr);
    NX_CHECK(!first.from_cache);
    NX_CHECK(library.file_count() == 1U);
    NX_CHECK(library.total_bytes() == kEds.size());

    auto second = library.load(path);
    NX_CHECK(second.from_cache);
    NX_CHECK(second.dictionary == first.dictionary);

    write_file(path, std::string(kEds) + "\n; changed\n");
    auto third = library.load(path);
    NX_CHECK(third.reloaded);
    NX_CHECK(!third.from_cache);
    NX_CHECK(third.dictionary != nullptr);
    NX_CHECK(library.find(path) != nullptr);

    canview::EdsLibrary small_budget(8U);
    NX_CHECK(small_budget.load(path).failed);

    canview::EdsLimits tiny{};
    tiny.max_file_bytes = 32U;
    canview::EdsLibrary limited(1024U, tiny);
    NX_CHECK(limited.load(path).failed);

    canview::EdsLibrary keeping_old;
    NX_CHECK(keeping_old.load(path).dictionary != nullptr);
    const std::string bigger = std::string(kEds) + std::string(5000U, ' ');
    write_file("/tmp/opencode/canview-eds-big.eds", bigger);
    canview::EdsLimits small_file{};
    small_file.max_file_bytes = 1024U;
    canview::EdsLibrary old_keep(1024U * 1024U, small_file);
    write_file("/tmp/opencode/canview-eds-cache.eds", kEds);
    NX_CHECK(old_keep.load("/tmp/opencode/canview-eds-cache.eds").dictionary != nullptr);
    std::ofstream overwrite("/tmp/opencode/canview-eds-cache.eds",
                            std::ios::binary | std::ios::trunc);
    overwrite << bigger;
    overwrite.close();
    const auto failed_reload = old_keep.load("/tmp/opencode/canview-eds-cache.eds");
    NX_CHECK(failed_reload.failed);
    NX_CHECK(failed_reload.dictionary != nullptr);
}

std::shared_ptr<const canview::EdsDictionary> make_dictionary(std::string_view content) {
    auto parsed = canview::EdsParser::parse(content, "memory.eds");
    if (!parsed.ok()) {
        return nullptr;
    }
    return std::shared_ptr<const canview::EdsDictionary>(std::move(parsed.dictionary));
}

void test_registry() {
    auto dictionary = make_dictionary(kEds);
    NX_CHECK(dictionary != nullptr);
    canview::CanOpenRegistry registry;
    const auto slot = registry.bind(1U, 2U, "Drive", 0xFF00FF00U, dictionary, "test.eds");
    NX_CHECK(slot.has_value());
    if (!slot) {
        return;
    }
    NX_CHECK(*slot == 0U);
    const auto* binding = registry.find(1U, 2U);
    NX_CHECK(binding != nullptr);
    if (binding != nullptr) {
        NX_CHECK(binding->alias == "Drive");
        NX_CHECK(binding->colour == 0xFF00FF00U);
        NX_CHECK(binding->eds_hash == dictionary->content_hash());
        NX_CHECK(binding->heartbeat_timeout_ns == 3'000'000'000ULL);
        NX_CHECK(binding->registry_slot == 0U);
    }
    NX_CHECK(!registry.bind(1U, 2U, "Again", 0U, dictionary, "test.eds").has_value());
    const auto replaced =
        registry.bind(1U, 2U, "Replaced", 0xFFFFFFFFU, dictionary, "test.eds", true);
    NX_CHECK(replaced.has_value() && *replaced == *slot);
    NX_CHECK(registry.find(1U, 2U)->alias == "Replaced");
    NX_CHECK(!registry.bind(1U, 0U, "Bad", 0U, nullptr, "").has_value());

    registry.observe_heartbeat(*slot, canview::NmtState::Operational, 1'000'000'000ULL);
    NX_CHECK(registry.find(1U, 2U)->nmt_state == canview::NmtState::Operational);
    NX_CHECK(!registry.find(1U, 2U)->stale(1'000'000'000ULL + 2'000'000'000ULL));
    NX_CHECK(registry.find(1U, 2U)->stale(1'000'000'000ULL + 4'000'000'000ULL));

    canview::CanOpenRegistry full;
    std::uint16_t last = 0U;
    for (std::uint8_t node = 1U; node < 128U; ++node) {
        const auto bound = full.bind(1U, node, "", 0U, nullptr, "");
        NX_CHECK(bound.has_value());
        if (bound.has_value()) {
            last = *bound;
        }
    }
    NX_CHECK(full.bind(2U, 1U, "", 0U, nullptr, "").has_value());
    NX_CHECK(full.size() == canview::CanOpenRegistry::capacity);
    const auto overflow = full.bind(2U, 2U, "", 0U, nullptr, "");
    NX_CHECK(!overflow);
    if (!overflow) {
        NX_CHECK(overflow.error() == nexigen::core::ErrorCode::ResourceExhausted);
    }
    NX_CHECK(full.unbind_slot(last));
    NX_CHECK(full.size() == canview::CanOpenRegistry::capacity - 1U);
}

void test_router() {
    auto dictionary = make_dictionary(kEds);
    NX_CHECK(dictionary != nullptr);
    canview::CanOpenRegistry registry;
    const auto slot = registry.bind(1U, 2U, "Drive", 0U, dictionary, "test.eds");
    NX_CHECK(slot.has_value());
    canview::CobIdRouter router;
    router.rebuild(registry);

    const auto tpdo = router.lookup(1U, 0x302U, false);
    NX_CHECK(tpdo.has_value());
    if (tpdo.has_value()) {
        NX_CHECK(tpdo->service == canview::CoService::Tpdo);
        NX_CHECK(tpdo->pdo == 1U);
        NX_CHECK(tpdo->custom);
        NX_CHECK(tpdo->slot == *slot);
    }
    NX_CHECK(!router.lookup(1U, 0x182U, false).has_value()); // replaced by custom
    NX_CHECK(router.lookup(1U, 0x202U, false).has_value());
    NX_CHECK(router.lookup(1U, 0x082U, false).has_value());
    NX_CHECK(router.lookup(1U, 0x582U, false).has_value());
    NX_CHECK(router.lookup(1U, 0x602U, false).has_value());
    NX_CHECK(router.lookup(1U, 0x702U, false).has_value());
    NX_CHECK(router.lookup(1U, 0x000U, false).has_value());
    NX_CHECK(router.lookup(1U, 0x080U, false).has_value());
    NX_CHECK(router.lookup(1U, 0x100U, false).has_value());
    NX_CHECK(!router.lookup(1U, 0x302U, true).has_value());
    NX_CHECK(!router.lookup(2U, 0x302U, false).has_value());

    constexpr std::string_view conflicting = R"([1800]
ObjectType=0x9
SubNumber=2
[1800sub1]
COB-ID=0x302
)";
    auto conflict_dictionary = make_dictionary(conflicting);
    NX_CHECK(conflict_dictionary != nullptr);
    NX_CHECK(registry.bind(1U, 3U, "Other", 0U, conflict_dictionary, "conflict.eds").has_value());
    router.rebuild(registry);
    NX_CHECK(!router.conflicts().empty());
    const auto kept = router.lookup(1U, 0x302U, false);
    NX_CHECK(kept.has_value());
    if (kept.has_value()) {
        NX_CHECK(kept->slot == *slot);
    }

    constexpr std::string_view disabled = R"([1800]
ObjectType=0x9
SubNumber=2
[1800sub1]
COB-ID=0x80000000
)";
    auto disabled_dictionary = make_dictionary(disabled);
    NX_CHECK(disabled_dictionary != nullptr);
    canview::CanOpenRegistry disabled_registry;
    NX_CHECK(disabled_registry.bind(1U, 4U, "", 0U, disabled_dictionary, "").has_value());
    canview::CobIdRouter disabled_router;
    disabled_router.rebuild(disabled_registry);
    NX_CHECK(!disabled_router.lookup(1U, 0x184U, false).has_value());
}

void test_reindex() {
    auto dictionary = make_dictionary(kEds);
    NX_CHECK(dictionary != nullptr);
    auto ring = canview::CanRecordRing::create(1024U);
    NX_CHECK(ring.has_value());
    if (!ring || dictionary == nullptr) {
        return;
    }
    const auto first = push_frame(*ring, 1U, 0x202U);
    const auto second = push_frame(*ring, 1U, 0x302U);
    const auto other = push_frame(*ring, 2U, 0x182U);

    canview::CanOpenRegistry registry;
    const auto slot = registry.bind(1U, 2U, "Drive", 0U, dictionary, "test.eds");
    NX_CHECK(slot.has_value());
    if (!slot) {
        return;
    }
    canview::CobIdRouter router;
    router.rebuild(registry);
    canview::CanOpenReindexer reindexer;
    reindexer.begin(*ring, router);
    NX_CHECK(reindexer.active());
    auto progress = reindexer.step(*ring, 1U);
    NX_CHECK(progress.active);
    while (reindexer.active()) {
        progress = reindexer.step(*ring, 1U);
    }
    NX_CHECK(!progress.active);
    NX_CHECK(reindexer.assigned() >= 2U);
    NX_CHECK(ring->find(first)->co_node_index == *slot);
    NX_CHECK(ring->find(second)->co_node_index == *slot);
    NX_CHECK(ring->find(other)->co_node_index == canview::CanRecordView::no_node);
    NX_CHECK(reindexer.progress().ratio() == 1.0);

    NX_CHECK(registry.unbind(1U, 2U));
    router.rebuild(registry);
    reindexer.begin(*ring, router);
    while (reindexer.active()) {
        static_cast<void>(reindexer.step(*ring, 2U));
    }
    NX_CHECK(ring->find(first)->co_node_index == canview::CanRecordView::no_node);
    NX_CHECK(ring->find(second)->co_node_index == canview::CanRecordView::no_node);
    NX_CHECK(!reindexer.progress().to_string().empty());
}

} // namespace

namespace canview_test {

void run_canopen_tests() {
    test_eds_parser();
    test_eds_library();
    test_registry();
    test_router();
    test_reindex();
}

} // namespace canview_test
