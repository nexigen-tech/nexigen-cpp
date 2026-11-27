// nexigen::canopen core primitives — transport-independent tests.
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <format>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include <nexigen/canopen/cob_id_binding.hpp>
#include <nexigen/canopen/cob_id_conflict.hpp>
#include <nexigen/canopen/cob_id_route.hpp>
#include <nexigen/canopen/cob_id_router.hpp>
#include <nexigen/canopen/co_service.hpp>
#include <nexigen/canopen/eds_access.hpp>
#include <nexigen/canopen/eds_data_kind.hpp>
#include <nexigen/canopen/eds_data_type_info.hpp>
#include <nexigen/canopen/eds_dictionary.hpp>
#include <nexigen/canopen/eds_limits.hpp>
#include <nexigen/canopen/eds_mapping_entry.hpp>
#include <nexigen/canopen/eds_object.hpp>
#include <nexigen/canopen/eds_parser.hpp>
#include <nexigen/canopen/eds_pdo_mapping.hpp>
#include <nexigen/canopen/nmt_state.hpp>
#include <nexigen/canopen/pdo_decode.hpp>
#include <nexigen/canopen/pdo_decode_result.hpp>
#include <nexigen/canopen/sdo_assembler.hpp>
#include <nexigen/canopen/sdo_completed_write.hpp>
#include <nexigen/canopen/sdo_event_kind.hpp>
#include <nexigen/hw/can/can_message.hpp>

namespace {

namespace co = nexigen::canopen;
namespace hwcan = nexigen::hw::can;

int failures = 0;

void check(bool condition, const char* text, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, text);
        ++failures;
    }
}

#define CO_CHECK(condition) check((condition), #condition, __LINE__)

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

std::unique_ptr<co::EdsDictionary> make_dictionary() {
    auto parsed = co::EdsParser::parse(kEds, "core.eds");
    if (!parsed.ok()) {
        return nullptr;
    }
    return std::move(parsed.dictionary);
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

template <class Enum> void check_enum(Enum first, std::string_view first_name) {
    CO_CHECK(co::to_string(first) == first_name);
    CO_CHECK(std::format("{}", first) == first_name);
    Enum parsed{};
    CO_CHECK(co::from_string(first_name, parsed));
    CO_CHECK(parsed == first);
    const nlohmann::json encoded = first;
    CO_CHECK(encoded.is_string());
    Enum decoded{};
    co::from_json(encoded, decoded);
    CO_CHECK(decoded == first);
    Enum untouched = first;
    co::from_json(nlohmann::json{123}, untouched);
    CO_CHECK(untouched == first);
    co::from_json(nlohmann::json{"no-such-value"}, untouched);
    CO_CHECK(untouched == first);
}

void test_vocabulary() {
    check_enum(co::NmtState::Operational, "Operational");
    check_enum(co::CoService::Tpdo, "Tpdo");
    check_enum(co::EdsAccess::ReadWrite, "ReadWrite");
    check_enum(co::EdsDataKind::Real, "Real");
    check_enum(co::PdoDecodeStatus::Partial, "Partial");
    check_enum(co::SdoEventKind::CompletedWrite, "CompletedWrite");
    check_enum(co::EdsObjectType::Var, "Var");
    check_enum(co::EdsObjectType::Record, "Record");
    CO_CHECK(co::eds_data_type(0x0006U).bits == 16U);
    CO_CHECK(co::eds_data_type(0x0004U).signed_value);
    CO_CHECK(co::eds_data_type(0x9999U).kind == co::EdsDataKind::Unknown);
    CO_CHECK(co::parse_eds_access("rww") == co::EdsAccess::ReadWriteOutput);
    CO_CHECK(co::parse_eds_access("RW") == co::EdsAccess::ReadWrite);

    co::PdoSignal signal{};
    signal.index = 0x2000U;
    signal.bits = 32U;
    signal.raw = 42U;
    signal.kind = co::EdsDataKind::Unsigned;
    const nlohmann::json signal_json = signal;
    co::PdoSignal signal_back{};
    co::from_json(signal_json, signal_back);
    CO_CHECK(signal_back.index == 0x2000U);
    CO_CHECK(signal_back.raw == 42U);
    CO_CHECK(signal_back.kind == co::EdsDataKind::Unsigned);

    co::PdoDecodeResult result{};
    result.status = co::PdoDecodeStatus::Ok;
    result.count = 1U;
    result.signals[0] = signal;
    const nlohmann::json result_json = result;
    co::PdoDecodeResult result_back{};
    co::from_json(result_json, result_back);
    CO_CHECK(result_back.status == co::PdoDecodeStatus::Ok);
    CO_CHECK(result_back.count == 1U && result_back.signals[0].index == 0x2000U);

    co::SdoEvent event{};
    event.kind = co::SdoEventKind::CompletedWrite;
    event.index = 0x2000U;
    event.size = 4U;
    const nlohmann::json event_json = event;
    co::SdoEvent event_back{};
    co::from_json(event_json, event_back);
    CO_CHECK(event_back.kind == co::SdoEventKind::CompletedWrite);
    CO_CHECK(event_back.index == 0x2000U && event_back.size == 4U);

    co::SdoCompletedWrite write{};
    write.index = 0x2001U;
    write.size = 1U;
    write.data[0] = 0x2AU;
    const nlohmann::json write_json = write;
    co::SdoCompletedWrite write_back{};
    co::from_json(write_json, write_back);
    CO_CHECK(write_back.index == 0x2001U);
    CO_CHECK(write_back.size == 1U && write_back.data[0] == 0x2AU);

    co::CobIdRoute route{};
    route.slot = 7U;
    route.node_id = 2U;
    route.service = co::CoService::Tpdo;
    const nlohmann::json route_json = route;
    co::CobIdRoute route_back{};
    co::from_json(route_json, route_back);
    CO_CHECK(route_back.slot == 7U && route_back.service == co::CoService::Tpdo);

    co::CobIdConflict conflict{};
    conflict.channel_id = 1U;
    conflict.can_id = 0x302U;
    const nlohmann::json conflict_json = conflict;
    co::CobIdConflict conflict_back{};
    co::from_json(conflict_json, conflict_back);
    CO_CHECK(conflict_back.channel_id == 1U && conflict_back.can_id == 0x302U);
}

void test_eds_parser() {
    auto parsed = co::EdsParser::parse(kEds, "core.eds");
    CO_CHECK(parsed.ok());
    CO_CHECK(parsed.diagnostics.empty());
    if (!parsed.ok()) {
        return;
    }
    const co::EdsDictionary& dictionary = *parsed.dictionary;
    CO_CHECK(dictionary.object_count() >= 8U);
    CO_CHECK(dictionary.source_path() == "core.eds");
    CO_CHECK(dictionary.content_hash() != 0U);
    const co::EdsObject* heartbeat = dictionary.find(0x1017U, 0U);
    CO_CHECK(heartbeat != nullptr);
    if (heartbeat != nullptr) {
        CO_CHECK(heartbeat->data_type == 0x0006U);
        CO_CHECK(heartbeat->default_value.value == 1000U);
    }
    CO_CHECK(!dictionary.mappings().empty());
    CO_CHECK(dictionary.cob_ids().size() == 2U);
    CO_CHECK(co::eds_content_hash("abc") != 0U);

    const auto malformed = co::EdsParser::parse(
        "[2000]\nParameterName=Test\nthis line has no equals\nObjectType=not-a-number\n",
        "bad.eds");
    CO_CHECK(malformed.ok());
    CO_CHECK(malformed.diagnostics.size() >= 2U);

    co::EdsLimits tiny{};
    tiny.max_objects = 2U;
    CO_CHECK(!co::EdsParser::parse(kEds, "tiny.eds", tiny).ok());

    co::EdsLimits name_limit{};
    name_limit.max_name_bytes = 4U;
    const auto truncated =
        co::EdsParser::parse("[2000]\nParameterName=LongName\n", "name.eds", name_limit);
    CO_CHECK(truncated.ok());
    if (truncated.ok()) {
        const co::EdsObject* object = truncated.dictionary->find(0x2000U, 0U);
        CO_CHECK(object != nullptr && object->name.size() == 4U);
    }
}

void test_pdo_unpack() {
    auto dictionary = make_dictionary();
    CO_CHECK(dictionary != nullptr);
    if (dictionary == nullptr) {
        return;
    }
    co::EdsPdoMapping mapping{};
    mapping.count = 4U;
    mapping.entries[0] = {0x2000U, 0U, 32U};
    mapping.entries[1] = {0x2001U, 0U, 16U};
    mapping.entries[2] = {0x2002U, 0U, 32U};
    mapping.entries[3] = {0x2003U, 0U, 16U};

    const float real = 1.5F;
    const std::uint32_t real_bits = std::bit_cast<std::uint32_t>(real);
    auto message = make_message(0x182U, {
                                            0x78U,
                                            0x56U,
                                            0x34U,
                                            0x12U,
                                            0x01U,
                                            0x80U,
                                            static_cast<std::uint8_t>(real_bits & 0xFFU),
                                            static_cast<std::uint8_t>((real_bits >> 8U) & 0xFFU),
                                            static_cast<std::uint8_t>((real_bits >> 16U) & 0xFFU),
                                            static_cast<std::uint8_t>((real_bits >> 24U) & 0xFFU),
                                            'O',
                                            'K',
                                        });
    const auto decoded = co::decode_pdo(message, mapping, dictionary.get());
    const auto decoded_again = co::decode_pdo(message, mapping, dictionary.get());
    CO_CHECK(decoded.status == decoded_again.status);
    CO_CHECK(decoded.count == decoded_again.count);
    CO_CHECK(decoded.signals[0].raw == decoded_again.signals[0].raw);
    CO_CHECK(decoded.status == co::PdoDecodeStatus::Ok);
    CO_CHECK(decoded.count == 4U);
    if (decoded.count == 4U) {
        CO_CHECK(decoded.signals[0].raw == 0x12345678U);
        CO_CHECK(decoded.signals[1].signed_value == -32767);
        CO_CHECK(decoded.signals[2].kind == co::EdsDataKind::Real);
        CO_CHECK(decoded.signals[2].real > 1.49 && decoded.signals[2].real < 1.51);
        CO_CHECK(decoded.signals[3].string_value);
        CO_CHECK(decoded.signals[3].byte_count == 2U);
        CO_CHECK(decoded.signals[3].bytes[0] == 'O' && decoded.signals[3].bytes[1] == 'K');
    }

    co::EdsPdoMapping packed{};
    packed.count = 2U;
    packed.entries[0] = {0x2000U, 0U, 12U};
    packed.entries[1] = {0x2001U, 0U, 12U};
    const std::uint32_t packed_value = 0xABCU | (0x123U << 12U);
    auto packed_message =
        make_message(0x182U, {
                                 static_cast<std::uint8_t>(packed_value & 0xFFU),
                                 static_cast<std::uint8_t>((packed_value >> 8U) & 0xFFU),
                                 static_cast<std::uint8_t>((packed_value >> 16U) & 0xFFU),
                             });
    const auto packed_result = co::decode_pdo(packed_message, packed, dictionary.get());
    CO_CHECK(packed_result.status == co::PdoDecodeStatus::Ok);
    CO_CHECK(packed_result.count == 2U);
    if (packed_result.count == 2U) {
        CO_CHECK((packed_result.signals[0].raw & 0xFFFU) == 0xABCU);
        CO_CHECK((packed_result.signals[1].raw & 0xFFFU) == 0x123U);
    }

    co::EdsPdoMapping unknown{};
    unknown.count = 1U;
    unknown.entries[0] = {0x9999U, 0U, 8U};
    const auto unknown_result =
        co::decode_pdo(make_message(0x182U, {0x01U}), unknown, dictionary.get());
    CO_CHECK(unknown_result.status == co::PdoDecodeStatus::UnknownObject);
    CO_CHECK(unknown_result.count == 1U && unknown_result.signals[0].unknown_object);

    co::EdsPdoMapping partial{};
    partial.count = 1U;
    partial.entries[0] = {0x2000U, 0U, 64U};
    const auto partial_result =
        co::decode_pdo(make_message(0x182U, {0x01U}), partial, dictionary.get());
    CO_CHECK(partial_result.status == co::PdoDecodeStatus::Partial);
    CO_CHECK(partial_result.count == 0U);
}

void test_sdo_assembler() {
    co::SdoAssembler assembler{};
    co::SdoEvent event{};

    auto upload = make_message(0x582U, {0x43U, 0x00U, 0x10U, 0x00U, 0x78U, 0x56U, 0x34U, 0x12U});
    CO_CHECK(assembler.feed(upload, true, 0U, event));
    CO_CHECK(event.kind == co::SdoEventKind::UploadResponse);
    CO_CHECK(event.value == 0x12345678U);
    CO_CHECK(event.value_bytes == 4U);

    auto write_request = make_message(0x602U, {0x2FU, 0x00U, 0x20U, 0x00U, 0x2AU, 0U, 0U, 0U});
    CO_CHECK(assembler.feed(write_request, false, 1U, event));
    CO_CHECK(event.kind == co::SdoEventKind::ExpeditedWriteRequest);
    auto write_response = make_message(0x582U, {0x60U, 0x00U, 0x20U, 0x00U, 0U, 0U, 0U, 0U});
    CO_CHECK(assembler.feed(write_response, true, 2U, event));
    CO_CHECK(event.kind == co::SdoEventKind::CompletedWrite);
    CO_CHECK(event.index == 0x2000U && event.subindex == 0U && event.size == 1U);
    const auto completed = assembler.take_completed();
    CO_CHECK(completed.has_value());
    if (completed.has_value()) {
        CO_CHECK(completed->index == 0x2000U && completed->size == 1U);
        CO_CHECK(completed->data[0] == 0x2AU);
    }

    auto abort = make_message(0x582U, {0x80U, 0x00U, 0x20U, 0x00U, 0x00U, 0x00U, 0x02U, 0x06U});
    CO_CHECK(assembler.feed(abort, true, 3U, event));
    CO_CHECK(event.kind == co::SdoEventKind::Abort);
    CO_CHECK(event.abort_code == 0x06020000U);

    auto segmented_init = make_message(0x582U, {0x41U, 0x00U, 0x10U, 0x00U, 0x0AU, 0U, 0U, 0U});
    CO_CHECK(assembler.feed(segmented_init, true, 0U, event));
    CO_CHECK(event.kind == co::SdoEventKind::UploadResponse && event.segmented);
    CO_CHECK(assembler.active_assemblies() == 1U);
    auto segment_one = make_message(0x582U, {0x02U, 'h', 'e', 'l', 'l', 'o', ' ', 'w'});
    CO_CHECK(assembler.feed(segment_one, true, 1U, event));
    CO_CHECK(event.kind == co::SdoEventKind::UploadSegment && !event.last);
    auto segment_two = make_message(0x582U, {0x00U, 'o', 'r', 'l', 'd', 0U, 0U, 0U});
    CO_CHECK(assembler.feed(segment_two, true, 2U, event));
    CO_CHECK(event.kind == co::SdoEventKind::UploadSegment && event.last);
    CO_CHECK(assembler.active_assemblies() == 0U);

    auto huge = make_message(0x582U, {0x41U, 0x00U, 0x10U, 0x00U, 0x88U, 0x13U, 0U, 0U});
    CO_CHECK(assembler.feed(huge, true, 0U, event));
    CO_CHECK(event.kind == co::SdoEventKind::TooLarge);
}

void test_cob_id_router() {
    auto dictionary = make_dictionary();
    CO_CHECK(dictionary != nullptr);
    if (dictionary == nullptr) {
        return;
    }
    co::CobIdBinding binding{};
    binding.slot = 7U;
    binding.channel_id = 1U;
    binding.node_id = 2U;
    binding.dictionary = dictionary.get();
    std::array<co::CobIdBinding, 1> bindings{binding};
    co::CobIdRouter router{};
    router.rebuild(bindings);

    const auto tpdo = router.lookup(1U, 0x302U, false);
    CO_CHECK(tpdo.has_value());
    if (tpdo.has_value()) {
        CO_CHECK(tpdo->service == co::CoService::Tpdo);
        CO_CHECK(tpdo->pdo == 1U && tpdo->custom);
        CO_CHECK(tpdo->slot == 7U);
    }
    CO_CHECK(!router.lookup(1U, 0x182U, false).has_value());
    CO_CHECK(router.lookup(1U, 0x202U, false).has_value());
    CO_CHECK(router.lookup(1U, 0x082U, false).has_value());
    CO_CHECK(router.lookup(1U, 0x582U, false).has_value());
    CO_CHECK(router.lookup(1U, 0x602U, false).has_value());
    CO_CHECK(router.lookup(1U, 0x702U, false).has_value());
    CO_CHECK(router.lookup(1U, 0x000U, false).has_value());
    CO_CHECK(router.lookup(1U, 0x080U, false).has_value());
    CO_CHECK(router.lookup(1U, 0x100U, false).has_value());
    CO_CHECK(!router.lookup(1U, 0x302U, true).has_value());
    CO_CHECK(!router.lookup(2U, 0x302U, false).has_value());

    co::CobIdBinding other{};
    other.slot = 8U;
    other.channel_id = 1U;
    other.node_id = 2U;
    other.dictionary = dictionary.get();
    std::array<co::CobIdBinding, 2> conflicting{binding, other};
    router.rebuild(conflicting);
    CO_CHECK(!router.conflicts().empty());
    const auto kept = router.lookup(1U, 0x302U, false);
    CO_CHECK(kept.has_value() && kept->slot == 7U);

    auto plain =
        co::EdsParser::parse("[2000]\nParameterName=Plain\nDataType=0x0007\n", "plain.eds");
    CO_CHECK(plain.ok());
    if (plain.ok()) {
        co::CobIdBinding default_binding{};
        default_binding.slot = 9U;
        default_binding.channel_id = 1U;
        default_binding.node_id = 2U;
        default_binding.dictionary = plain.dictionary.get();
        std::array<co::CobIdBinding, 1> defaults{default_binding};
        router.rebuild(defaults);
        const auto default_tpdo = router.lookup(1U, 0x182U, false);
        CO_CHECK(default_tpdo.has_value());
        if (default_tpdo.has_value()) {
            CO_CHECK(default_tpdo->service == co::CoService::Tpdo);
            CO_CHECK(default_tpdo->pdo == 1U && !default_tpdo->custom);
        }
    }
}

} // namespace

int main() {
    test_vocabulary();
    test_eds_parser();
    test_pdo_unpack();
    test_sdo_assembler();
    test_cob_id_router();
    std::printf("canopen core tests: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
