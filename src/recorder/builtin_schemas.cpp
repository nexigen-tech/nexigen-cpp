// nexigen::recorder — built-in schema descriptors.
#include <nexigen/recorder/builtin_schemas.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "crc32c_fast.hpp"

namespace nexigen::recorder {

namespace {

[[nodiscard]] SchemaDeclare make_schema(std::uint32_t id, std::string name, std::string fields) {
    SchemaDeclare schema{};
    schema.id = id;
    schema.name = std::move(name);
    schema.version = 1;
    schema.encoding = "jsonschema";
    schema.endianness = "little";
    const std::string text = "{\"type\":\"object\",\"fields\":" + std::move(fields) + "}";
    schema.data.assign(text.begin(), text.end());
    schema.data_crc32c = detail::crc32c(schema.data.data(), schema.data.size());
    return schema;
}

} // namespace

std::vector<SchemaDeclare> builtin_schemas() {
    std::vector<SchemaDeclare> schemas;
    schemas.push_back(make_schema(
        1, "nexigen.log.LogFrame",
        R"([{"name":"timestamp_ns","type":"u64","offset":0,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"app_id","type":"char[4]","offset":8,"element_size":1,"count":4,"stride":1},)"
        R"({"name":"context_id","type":"u32","offset":12,"element_size":4,"count":1,"stride":4},)"
        R"({"name":"thread_id","type":"u32","offset":16,"element_size":4,"count":1,"stride":4},)"
        R"({"name":"pid","type":"u32","offset":20,"element_size":4,"count":1,"stride":4},)"
        R"({"name":"dropped_counter","type":"u32","offset":24,"element_size":4,"count":1,"stride":4},)"
        R"({"name":"payload_size","type":"u16","offset":28,"element_size":2,"count":1,"stride":2},)"
        R"({"name":"log_level","type":"u8","offset":30,"element_size":1,"count":1,"stride":1},)"
        R"({"name":"payload","type":"u8[225]","offset":31,"element_size":1,"count":225,"stride":1}])"));
    schemas.push_back(make_schema(
        2, "nexigen.metrics.MetricFrame",
        R"([{"name":"app_id","type":"char[4]","offset":0,"element_size":1,"count":4,"stride":1},)"
        R"({"name":"pid","type":"u32","offset":4,"element_size":4,"count":1,"stride":4},)"
        R"({"name":"timestamp_ns","type":"i64","offset":8,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"meter_name","type":"char[64]","offset":16,"element_size":1,"count":64,"stride":1},)"
        R"({"name":"meter_version","type":"char[64]","offset":80,"element_size":1,"count":64,"stride":1},)"
        R"({"name":"metric_name","type":"char[64]","offset":144,"element_size":1,"count":64,"stride":1},)"
        R"({"name":"unit","type":"char[64]","offset":208,"element_size":1,"count":64,"stride":1},)"
        R"({"name":"kind","type":"u8","offset":272,"element_size":1,"count":1,"stride":1},)"
        R"({"name":"concurrency","type":"u8","offset":273,"element_size":1,"count":1,"stride":1},)"
        R"({"name":"access","type":"u8","offset":274,"element_size":1,"count":1,"stride":1},)"
        R"({"name":"reset","type":"u8","offset":275,"element_size":1,"count":1,"stride":1},)"
        R"({"name":"scalar_counter","type":"u64","offset":280,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"scalar_gauge_int","type":"i64","offset":288,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"scalar_gauge_double","type":"f64","offset":296,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"scalar_is_double","type":"u8","offset":304,"element_size":1,"count":1,"stride":1},)"
        R"({"name":"statistics_count","type":"u64","offset":280,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"statistics_min","type":"f64","offset":288,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"statistics_max","type":"f64","offset":296,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"statistics_mean","type":"f64","offset":304,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"statistics_stddev","type":"f64","offset":312,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"statistics_ignored","type":"u64","offset":320,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"statistics_dropped","type":"u64","offset":328,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"histogram_count","type":"u64","offset":280,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"histogram_sum","type":"f64","offset":288,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"histogram_min","type":"f64","offset":296,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"histogram_max","type":"f64","offset":304,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"histogram_ignored","type":"u64","offset":312,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"histogram_zero","type":"u64","offset":320,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"histogram_scale","type":"i32","offset":328,"element_size":4,"count":1,"stride":4},)"
        R"({"name":"histogram_p50","type":"f64","offset":336,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"histogram_p90","type":"f64","offset":344,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"histogram_p99","type":"f64","offset":352,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"histogram_p999","type":"f64","offset":360,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"histogram_bucket_count","type":"u32","offset":368,"element_size":4,"count":1,"stride":4},)"
        R"({"name":"histogram_buckets_lower","type":"f64[256]","offset":376,"element_size":8,"count":256,"stride":8},)"
        R"({"name":"histogram_buckets_count","type":"u64[256]","offset":2424,"element_size":8,"count":256,"stride":8},)"
        R"({"name":"state_value","type":"char[64]","offset":280,"element_size":1,"count":64,"stride":1}])"));
    schemas.push_back(make_schema(
        3, "nexigen.tracing.SpanFrame",
        R"([{"name":"trace_id","type":"u64","offset":0,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"span_id","type":"u64","offset":8,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"parent_span_id","type":"u64","offset":16,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"start_ns","type":"u64","offset":32,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"end_ns","type":"u64","offset":40,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"name","type":"char[48]","offset":72,"element_size":1,"count":48,"stride":1}])"));
    schemas.push_back(make_schema(
        4, "nexigen.recorder.GapMarker",
        R"([{"name":"first_lost_sequence","type":"u64","offset":0,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"lost_count","type":"u64","offset":8,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"reason","type":"u16","offset":16,"element_size":2,"count":1,"stride":2}])"));
    schemas.push_back(make_schema(
        5, "nexigen.recorder.TimeSync",
        R"([{"name":"mono_ns","type":"u64","offset":0,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"utc_offset_ns","type":"i64","offset":8,"element_size":8,"count":1,"stride":8}])"));
    schemas.push_back(make_schema(
        6, "nexigen.recorder.VideoFrameRef",
        R"([{"name":"vid_offset","type":"u64","offset":0,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"capture_mono_ns","type":"u64","offset":8,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"source_sequence","type":"u64","offset":16,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"config_epoch","type":"u64","offset":24,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"vid_size","type":"u32","offset":32,"element_size":4,"count":1,"stride":4},)"
        R"({"name":"vid_crc32c","type":"u32","offset":36,"element_size":4,"count":1,"stride":4},)"
        R"({"name":"width","type":"u32","offset":40,"element_size":4,"count":1,"stride":4},)"
        R"({"name":"height","type":"u32","offset":44,"element_size":4,"count":1,"stride":4},)"
        R"({"name":"codec","type":"u16","offset":48,"element_size":2,"count":1,"stride":2},)"
        R"({"name":"bitstream","type":"u16","offset":50,"element_size":2,"count":1,"stride":2},)"
        R"({"name":"vid_segment_seq","type":"u32","offset":52,"element_size":4,"count":1,"stride":4}])"));
    schemas.push_back(make_schema(
        7, "nexigen.recorder.VideoStreamConfig",
        R"([{"name":"codec","type":"u16","offset":0,"element_size":2,"count":1,"stride":2},)"
        R"({"name":"bitstream","type":"u16","offset":2,"element_size":2,"count":1,"stride":2},)"
        R"({"name":"width","type":"u32","offset":4,"element_size":4,"count":1,"stride":4},)"
        R"({"name":"height","type":"u32","offset":8,"element_size":4,"count":1,"stride":4},)"
        R"({"name":"config_epoch","type":"u64","offset":24,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"extradata","type":"u8[1024]","offset":32,"element_size":1,"count":1024,"stride":1}])"));
    schemas.push_back(make_schema(
        8, "nexigen.recorder.TimeBase",
        R"([{"name":"generation","type":"u64","offset":0,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"time_base_epoch","type":"u64","offset":8,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"boot_token","type":"u64","offset":16,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"layout","type":"u32","offset":24,"element_size":4,"count":1,"stride":4},)"
        R"({"name":"status","type":"u8","offset":28,"element_size":1,"count":1,"stride":1},)"
        R"({"name":"sync_mode","type":"u8","offset":29,"element_size":1,"count":1,"stride":1},)"
        R"({"name":"gap","type":"u8","offset":30,"element_size":1,"count":1,"stride":1},)"
        R"({"name":"service_flags","type":"u32","offset":32,"element_size":4,"count":1,"stride":4},)"
        R"({"name":"tai_offset_ns","type":"i64","offset":40,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"utc_offset_ns","type":"i64","offset":48,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"utc_unc_ns","type":"u64","offset":56,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"domain_count","type":"u16","offset":64,"element_size":2,"count":1,"stride":2},)"
        R"({"name":"boot_uuid","type":"char[37]","offset":68,"element_size":1,"count":37,"stride":1},)"
        R"({"name":"rows","type":"u8[4096]","offset":128,"element_size":1,"count":4096,"stride":1}])"));
    schemas.push_back(make_schema(
        9, "nexigen.recorder.TimeSnapshot",
        R"([{"name":"generation","type":"u64","offset":0,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"boot_token","type":"u64","offset":8,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"status","type":"u8","offset":16,"element_size":1,"count":1,"stride":1},)"
        R"({"name":"sync_mode","type":"u8","offset":17,"element_size":1,"count":1,"stride":1},)"
        R"({"name":"service_flags","type":"u32","offset":20,"element_size":4,"count":1,"stride":4},)"
        R"({"name":"monotonic_raw_ns","type":"u64","offset":24,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"monotonic_ns","type":"u64","offset":32,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"tai_ns","type":"u64","offset":40,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"realtime_ns","type":"u64","offset":48,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"tai_offset_ns","type":"i64","offset":56,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"utc_offset_ns","type":"i64","offset":64,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"unc_ns","type":"u64","offset":72,"element_size":8,"count":1,"stride":8},)"
        R"({"name":"domain_count","type":"u32","offset":80,"element_size":4,"count":1,"stride":4},)"
        R"({"name":"domains","type":"u8[768]","offset":88,"element_size":1,"count":768,"stride":1},)"
        R"({"name":"reserved2","type":"u8[40]","offset":856,"element_size":1,"count":40,"stride":1}])"));
    return schemas;
}

} // namespace nexigen::recorder
