// Split from tracing_test.cpp: nexigen::tracing span enums.

#include "tracing_test_support.hpp"

namespace {

void test_span_enums() {
    using tracing::SpanKind;
    using tracing::SpanStatus;
    using tracing::SamplingDecision;

    // Every enumerator renders its exact spelling and parses back.
    for (const auto& [value, name] :
         std::array<std::pair<SpanKind, std::string_view>, 5>{{{SpanKind::Internal, "Internal"},
                                                               {SpanKind::Server, "Server"},
                                                               {SpanKind::Client, "Client"},
                                                               {SpanKind::Producer, "Producer"},
                                                               {SpanKind::Consumer, "Consumer"}}}) {
        NX_CHECK(tracing::to_string(value) == name);
        SpanKind parsed = SpanKind::Internal;
        NX_CHECK(tracing::from_string(name, parsed));
        NX_CHECK(parsed == value);
    }
    for (const auto& [value, name] :
         std::array<std::pair<SpanStatus, std::string_view>, 3>{{{SpanStatus::Unset, "Unset"},
                                                                 {SpanStatus::Ok, "Ok"},
                                                                 {SpanStatus::Error, "Error"}}}) {
        NX_CHECK(tracing::to_string(value) == name);
        SpanStatus parsed = SpanStatus::Unset;
        NX_CHECK(tracing::from_string(name, parsed));
        NX_CHECK(parsed == value);
    }
    for (const auto& [value, name] : std::array<std::pair<SamplingDecision, std::string_view>, 2>{
             {{SamplingDecision::Drop, "Drop"},
              {SamplingDecision::RecordAndSample, "RecordAndSample"}}}) {
        NX_CHECK(tracing::to_string(value) == name);
        SamplingDecision parsed = SamplingDecision::Drop;
        NX_CHECK(tracing::from_string(name, parsed));
        NX_CHECK(parsed == value);
    }

    // Failed parses leave out-params unchanged (spec scenario: "ok"/empty).
    SpanStatus pre = SpanStatus::Unset;
    NX_CHECK(!tracing::from_string("ok", pre));
    NX_CHECK(pre == SpanStatus::Unset);
    SamplingDecision pre_drop = SamplingDecision::Drop;
    NX_CHECK(!tracing::from_string("", pre_drop));
    NX_CHECK(pre_drop == SamplingDecision::Drop);
    SpanKind pre_kind = SpanKind::Server;
    NX_CHECK(!tracing::from_string("server", pre_kind));
    NX_CHECK(pre_kind == SpanKind::Server);

    // Garbage casts render "Unknown" (static storage, no throw).
    NX_CHECK(tracing::to_string(static_cast<SpanKind>(99)) == "Unknown");
    NX_CHECK(tracing::to_string(static_cast<SpanStatus>(250)) == "Unknown");
    NX_CHECK(tracing::to_string(static_cast<SamplingDecision>(7)) == "Unknown");

    // --- task 2.2: JSON round-trips through nlohmann under -fno-exceptions.
    {
        const nlohmann::json j_kind = SpanKind::Client;
        NX_CHECK(j_kind.is_string() && j_kind.get<std::string>() == "Client");
        SpanKind back = SpanKind::Internal;
        nlohmann::from_json(j_kind, back); // ADL via j.get would work too
        NX_CHECK(back == SpanKind::Client);

        const nlohmann::json j_status = SpanStatus::Error;
        NX_CHECK(j_status.get<std::string>() == "Error");
        SpanStatus status_back = SpanStatus::Unset;
        nlohmann::from_json(j_status, status_back);
        NX_CHECK(status_back == SpanStatus::Error);

        const nlohmann::json j_sampling = SamplingDecision::RecordAndSample;
        NX_CHECK(j_sampling.get<std::string>() == "RecordAndSample");
        SamplingDecision sampling_back = SamplingDecision::Drop;
        nlohmann::from_json(j_sampling, sampling_back);
        NX_CHECK(sampling_back == SamplingDecision::RecordAndSample);
    }

    // Hostile JSON: numbers/objects/arrays/null/unknown strings on a
    // pre-initialized value — returns normally, value still Unset.
    {
        const std::array<nlohmann::json, 6> hostile{
            nlohmann::json(42),      nlohmann::json(-1),      nlohmann::json{{"a", "b"}},
            nlohmann::json{1, 2, 3}, nlohmann::json(nullptr), nlohmann::json("NotAnEnum")};
        for (const nlohmann::json& bad : hostile) {
            SpanStatus out = SpanStatus::Unset;
            nlohmann::from_json(bad, out);
            NX_CHECK(out == SpanStatus::Unset);
        }
        for (const nlohmann::json& bad : hostile) {
            SpanKind out = SpanKind::Server;
            nlohmann::from_json(bad, out);
            NX_CHECK(out == SpanKind::Server);
        }
        for (const nlohmann::json& bad : hostile) {
            SamplingDecision out = SamplingDecision::RecordAndSample;
            nlohmann::from_json(bad, out);
            NX_CHECK(out == SamplingDecision::RecordAndSample);
        }
    }

    // std::format renders canonical names; width/fill/alignment honored.
    NX_CHECK(std::format("{}", SpanKind::Server) == "Server");
    NX_CHECK(std::format("{}", SpanStatus::Error) == "Error");
    NX_CHECK(std::format("{}", SamplingDecision::Drop) == "Drop");
    NX_CHECK(std::format("{:*>10}", SpanKind::Client) == "****Client");
    NX_CHECK(std::format("{:<8}|", SpanStatus::Ok) == "Ok      |");
    NX_CHECK(std::format("{:>14}", SamplingDecision::Drop) == "          Drop");
    NX_CHECK(std::format("{:^10}", SpanKind::Internal) == " Internal ");
    // Width exactly equal to the name: no padding.
    NX_CHECK(std::format("{:6}", SpanStatus::Unset) == "Unset ");
}

} // namespace

void suite_span_enums() {
    run_suite("span_enums", test_span_enums);
}
