// Тесты nexigen::com — харнес как в остальных модулях (NX_CHECK + run_suite,
// zero-dependency). Сборка только при NEXIGEN_ENABLE_COM=ON.
//
// Наборы: error (словарь com::Error), service_name, node, message_concept.
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/com/callback_progression.hpp>
#include <nexigen/com/client.hpp>
#include <nexigen/com/concepts/message_concept.hpp>
#include <nexigen/com/detail/error_mapping.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/event_id.hpp>
#include <nexigen/com/field.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/pending_response.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/sample.hpp>
#include <nexigen/com/sample_mut.hpp>
#include <nexigen/com/server.hpp>
#include <nexigen/com/service_manifest.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/com/wait_set_attachment_id.hpp>
#include <nexigen/com/wait_set_guard.hpp>
#include <nexigen/com/waitset.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace com = nexigen::com;

namespace {

int g_checks = 0;
int g_failures = 0;

#define NX_CHECK(...)                                                                              \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(__VA_ARGS__)) {                                                                      \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
        }                                                                                          \
    } while (false)

void run_suite(const char* name, void (*body)()) {
    const int checks_before = g_checks;
    const int failures_before = g_failures;
    body();
    std::printf("[%-22s] %d checks, %d failed\n", name, g_checks - checks_before,
                g_failures - failures_before);
}

void test_error() {
    // Full conversion set: round-trip every canonical name.
    const com::Error all[] = {
        com::Error::Ok,
        com::Error::ServiceAlreadyExists,
        com::Error::ServiceNotFound,
        com::Error::CreationFailed,
        com::Error::ConnectionFailure,
        com::Error::IncompatibleTypeLayout,
        com::Error::OutOfSamples,
        com::Error::UnableToDeliver,
        com::Error::Timeout,
        com::Error::InvalidArgument,
        com::Error::Unknown,
    };
    for (const com::Error error : all) {
        const std::string_view name = com::to_string(error);
        NX_CHECK(!name.empty());
        com::Error back = com::Error::Ok;
        NX_CHECK(com::from_string(name, back));
        NX_CHECK(back == error);
        nlohmann::json j;
        com::to_json(j, error);
        NX_CHECK(j == std::string(name));
        com::Error parsed = com::Error::Ok;
        com::from_json(j, parsed);
        NX_CHECK(parsed == error);
        NX_CHECK(std::format("{}", error) == std::string(name));
    }

    // Garbage-cast renders Unknown; hostile JSON leaves the target unchanged.
    NX_CHECK(com::to_string(static_cast<com::Error>(0x7f)) == "Unknown");
    com::Error untouched = com::Error::Timeout;
    com::from_json(nlohmann::json{42}, untouched);
    NX_CHECK(untouched == com::Error::Timeout);
    com::from_json(nlohmann::json{"Nope"}, untouched);
    NX_CHECK(untouched == com::Error::Timeout);

    // Result/Status are std::expected over com::Error.
    const com::Result<int> ok_value = 42;
    NX_CHECK(ok_value.has_value() && *ok_value == 42);
    const com::Result<int> failed = std::unexpected(com::Error::Timeout);
    NX_CHECK(!failed.has_value() && failed.error() == com::Error::Timeout);
}

void test_service_name() {
    const auto created = com::ServiceName::create("My/Funk/ServiceName");
    NX_CHECK(created.has_value());
    const com::ServiceName copy = *created;
    NX_CHECK(copy == *created);
    NX_CHECK(copy.to_string() == "My/Funk/ServiceName");
    NX_CHECK(com::to_string(copy) == copy.to_string());
    NX_CHECK(std::format("{}", copy) == copy.to_string());

    // Empty name is an InvalidArgument, not a crash.
    const auto empty = com::ServiceName::create("");
    NX_CHECK(!empty.has_value());
    NX_CHECK(empty.error() == com::Error::InvalidArgument);
}

void test_node() {
    const auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const std::string text = node->to_string();
    NX_CHECK(text.find("Node(") != std::string::npos);
    NX_CHECK(com::to_string(*node) == text);
    NX_CHECK(std::format("{}", *node) == text);
}

struct PodPayload {
    std::int32_t x;
    std::int32_t y;
    double funky;
};

struct NonPodPayload {
    std::string name;
    virtual ~NonPodPayload() = default;
};

struct MethodRequest {
    std::int32_t id;
};

struct MethodResponse {
    std::int32_t value;
};

/// Same size/alignment family as PodPayload but a distinct type name — the
/// iceoryx2 contract check must reject cross-type connections.
struct OtherPayload {
    std::int64_t a;
    std::int64_t b;
    double c;
};

/// Custom header for the trace-propagation round-trip test.
struct TestHeader {
    std::uint64_t value;
    std::uint32_t seq;
};

template <class S, class = void> struct has_user_header : std::false_type {};
template <class S>
struct has_user_header<S, std::void_t<decltype(std::declval<const S&>().user_header())>>
    : std::true_type {};

static_assert(!has_user_header<com::Sample<int>>::value,
              "user_header() must be hidden when H = void");
static_assert(has_user_header<com::Sample<int, TestHeader>>::value,
              "user_header() must exist when H is a custom header");

void test_message_concept() {
    static_assert(com::concepts::Message<PodPayload>);
    static_assert(com::concepts::Message<std::int32_t>);
    static_assert(com::concepts::Message<double>);
    static_assert(!com::concepts::Message<NonPodPayload>);
    static_assert(!com::concepts::Message<std::string>);
    static_assert(!com::concepts::Message<std::vector<int>>);
    NX_CHECK(true); // the concept predicates above are compile-time law
}

void test_event() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/Event");
    NX_CHECK(name.has_value());

    auto notifier = com::EventNotifier::create(*node, *name);
    NX_CHECK(notifier.has_value());
    auto listener = com::EventListener::create(*node, *name);
    NX_CHECK(listener.has_value());

    // Notify (default id + custom id); the listener's blocking wait must wake
    // with the accumulated activations — no polling.
    NX_CHECK(notifier->notify().has_value());
    NX_CHECK(notifier->notify_with(com::EventId{7}).has_value());

    int activations = 0;
    const com::Status waited = listener->blocking_wait([&](com::EventId) { ++activations; });
    NX_CHECK(waited.has_value());
    NX_CHECK(activations >= 1);

    // Diagnostics.
    const std::string ntext = notifier->to_string();
    NX_CHECK(ntext.find("EventNotifier") != std::string::npos);
    NX_CHECK(std::format("{}", *notifier) == ntext);
    const std::string ltext = listener->to_string();
    NX_CHECK(ltext.find("EventListener") != std::string::npos);
    NX_CHECK(std::format("{}", *listener) == ltext);

    const com::EventId id{7};
    NX_CHECK(id.as_value() == 7U);
    NX_CHECK(com::to_string(id) == "EventId(7)");
    NX_CHECK(std::format("{}", id) == "EventId(7)");
}

void test_manifest() {
    com::ServiceManifest manifest;
    manifest.provided_services.push_back({"camera", 4, 1});
    manifest.required_services.push_back({"telemetry", 8, true});

    nlohmann::json j;
    com::to_json(j, manifest);
    com::ServiceManifest back;
    com::from_json(j, back);
    NX_CHECK(back.provided_services.size() == 1U);
    NX_CHECK(back.provided_services[0].service_name == "camera");
    NX_CHECK(back.provided_services[0].max_subscribers == 4U);
    NX_CHECK(back.provided_services[0].history_size == 1U);
    NX_CHECK(back.required_services.size() == 1U);
    NX_CHECK(back.required_services[0].service_name == "telemetry");
    NX_CHECK(back.required_services[0].buffer_capacity == 8U);
    NX_CHECK(back.required_services[0].report_overflow_to_phm);

    const std::string text = manifest.to_string();
    NX_CHECK(text.find("camera") != std::string::npos);
    NX_CHECK(com::to_string(manifest) == text);
    NX_CHECK(std::format("{}", manifest) == text);

    // Hostile input leaves the target unchanged.
    com::from_json(nlohmann::json::array(), manifest);
    NX_CHECK(manifest.provided_services.size() == 1U);
    com::from_json(nlohmann::json{{"provided_services", nlohmann::json::array({42})}}, manifest);
    NX_CHECK(manifest.provided_services.size() == 1U);
}

void test_waitset() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());

    // Multiplexing: attach two listeners, fire only the first — the callback
    // must observe exactly that source.
    {
        const auto name_a = com::ServiceName::create("Nexigen/Test/WsA");
        const auto name_b = com::ServiceName::create("Nexigen/Test/WsB");
        NX_CHECK(name_a.has_value());
        NX_CHECK(name_b.has_value());
        auto listener_a = com::EventListener::create(*node, *name_a);
        auto listener_b = com::EventListener::create(*node, *name_b);
        auto notifier_a = com::EventNotifier::create(*node, *name_a);
        NX_CHECK(listener_a.has_value());
        NX_CHECK(listener_b.has_value());
        NX_CHECK(notifier_a.has_value());

        auto waitset = com::WaitSet::create();
        NX_CHECK(waitset.has_value());
        NX_CHECK(waitset->is_empty());
        auto guard_a = waitset->attach_notification(*listener_a);
        auto guard_b = waitset->attach_notification(*listener_b);
        NX_CHECK(guard_a.has_value());
        NX_CHECK(guard_b.has_value());
        NX_CHECK(waitset->len() == 2U);
        NX_CHECK(!waitset->is_empty());

        NX_CHECK(notifier_a->notify().has_value());
        bool fired_a = false;
        bool fired_b = false;
        const auto status = waitset->wait_and_process_once([&](com::WaitSetAttachmentId id) {
            fired_a = id.has_event_from(*guard_a);
            fired_b = id.has_event_from(*guard_b);
            return com::CallbackProgression::Stop;
        });
        NX_CHECK(status.has_value());
        NX_CHECK(fired_a);
        NX_CHECK(!fired_b);

        // Diagnostics: attachment count in to_string and formatter equality.
        const std::string text = waitset->to_string();
        NX_CHECK(text.find("attachments=2") != std::string::npos);
        NX_CHECK(com::to_string(*waitset) == text);
        NX_CHECK(std::format("{}", *waitset) == text);

        // Dropped guards detach.
        guard_a = std::move(*guard_a); // keep a moved-from no-op via scope
    }
    {
        const auto name = com::ServiceName::create("Nexigen/Test/WsDetach");
        NX_CHECK(name.has_value());
        auto listener = com::EventListener::create(*node, *name);
        NX_CHECK(listener.has_value());
        auto waitset = com::WaitSet::create();
        NX_CHECK(waitset.has_value());
        {
            auto guard = waitset->attach_notification(*listener);
            NX_CHECK(guard.has_value());
            NX_CHECK(waitset->len() == 1U);
        }
        NX_CHECK(waitset->len() == 0U);
        NX_CHECK(waitset->is_empty());
    }

    // Deadline: a listener with a short deadline wakes with has_missed_deadline.
    {
        const auto name = com::ServiceName::create("Nexigen/Test/WsDeadline");
        NX_CHECK(name.has_value());
        auto listener = com::EventListener::create(*node, *name);
        NX_CHECK(listener.has_value());
        auto waitset = com::WaitSet::create();
        NX_CHECK(waitset.has_value());
        auto guard = waitset->attach_deadline(*listener, std::chrono::milliseconds(100));
        NX_CHECK(guard.has_value());
        bool missed = false;
        const auto status = waitset->wait_and_process_once([&](com::WaitSetAttachmentId id) {
            missed = id.has_missed_deadline(*guard);
            return com::CallbackProgression::Stop;
        });
        NX_CHECK(status.has_value());
        NX_CHECK(missed);
    }

    // Interval: a short periodic attachment wakes with no notifier event.
    {
        auto waitset = com::WaitSet::create();
        NX_CHECK(waitset.has_value());
        auto guard = waitset->attach_interval(std::chrono::milliseconds(100));
        NX_CHECK(guard.has_value());
        bool woken = false;
        const auto status = waitset->wait_and_process_once([&](com::WaitSetAttachmentId) {
            woken = true;
            return com::CallbackProgression::Stop;
        });
        NX_CHECK(status.has_value());
        NX_CHECK(woken);
    }

    // Callback progression: Stop ends wait_and_process after the first source.
    {
        const auto name = com::ServiceName::create("Nexigen/Test/WsStop");
        NX_CHECK(name.has_value());
        auto listener = com::EventListener::create(*node, *name);
        auto notifier = com::EventNotifier::create(*node, *name);
        NX_CHECK(listener.has_value());
        NX_CHECK(notifier.has_value());
        auto waitset = com::WaitSet::create();
        NX_CHECK(waitset.has_value());
        auto guard = waitset->attach_notification(*listener);
        NX_CHECK(guard.has_value());
        NX_CHECK(notifier->notify().has_value());
        int calls = 0;
        const auto status = waitset->wait_and_process([&](com::WaitSetAttachmentId) {
            ++calls;
            return com::CallbackProgression::Stop;
        });
        NX_CHECK(status.has_value());
        NX_CHECK(calls == 1);
    }

    // Empty wait set: the transport reports NoAttachments immediately rather
    // than spinning or blocking for the full timeout.
    {
        auto waitset = com::WaitSet::create();
        NX_CHECK(waitset.has_value());
        const auto start = std::chrono::steady_clock::now();
        const auto status = waitset->wait_and_process_once_with_timeout(
            [](com::WaitSetAttachmentId) { return com::CallbackProgression::Stop; },
            std::chrono::milliseconds(1000));
        const auto elapsed = std::chrono::steady_clock::now() - start;
        NX_CHECK(!status.has_value());
        NX_CHECK(status.error() == com::Error::InvalidArgument);
        NX_CHECK(elapsed < std::chrono::milliseconds(500));
    }

    // CallbackProgression conversion set round-trips.
    {
        for (const com::CallbackProgression value :
             {com::CallbackProgression::Continue, com::CallbackProgression::Stop}) {
            const std::string_view name = com::to_string(value);
            NX_CHECK(!name.empty());
            com::CallbackProgression back = com::CallbackProgression::Continue;
            NX_CHECK(com::from_string(name, back));
            NX_CHECK(back == value);
            nlohmann::json j;
            com::to_json(j, value);
            NX_CHECK(j == std::string(name));
            com::CallbackProgression parsed = com::CallbackProgression::Continue;
            com::from_json(j, parsed);
            NX_CHECK(parsed == value);
            NX_CHECK(std::format("{}", value) == std::string(name));
        }
        NX_CHECK(com::to_string(static_cast<com::CallbackProgression>(0x7f)) == "Unknown");
    }

    // Error mapping unit cases (task 3.1): WaitSet error enums collapse onto
    // the com::Error vocabulary per design D3.
    {
        NX_CHECK(com::detail::map_error(iox2::WaitSetAttachmentError::AlreadyAttached) ==
                 com::Error::InvalidArgument);
        NX_CHECK(com::detail::map_error(iox2::WaitSetAttachmentError::InsufficientCapacity) ==
                 com::Error::OutOfSamples);
        NX_CHECK(com::detail::map_error(iox2::WaitSetRunError::NoAttachments) ==
                 com::Error::InvalidArgument);
        NX_CHECK(com::detail::map_error(iox2::WaitSetRunError::TerminationRequest) ==
                 com::Error::Ok);
        NX_CHECK(com::detail::map_error(iox2::WaitSetRunError::Interrupt) == com::Error::Ok);
    }
}

void test_field() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/Field");
    NX_CHECK(name.has_value());

    auto field = com::Field<PodPayload>::create(*node, *name, PodPayload{5, 0, 0.0});
    NX_CHECK(field.has_value());
    NX_CHECK(field->get().x == 5);

    // Set is visible to get: writer reads its own last value.
    NX_CHECK(field->set(PodPayload{6, 1, 0.5}).has_value());
    NX_CHECK(field->get().x == 6);

    // Last value on connect: a listener that opens after the update reads the
    // current value immediately (history >= 1).
    auto listener = com::FieldListener<PodPayload>::open(*node, *name);
    NX_CHECK(listener.has_value());
    NX_CHECK(listener->get().x == 6);

    // Update notifies listeners: the blocked wait wakes without polling.
    com::Status wait_status = std::unexpected(com::Error::Unknown);
    std::thread waiter([&] { wait_status = listener->wait_for_update(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    NX_CHECK(field->set(PodPayload{7, 2, 1.5}).has_value());
    waiter.join();
    NX_CHECK(wait_status.has_value());
    NX_CHECK(listener->get().x == 7);
    NX_CHECK(field->get().x == 7);

    // Diagnostics: service name and the last-update sequence.
    const std::string ftext = field->to_string();
    NX_CHECK(ftext.find("Nexigen/Test/Field") != std::string::npos);
    NX_CHECK(ftext.find("updates=2") != std::string::npos);
    NX_CHECK(com::to_string(*field) == ftext);
    NX_CHECK(std::format("{}", *field) == ftext);
    const std::string ltext = listener->to_string();
    NX_CHECK(ltext.find("FieldListener") != std::string::npos);
    NX_CHECK(com::to_string(*listener) == ltext);
    NX_CHECK(std::format("{}", *listener) == ltext);
}

/// Synchronous round-trip: the server receives the request bytes and the client
/// harvests the answer.
void test_method_roundtrip() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());

    // Round-trip: the server receives the exact request bytes and its answer
    // reaches the waiting client (transport-level correlation).
    {
        const auto name = com::ServiceName::create("Nexigen/Test/Method");
        NX_CHECK(name.has_value());
        auto client = com::Client<MethodRequest, MethodResponse>::create(*node, *name);
        NX_CHECK(client.has_value());
        auto server = com::Server<MethodRequest, MethodResponse>::create(*node, *name);
        NX_CHECK(server.has_value());

        // Synchronous call: a background server answers in time.
        std::thread server_thread([&server] {
            for (int i = 0; i < 200; ++i) {
                auto request = server->receive();
                if (request.has_value() && request->has_value()) {
                    NX_CHECK((*request)->payload().id == 1);
                    NX_CHECK((*request)->send_copy(MethodResponse{42}).has_value());
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
        const auto response = client->call(MethodRequest{1}, std::chrono::milliseconds(2000));
        server_thread.join();
        NX_CHECK(response.has_value());
        NX_CHECK(response->value == 42);

        // Pending count returns to zero once the call completed.
        NX_CHECK(client->pending_count() == 0U);
        const std::string ctext = client->to_string();
        NX_CHECK(ctext.find("Nexigen/Test/Method") != std::string::npos);
        NX_CHECK(ctext.find("pending=0") != std::string::npos);
        NX_CHECK(com::to_string(*client) == ctext);
        NX_CHECK(std::format("{}", *client) == ctext);
    }
}

/// Out-of-order correlation: each pending handle harvests only its own
/// response and carries its own correlation id.
void test_method_out_of_order() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());

    // Out-of-order correlation: two outstanding requests, the server answers
    // in reverse order; each pending handle harvests only its own response
    // and carries its own correlation id.
    {
        const auto name = com::ServiceName::create("Nexigen/Test/MethodOO");
        NX_CHECK(name.has_value());
        auto client = com::Client<MethodRequest, MethodResponse>::create(*node, *name);
        NX_CHECK(client.has_value());
        auto server = com::Server<MethodRequest, MethodResponse>::create(*node, *name);
        NX_CHECK(server.has_value());

        auto pending_a = client->send(MethodRequest{100});
        NX_CHECK(pending_a.has_value());
        auto pending_b = client->send(MethodRequest{200});
        NX_CHECK(pending_b.has_value());
        NX_CHECK(client->pending_count() == 2U);
        NX_CHECK(pending_a->correlation_id() != pending_b->correlation_id());

        // Server harvests both requests and answers out of order: B first.
        std::optional<com::ServerRequest<MethodRequest, MethodResponse>> request_a;
        std::optional<com::ServerRequest<MethodRequest, MethodResponse>> request_b;
        for (int i = 0; i < 500 && !request_a.has_value(); ++i) {
            auto got = server->receive();
            NX_CHECK(got.has_value());
            if (got->has_value()) {
                NX_CHECK(!request_a.has_value());
                request_a.emplace(std::move(**got));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        NX_CHECK(request_a.has_value());
        for (int i = 0; i < 500 && !request_b.has_value(); ++i) {
            auto got = server->receive();
            NX_CHECK(got.has_value());
            if (got->has_value()) {
                NX_CHECK(!request_b.has_value());
                request_b.emplace(std::move(**got));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        NX_CHECK(request_b.has_value());
        NX_CHECK(request_a->payload().id == 100);
        NX_CHECK(request_b->payload().id == 200);

        // Answer in reverse order: B gets 200, A gets 100.
        NX_CHECK(request_b->send_copy(MethodResponse{200}).has_value());
        NX_CHECK(request_a->send_copy(MethodResponse{100}).has_value());

        std::optional<MethodResponse> response_a;
        for (int i = 0; i < 500 && !response_a.has_value(); ++i) {
            const auto got = pending_a->receive();
            NX_CHECK(got.has_value());
            if (got->has_value()) {
                response_a = **got;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        NX_CHECK(response_a.has_value());
        NX_CHECK(response_a->value == 100);
        std::optional<MethodResponse> response_b;
        for (int i = 0; i < 500 && !response_b.has_value(); ++i) {
            const auto got = pending_b->receive();
            NX_CHECK(got.has_value());
            if (got->has_value()) {
                response_b = **got;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        NX_CHECK(response_b.has_value());
        NX_CHECK(response_b->value == 200);

        // Every response was delivered through its own pending handle.
        NX_CHECK(pending_a->to_string().find("PendingResponse(correlation_id=") !=
                 std::string::npos);
        NX_CHECK(std::format("{}", *pending_a) == pending_a->to_string());

        // Server diagnostics: pending drains to zero after both answers.
        NX_CHECK(server->pending_count() == 0U);
        const std::string stext = server->to_string();
        NX_CHECK(stext.find("Nexigen/Test/MethodOO") != std::string::npos);
        NX_CHECK(stext.find("pending=0") != std::string::npos);
        NX_CHECK(std::format("{}", *server) == stext);
    }
}

/// Timeout: a server exists but never answers; call() reports Timeout and
/// abandons the request.
void test_method_timeout() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());

    // Timeout: a server exists but never answers; call() reports Timeout and
    // abandons the request (the pending handle is dropped — no response is
    // delivered afterwards).
    {
        const auto name = com::ServiceName::create("Nexigen/Test/MethodTimeout");
        NX_CHECK(name.has_value());
        auto client = com::Client<MethodRequest, MethodResponse>::create(*node, *name);
        NX_CHECK(client.has_value());
        auto server = com::Server<MethodRequest, MethodResponse>::create(*node, *name);
        NX_CHECK(server.has_value());

        const auto response = client->call(MethodRequest{9}, std::chrono::milliseconds(120));
        NX_CHECK(!response.has_value());
        NX_CHECK(response.error() == com::Error::Timeout);
        NX_CHECK(client->pending_count() == 0U);
    }
}

/// Request-response method channel (spec com/request-response).
void test_method() {
    test_method_roundtrip();
    test_method_out_of_order();
    test_method_timeout();
}

void test_pub_sub_roundtrip() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/RoundTrip");
    NX_CHECK(name.has_value());

    auto publisher = com::Publisher<PodPayload>::create(*node, *name);
    NX_CHECK(publisher.has_value());
    auto subscriber = com::Subscriber<PodPayload>::create(*node, *name);
    NX_CHECK(subscriber.has_value());

    // Zero-copy loan → in-place write → send.
    auto loaned = publisher->loan_uninit();
    NX_CHECK(loaned.has_value());
    auto sample = loaned->write_payload(PodPayload{7, 14, 2.5});
    NX_CHECK(publisher->send(std::move(sample)).has_value());

    // The loaned slot is returned to the pool when the loan is dropped, and a
    // second send reuses it.
    {
        auto again = publisher->loan_uninit();
        NX_CHECK(again.has_value());
        auto second = again->write_payload(PodPayload{8, 16, 3.0});
        NX_CHECK(publisher->send(std::move(second)).has_value());
    }

    // Receive both (connection establishment may lag the first send).
    std::int32_t received_x[2] = {0, 0};
    int received = 0;
    for (int i = 0; i < 500 && received < 2; ++i) {
        const auto got = subscriber->receive();
        NX_CHECK(got.has_value());
        if (got->has_value()) {
            NX_CHECK(received < 2);
            received_x[received++] = (*got)->payload().x;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    NX_CHECK(received == 2);
    NX_CHECK((received_x[0] == 7 && received_x[1] == 8) ||
             (received_x[0] == 8 && received_x[1] == 7));

    // Diagnostics: member == free == formatter; telemetry send counter ticked.
    const std::string ptext = publisher->to_string();
    NX_CHECK(ptext.find("RoundTrip") != std::string::npos);
    NX_CHECK(ptext.find("sent=2") != std::string::npos);
    NX_CHECK(com::to_string(*publisher) == ptext);
    NX_CHECK(std::format("{}", *publisher) == ptext);
    const std::string stext = subscriber->to_string();
    NX_CHECK(stext.find("RoundTrip") != std::string::npos);
    NX_CHECK(std::format("{}", *subscriber) == stext);
}

void test_pub_sub_overflow() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/Overflow");
    NX_CHECK(name.has_value());

    auto publisher = com::Publisher<PodPayload>::create(*node, *name);
    NX_CHECK(publisher.has_value());
    // Receiver buffer of 2: the subscriber stops reading after the first two.
    auto subscriber = com::Subscriber<PodPayload>::create(*node, *name, 2);
    NX_CHECK(subscriber.has_value());

    // A slow subscriber cannot block the publisher: a full queue discards per
    // the DiscardData policy and every send completes immediately. 100 sends
    // would hang forever under a retry policy, so the run completes at all.
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
        auto loaned = publisher->loan_uninit();
        NX_CHECK(loaned.has_value());
        auto sample = loaned->write_payload(PodPayload{i, 0, 0.0});
        NX_CHECK(publisher->send(std::move(sample)).has_value());
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    NX_CHECK(elapsed < std::chrono::seconds(5));

    // Fill and loss counters ticked: sent=100, queued (fill events) and lost
    // (evicted samples) each equal 98 — everything past the 2-slot buffer.
    const std::string ptext = publisher->to_string();
    NX_CHECK(ptext.find("Nexigen/Test/Overflow") != std::string::npos);
    NX_CHECK(ptext.find("sent=100") != std::string::npos);
    NX_CHECK(ptext.find("queued=98") != std::string::npos);
    NX_CHECK(ptext.find("lost=98") != std::string::npos);
    NX_CHECK(std::format("{}", *publisher) == ptext);

    // The subscriber still holds exactly its buffer: the first two samples
    // that were delivered before the queue filled up.
    int received = 0;
    int received_x[2] = {0, 0};
    while (true) {
        const auto got = subscriber->receive();
        NX_CHECK(got.has_value());
        if (got->has_value()) {
            NX_CHECK(received < 2);
            received_x[received++] = (*got)->payload().x;
        } else {
            break;
        }
    }
    NX_CHECK(received == 2);
    NX_CHECK(received_x[0] == 0);
    NX_CHECK(received_x[1] == 1);
}

void test_pub_sub_type_mismatch() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/Mismatch");
    NX_CHECK(name.has_value());

    // The service is created with PodPayload as its payload contract.
    auto publisher = com::Publisher<PodPayload>::create(*node, *name);
    NX_CHECK(publisher.has_value());

    // Connecting a port of a different payload type must report the layout
    // mismatch, not silently exchange bytes.
    auto bad_subscriber = com::Subscriber<OtherPayload>::create(*node, *name);
    NX_CHECK(!bad_subscriber.has_value());
    NX_CHECK(bad_subscriber.error() == com::Error::IncompatibleTypeLayout);
}

void test_pub_sub_two_process() { // The parent process is the subscriber, the forked child is the
    // publisher — a real two-process loan-send-receive round-trip over the
    // shared-memory transport.
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/TwoProcess");
    NX_CHECK(name.has_value());

    auto subscriber = com::Subscriber<PodPayload>::create(*node, *name);
    NX_CHECK(subscriber.has_value());

    const pid_t child = fork();
    NX_CHECK(child >= 0);
    if (child == 0) {
        // Child: its own Node and publisher; _exit skips parent teardown.
        auto child_node = com::Node::create();
        auto publisher = com::Publisher<PodPayload>::create(*child_node, *name);
        for (int i = 1; i <= 3; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            auto loaned = publisher->loan_uninit();
            auto sample = loaned->write_payload(PodPayload{40 + i, 0, 0.0});
            if (!publisher->send(std::move(sample)).has_value()) {
                _exit(1);
            }
        }
        _exit(0);
    }

    std::int32_t received_x[3] = {0, 0, 0};
    int received = 0;
    for (int i = 0; i < 1000 && received < 3; ++i) {
        const auto got = subscriber->receive();
        NX_CHECK(got.has_value());
        if (got->has_value()) {
            NX_CHECK(received < 3);
            received_x[received++] = (*got)->payload().x;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    int status = 0;
    waitpid(child, &status, 0);
    NX_CHECK(WIFEXITED(status));
    NX_CHECK(WEXITSTATUS(status) == 0);

    NX_CHECK(received == 3);
    NX_CHECK(received_x[0] == 41);
    NX_CHECK(received_x[1] == 42);
    NX_CHECK(received_x[2] == 43);
}

void test_custom_header() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/CustomHeader");
    NX_CHECK(name.has_value());

    auto publisher = com::Publisher<int, TestHeader>::create(*node, *name);
    NX_CHECK(publisher.has_value());
    auto subscriber = com::Subscriber<int, TestHeader>::create(*node, *name);
    NX_CHECK(subscriber.has_value());

    auto loaned = publisher->loan_uninit();
    NX_CHECK(loaned.has_value());
    auto sample = loaned->write_payload(42);
    sample.user_header_mut() = TestHeader{0xCAFEULL, 5};
    NX_CHECK(publisher->send(std::move(sample)).has_value());

    bool received = false;
    for (int i = 0; i < 500 && !received; ++i) {
        const auto got = subscriber->receive();
        NX_CHECK(got.has_value());
        if (got->has_value()) {
            NX_CHECK((*got)->payload() == 42);
            const TestHeader& header = (*got)->user_header();
            NX_CHECK(header.value == 0xCAFEULL);
            NX_CHECK(header.seq == 5U);
            received = true;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    NX_CHECK(received);
}

} // namespace

int main() {
    run_suite("error", test_error);
    run_suite("service_name", test_service_name);
    run_suite("node", test_node);
    run_suite("message_concept", test_message_concept);
    run_suite("pub_sub_roundtrip", test_pub_sub_roundtrip);
    run_suite("pub_sub_overflow", test_pub_sub_overflow);
    run_suite("pub_sub_type_mismatch", test_pub_sub_type_mismatch);
    run_suite("pub_sub_two_process", test_pub_sub_two_process);
    run_suite("custom_header", test_custom_header);
    run_suite("event", test_event);
    run_suite("waitset", test_waitset);
    run_suite("field", test_field);
    run_suite("method", test_method);
    run_suite("manifest", test_manifest);
    std::printf("TOTAL %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
