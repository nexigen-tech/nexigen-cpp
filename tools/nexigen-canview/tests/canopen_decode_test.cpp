// NMT, EMCY, SDO, PDO, remap and presentation tests.
#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>

#include <cstdio>

#include <nexigen/hw/can/can_message.hpp>

#include "canopen_decoder.hpp"
#include "canopen_registry.hpp"
#include "session_record.hpp"
#include "co_runtime.hpp"
#include "cob_id_router.hpp"
#include "eds_parser.hpp"
#include "emcy_decoder.hpp"
#include "pdo_decoder.hpp"
#include "record_meta.hpp"
#include "sdo_decoder.hpp"
#include "suite.hpp"
#include "support/check.hpp"

namespace {

namespace canview = nexigen::canview;
namespace hwcan = nexigen::hw::can;

constexpr std::string_view kEds = R"([1000]
ParameterName=DeviceType
DataType=0x0007
[1017]
DataType=0x0006
DefaultValue=1000
[1400]
ObjectType=0x9
SubNumber=2
[1400sub1]
COB-ID=$NODEID+0x200
[1800]
ObjectType=0x9
SubNumber=2
[1800sub1]
COB-ID=$NODEID+0x300
[1A00]
ObjectType=0x9
SubNumber=4
[1A00sub1]
DefaultValue=0x20000020
[1A00sub2]
DefaultValue=0x20010010
[1A00sub3]
DefaultValue=0x20020020
[2000]
ParameterName=DrivePosition
DataType=0x0007
DefaultValue=0x12345678
[2001]
ParameterName=DriveCurrent
DataType=0x0003
[2002]
ParameterName=DriveTemperature
DataType=0x0008
[2003]
ParameterName=DriveName
DataType=0x0009
)";

std::shared_ptr<const canview::EdsDictionary> make_dictionary() {
    auto parsed = canview::EdsParser::parse(kEds, "decode.eds");
    if (!parsed.ok()) {
        return nullptr;
    }
    return std::shared_ptr<const canview::EdsDictionary>(std::move(parsed.dictionary));
}

hwcan::CanMessage make_message(std::uint32_t can_id, std::initializer_list<std::uint8_t> data) {
    hwcan::CanMessage message{};
    message.channel_id = 1U;
    message.can_id = can_id;
    message.dlc = static_cast<std::uint8_t>(data.size());
    std::size_t index = 0U;
    for (const std::uint8_t value : data) {
        if (index < message.data.size()) {
            message.data[index++] = value;
        }
    }
    return message;
}

canview::CanRecordView make_view(const hwcan::CanMessage& message, std::uint8_t map_epoch = 0U) {
    canview::CanRecordView view{};
    view.seq = 1U;
    view.message = &message;
    view.meta = canview::pack_frame_meta(canview::CanRecordKind::Frame);
    view.co_map_epoch = map_epoch;
    return view;
}

struct Fixture {
    std::shared_ptr<const canview::EdsDictionary> dictionary{};
    canview::CanOpenRegistry registry{};
    canview::CobIdRouter router{};
    std::uint16_t slot{0U};

    Fixture() {
        dictionary = make_dictionary();
        const auto bound = registry.bind(1U, 2U, "Drive", 0xFF00FF00U, dictionary, "decode.eds");
        if (bound.has_value()) {
            slot = *bound;
        }
        router.rebuild(registry);
    }

    [[nodiscard]] bool valid() const { return dictionary != nullptr; }
};

void test_nmt_and_heartbeat() {
    Fixture fixture;
    NX_CHECK(fixture.valid());
    if (!fixture.valid()) {
        return;
    }
    canview::CanOpenDecoder decoder{fixture.registry, fixture.router};
    auto message = make_message(0x000U, {1U, 0U});
    auto decoded = decoder.decode(make_view(message), 1'000U);
    NX_CHECK(decoded.service == canview::CoService::Nmt);
    NX_CHECK(decoded.status == canview::CoDecodeStatus::Ok);
    NX_CHECK(fixture.registry.find(1U, 2U)->nmt_state == canview::NmtState::Operational);

    message = make_message(0x702U, {5U});
    decoded = decoder.decode(make_view(message), 2'000U);
    NX_CHECK(decoded.service == canview::CoService::Heartbeat);
    NX_CHECK(decoded.nmt_state == canview::NmtState::Operational);
    NX_CHECK(fixture.registry.find(1U, 2U)->heartbeat_seen);

    message = make_message(0x702U, {0U});
    decoded = decoder.decode(make_view(message), 3'000U);
    NX_CHECK(decoded.nmt_state == canview::NmtState::BootUp);

    message = make_message(0x000U, {2U, 2U});
    decoded = decoder.decode(make_view(message), 4'000U);
    NX_CHECK(fixture.registry.find(1U, 2U)->nmt_state == canview::NmtState::Stopped);
}

void test_emcy_and_sync() {
    Fixture fixture;
    NX_CHECK(fixture.valid());
    if (!fixture.valid()) {
        return;
    }
    canview::CanOpenDecoder decoder{fixture.registry, fixture.router};
    auto message = make_message(0x082U, {0x00U, 0x10U, 0x01U, 0U, 0U, 0U, 0U, 0U});
    auto decoded = decoder.decode(make_view(message), 0U);
    NX_CHECK(decoded.service == canview::CoService::Emcy);
    NX_CHECK(decoded.error);
    NX_CHECK(decoded.signal_count == 3U);
    NX_CHECK(std::string(decoded.text.data()).find("Generic error") != std::string::npos);

    message = make_message(0x080U, {0U});
    decoded = decoder.decode(make_view(message), 0U);
    NX_CHECK(decoded.service == canview::CoService::Sync);
    NX_CHECK(!decoded.error);
}

void test_sdo_upload() {
    Fixture fixture;
    NX_CHECK(fixture.valid());
    if (!fixture.valid()) {
        return;
    }
    canview::CanOpenDecoder decoder{fixture.registry, fixture.router};
    auto message = make_message(0x582U, {0x43U, 0x00U, 0x10U, 0x00U, 0x78U, 0x56U, 0x34U, 0x12U});
    auto decoded = decoder.decode(make_view(message), 0U);
    NX_CHECK(decoded.service == canview::CoService::SdoResponse);
    NX_CHECK(decoded.index == 0x1000U);
    NX_CHECK(decoded.signal_count == 1U);
    if (decoded.signal_count == 1U) {
        NX_CHECK(decoded.signals[0].raw == 0x12345678U);
        NX_CHECK(std::string(decoded.signals[0].name) == "DeviceType");
    }

    // Segmented upload: initiate size 10, then two segments.
    message = make_message(0x582U, {0x41U, 0x00U, 0x10U, 0x00U, 0x0AU, 0U, 0U, 0U});
    decoded = decoder.decode(make_view(message), 0U);
    NX_CHECK(decoded.status == canview::CoDecodeStatus::Ok);
    message = make_message(0x582U, {0x02U, 'h', 'e', 'l', 'l', 'o', ' ', 'w'});
    decoded = decoder.decode(make_view(message), 0U);
    NX_CHECK(decoded.status == canview::CoDecodeStatus::Ok);
    message = make_message(0x582U, {0x00U, 'o', 'r', 'l', 'd', 0U, 0U, 0U});
    decoded = decoder.decode(make_view(message), 0U);
    NX_CHECK(std::string(decoded.text.data()).find("complete") != std::string::npos);

    message = make_message(0x582U, {0x41U, 0x00U, 0x10U, 0x00U, 0x88U, 0x13U, 0U, 0U});
    decoded = decoder.decode(make_view(message), 0U);
    NX_CHECK(decoded.status == canview::CoDecodeStatus::TooLarge);
}

void test_sdo_write_and_abort() {
    Fixture fixture;
    NX_CHECK(fixture.valid());
    if (!fixture.valid()) {
        return;
    }
    canview::SdoDecoder sdo;
    canview::CoDecoded decoded{};
    auto request = make_message(0x602U, {0x2FU, 0x00U, 0x20U, 0x00U, 0x2AU, 0U, 0U, 0U});
    NX_CHECK(sdo.feed(request, false, decoded, 0U));
    auto response = make_message(0x582U, {0x60U, 0x00U, 0x20U, 0x00U, 0U, 0U, 0U, 0U});
    NX_CHECK(sdo.feed(response, true, decoded, 1U));
    const auto completed = sdo.take_completed();
    NX_CHECK(completed.has_value());
    if (completed.has_value()) {
        NX_CHECK(completed->index == 0x2000U);
        NX_CHECK(completed->subindex == 0U);
        NX_CHECK(completed->size == 1U);
        NX_CHECK(completed->data[0] == 0x2AU);
    }

    auto abort = make_message(0x582U, {0x80U, 0x00U, 0x20U, 0x00U, 0x00U, 0x00U, 0x02U, 0x06U});
    NX_CHECK(sdo.feed(abort, true, decoded, 2U));
    NX_CHECK(decoded.status == canview::CoDecodeStatus::Aborted);
    NX_CHECK(decoded.abort_code == 0x06020000U);
    NX_CHECK(!sdo.take_completed().has_value());

    // Two node transfers interleave without mixing.
    canview::SdoDecoder isolated;
    canview::CoDecoded node2{};
    canview::CoDecoded node3{};
    NX_CHECK(isolated.feed(make_message(0x582U, {0x41U, 0x00U, 0x10U, 0x00U, 0x07U, 0U, 0U, 0U}),
                           true, node2, 0U));
    NX_CHECK(isolated.feed(make_message(0x583U, {0x41U, 0x00U, 0x20U, 0x00U, 0x05U, 0U, 0U, 0U}),
                           true, node3, 0U));
    NX_CHECK(isolated.active_assemblies() == 2U);
    NX_CHECK(isolated.feed(make_message(0x582U, {0x02U, 'a', 'b', 'c', 'd', 'e', 'f', 'g'}), true,
                           node2, 1U));
    NX_CHECK(isolated.feed(make_message(0x583U, {0x02U, 'x', 'y', 'z', 'w', 'v', 0U, 0U}), true,
                           node3, 1U));
    NX_CHECK(
        isolated.feed(make_message(0x582U, {0x00U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}), true, node2, 2U));
    NX_CHECK(
        isolated.feed(make_message(0x583U, {0x00U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}), true, node3, 3U));
    NX_CHECK(isolated.active_assemblies() == 0U);

    canview::SdoDecoder timing_out;
    NX_CHECK(timing_out.feed(make_message(0x582U, {0x41U, 0x00U, 0x10U, 0x00U, 0x0AU, 0U, 0U, 0U}),
                             true, node2, 0U));
    NX_CHECK(timing_out.active_assemblies() == 1U);
    timing_out.expire(canview::SdoDecoder::timeout_ns + 1U);
    NX_CHECK(timing_out.active_assemblies() == 0U);

    // A successful mapping write changes the runtime epoch.
    canview::CanOpenDecoder decoder{fixture.registry, fixture.router};
    auto write_request =
        make_message(0x602U, {0x23U, 0x00U, 0x1AU, 0x01U, 0x20U, 0x00U, 0x00U, 0x20U});
    static_cast<void>(decoder.decode(make_view(write_request), 0U));
    auto write_response = make_message(0x582U, {0x60U, 0x00U, 0x1AU, 0x01U, 0U, 0U, 0U, 0U});
    static_cast<void>(decoder.decode(make_view(write_response), 1U));
    const canview::CoNodeRuntime* runtime = decoder.runtime(fixture.slot);
    NX_CHECK(runtime != nullptr);
    if (runtime != nullptr) {
        NX_CHECK(runtime->current_epoch() == 1U);
    }
    auto aborted = make_message(0x582U, {0x80U, 0x00U, 0x1AU, 0x01U, 0x00U, 0x00U, 0x02U, 0x06U});
    const auto abort_decoded = decoder.decode(make_view(aborted, 1U), 2U);
    NX_CHECK(abort_decoded.status == canview::CoDecodeStatus::Aborted);
    if (runtime != nullptr) {
        NX_CHECK(runtime->current_epoch() == 1U);
    }
}

void test_pdo_decode() {
    auto dictionary = make_dictionary();
    NX_CHECK(dictionary != nullptr);
    if (dictionary == nullptr) {
        return;
    }
    canview::EdsPdoMapping mapping{};
    mapping.count = 3U;
    mapping.entries[0] = {0x2000U, 0U, 32U};
    mapping.entries[1] = {0x2001U, 0U, 16U};
    mapping.entries[2] = {0x2002U, 0U, 32U};
    auto message = make_message(0x182U, {0x78U, 0x56U, 0x34U, 0x12U, 0xFEU, 0xFFU, 0x00U, 0x00U});
    message.data[6] = 0x00U;
    message.data[7] = 0x00U;
    // Extend to 12 bytes is impossible with classic dlc; use a dedicated REAL
    // mapping below instead.
    canview::CoDecoded decoded{};
    NX_CHECK(canview::decode_pdo(message, mapping, dictionary.get(), decoded));
    NX_CHECK(decoded.status == canview::CoDecodeStatus::Partial);
    NX_CHECK(decoded.signal_count == 2U);

    canview::EdsPdoMapping unsigned_mapping{};
    unsigned_mapping.count = 1U;
    unsigned_mapping.entries[0] = {0x2000U, 0U, 32U};
    decoded.reset();
    NX_CHECK(canview::decode_pdo(message, unsigned_mapping, dictionary.get(), decoded));
    NX_CHECK(decoded.signal_count == 1U);
    if (decoded.signal_count == 1U) {
        NX_CHECK(decoded.signals[0].raw == 0x12345678U);
        NX_CHECK(std::string(decoded.signals[0].name) == "DrivePosition");
    }

    canview::EdsPdoMapping signed_mapping{};
    signed_mapping.count = 1U;
    signed_mapping.entries[0] = {0x2001U, 0U, 16U};
    decoded.reset();
    NX_CHECK(canview::decode_pdo(message, signed_mapping, dictionary.get(), decoded));
    if (decoded.signal_count == 1U) {
        NX_CHECK(decoded.signals[0].signed_value == 0x5678);
    }

    canview::EdsPdoMapping unknown{};
    unknown.count = 1U;
    unknown.entries[0] = {0x9999U, 0U, 8U};
    decoded.reset();
    NX_CHECK(canview::decode_pdo(message, unknown, dictionary.get(), decoded));
    NX_CHECK(decoded.status == canview::CoDecodeStatus::UnknownObject);
    NX_CHECK(decoded.signal_count == 1U);

    canview::EdsPdoMapping short_mapping{};
    short_mapping.count = 1U;
    short_mapping.entries[0] = {0x2000U, 0U, 32U};
    canview::EdsPdoMapping bit_mapping{};
    bit_mapping.count = 1U;
    bit_mapping.entries[0] = {0x2000U, 0U, 4U};
    auto bit_message = make_message(0x182U, {0x0AU});
    decoded.reset();
    NX_CHECK(canview::decode_pdo(bit_message, bit_mapping, dictionary.get(), decoded));
    if (decoded.signal_count == 1U) {
        NX_CHECK(decoded.signals[0].raw == 10U);
    }

    canview::EdsPdoMapping string_mapping{};
    string_mapping.count = 1U;
    string_mapping.entries[0] = {0x2003U, 0U, 40U};
    auto string_message = make_message(0x182U, {'h', 'e', 'l', 'l', 'o'});
    decoded.reset();
    NX_CHECK(canview::decode_pdo(string_message, string_mapping, dictionary.get(), decoded));
    if (decoded.signal_count == 1U) {
        NX_CHECK(std::string(decoded.signals[0].text.data()) == "hello");
        NX_CHECK(decoded.signals[0].kind == canview::EdsDataKind::String);
    }

    auto short_message = make_message(0x182U, {0x01U});
    decoded.reset();
    NX_CHECK(!canview::decode_pdo(short_message, short_mapping, dictionary.get(), decoded));
    NX_CHECK(decoded.status == canview::CoDecodeStatus::Partial);

    canview::EdsPdoMapping real_mapping{};
    real_mapping.count = 1U;
    real_mapping.entries[0] = {0x2002U, 0U, 32U};
    auto real_message = make_message(0x182U, {0x00U, 0x00U, 0xC0U, 0x3FU});
    decoded.reset();
    NX_CHECK(canview::decode_pdo(real_message, real_mapping, dictionary.get(), decoded));
    if (decoded.signal_count == 1U) {
        NX_CHECK(decoded.signals[0].real > 1.49 && decoded.signals[0].real < 1.51);
    }
}

void test_remap_epochs() {
    auto dictionary = make_dictionary();
    NX_CHECK(dictionary != nullptr);
    if (dictionary == nullptr) {
        return;
    }
    canview::CanOpenRegistry registry;
    const auto slot = registry.bind(1U, 2U, "Drive", 0U, dictionary, "decode.eds");
    NX_CHECK(slot.has_value());
    if (!slot) {
        return;
    }
    canview::CobIdRouter router;
    router.rebuild(registry);
    canview::CoNodeRuntime runtime;
    runtime.init(*dictionary, 1U, 2U, *slot);
    NX_CHECK(runtime.current().tpdo_cob[0] == 0x302U);
    NX_CHECK(router.lookup(1U, 0x302U, false).has_value());

    const std::array<std::uint8_t, 4> cob_id{0x23U, 0x01U, 0x00U, 0x00U};
    NX_CHECK(runtime.apply_write(0x1800U, 1U, cob_id.data(), cob_id.size(), &router));
    NX_CHECK(runtime.current_epoch() == 1U);
    NX_CHECK(runtime.current().tpdo_cob[0] == 0x123U);
    NX_CHECK(router.lookup(1U, 0x123U, false).has_value());
    NX_CHECK(!router.lookup(1U, 0x302U, false).has_value());

    const std::array<std::uint8_t, 4> mapping{0x20U, 0x00U, 0x00U, 0x20U};
    NX_CHECK(runtime.apply_write(0x1A00U, 1U, mapping.data(), mapping.size(), &router));
    NX_CHECK(runtime.current_epoch() == 2U);
    bool stale = false;
    const canview::PdoSnapshot* old = runtime.snapshot(0U, stale);
    NX_CHECK(old != nullptr && !stale);
    const canview::PdoSnapshot* current = runtime.snapshot(2U, stale);
    NX_CHECK(current != nullptr && !stale);
    if (current != nullptr) {
        NX_CHECK(current->tpdo[0].entries[0].index == 0x2000U);
    }
    NX_CHECK(runtime.snapshot(99U, stale) != nullptr && stale);

    for (std::uint8_t index = 0U; index < 20U; ++index) {
        const std::array<std::uint8_t, 4> value{0x20U, 0x00U, 0x00U, 0x20U};
        NX_CHECK(runtime.apply_write(0x1A00U, 1U, value.data(), value.size(), &router));
    }
    NX_CHECK(runtime.retained_epochs() == canview::CoNodeRuntime::snapshot_count);
    NX_CHECK(runtime.snapshot(0U, stale) != nullptr && stale);
}

void test_canopen_vocabulary() {
    canview::CoDecoded decoded{};
    decoded.service = canview::CoService::Tpdo;
    decoded.status = canview::CoDecodeStatus::Ok;
    decoded.node_id = 2U;
    decoded.index = 0x2000U;
    canview::CoSignal signal{};
    signal.name = "DrivePosition";
    signal.kind = canview::EdsDataKind::Unsigned;
    signal.bits = 32U;
    signal.raw = 42U;
    std::snprintf(signal.text.data(), signal.text.size(), "42");
    NX_CHECK(decoded.add_signal(signal));
    decoded.set_text("tpdo");
    const nlohmann::json decoded_json = decoded;
    canview::CoDecoded decoded_back{};
    canview::from_json(decoded_json, decoded_back);
    NX_CHECK(decoded_back.service == canview::CoService::Tpdo);
    NX_CHECK(decoded_back.signal_count == 1U);
    NX_CHECK(decoded_back.signals[0].raw == 42U);
    NX_CHECK(std::string(decoded_back.signals[0].name) == "DrivePosition");
    NX_CHECK(!decoded_back.to_string().empty());
    NX_CHECK(!decoded_back.signals[0].to_string().empty());

    canview::NodeBinding binding{};
    binding.channel_id = 1U;
    binding.node_id = 2U;
    binding.alias = "Drive";
    binding.eds_path = "drive.eds";
    binding.eds_hash = 7U;
    const nlohmann::json binding_json = binding;
    canview::NodeBinding binding_back{};
    canview::from_json(binding_json, binding_back);
    NX_CHECK(binding_back.channel_id == 1U);
    NX_CHECK(binding_back.node_id == 2U);
    NX_CHECK(binding_back.alias == "Drive");

    canview::SessionEdsBinding session_binding{};
    session_binding.channel_id = 1U;
    session_binding.node_id = 2U;
    session_binding.path = "drive.eds";
    const nlohmann::json session_json = session_binding;
    canview::SessionEdsBinding session_back{};
    canview::from_json(session_json, session_back);
    NX_CHECK(session_back.path == "drive.eds");
    NX_CHECK(session_back.node_id == 2U);
}

void test_presentation() {
    Fixture fixture;
    NX_CHECK(fixture.valid());
    if (!fixture.valid()) {
        return;
    }
    canview::CanOpenDecoder decoder{fixture.registry, fixture.router};
    auto message = make_message(0x082U, {0x00U, 0x10U, 0x01U, 0U, 0U, 0U, 0U, 0U});
    const auto decoded = decoder.decode(make_view(message), 0U);
    NX_CHECK(decoder.colour(decoded) == 0xFF00FF00U);
    decoder.set_colour_mode(canview::NodeColourMode::Criticality);
    NX_CHECK(decoder.colour(decoded) == 0xFFE53935U);
    NX_CHECK(!canview::co_inspector_text(decoded).empty());
    NX_CHECK(canview::to_string(canview::CoDecodeStatus::Aborted) == "Aborted");
    NX_CHECK(canview::to_string(canview::NodeColourMode::Criticality) == "Criticality");
}

} // namespace

namespace canview_test {

void run_canopen_decode_tests() {
    test_nmt_and_heartbeat();
    test_emcy_and_sync();
    test_sdo_upload();
    test_sdo_write_and_abort();
    test_pdo_decode();
    test_remap_epochs();
    test_canopen_vocabulary();
    test_presentation();
}

} // namespace canview_test
