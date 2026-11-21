// nexigen::com — raw introspection subscriber implementation (iceoryx2 C API).
#include <nexigen/com/raw_subscriber.hpp>

#include <cstring>
#include <utility>

#include <iox2/iceoryx2.h>

namespace nexigen::com {
namespace {

[[nodiscard]] const char* c_name(const ServiceName& name, std::string& storage) {
    storage = name.to_string();
    return storage.c_str();
}

} // namespace

Result<RawSubscriber> RawSubscriber::create(const ServiceName& name, std::uint64_t buffer_size) {
    RawSubscriber result;

    iox2_config_t config_storage{};
    iox2_config_h config = nullptr;
    if (iox2_config_default(&config_storage, &config) != IOX2_OK) {
        return std::unexpected(Error::CreationFailed);
    }

    std::string name_storage;
    const char* name_text = c_name(name, name_storage);
    iox2_service_name_h service_name = nullptr;
    if (iox2_service_name_new(nullptr, name_text, std::strlen(name_text), &service_name) !=
        IOX2_OK) {
        iox2_config_drop(config);
        return std::unexpected(Error::CreationFailed);
    }
    result.service_name_ = service_name;

    iox2_static_config_t details{};
    bool exists = false;
    const int detail_status =
        iox2_service_details(iox2_service_type_e_IPC, iox2_cast_service_name_ptr(service_name),
                             iox2_cast_config_ptr(config),
                             iox2_messaging_pattern_e_PUBLISH_SUBSCRIBE, &details, &exists);
    iox2_config_drop(config);
    if (detail_status != IOX2_OK || !exists) {
        result.release();
        return std::unexpected(Error::ServiceNotFound);
    }

    const auto& types = details.details.publish_subscribe.message_type_details;
    if (types.payload.variant != iox2_type_variant_e_FIXED_SIZE || types.payload.size == 0U) {
        result.release();
        return std::unexpected(Error::IncompatibleTypeLayout);
    }
    result.payload_size_ = types.payload.size;
    result.user_header_size_ = types.user_header.size;
    result.payload_type_ = std::string(types.payload.type_name,
                                       strnlen(types.payload.type_name, IOX2_TYPE_NAME_LENGTH));

    iox2_node_builder_h node_builder = iox2_node_builder_new(nullptr);
    iox2_node_h node = nullptr;
    if (iox2_node_builder_create(node_builder, nullptr, iox2_service_type_e_IPC, &node) !=
        IOX2_OK) {
        result.release();
        return std::unexpected(Error::CreationFailed);
    }
    result.node_ = node;

    iox2_service_builder_h builder =
        iox2_node_service_builder(&node, nullptr, iox2_cast_service_name_ptr(service_name));
    iox2_service_builder_pub_sub_h pub_sub = iox2_service_builder_pub_sub(builder);

    if (iox2_service_builder_pub_sub_set_payload_type_details(
            &pub_sub, types.payload.variant, types.payload.type_name,
            strnlen(types.payload.type_name, IOX2_TYPE_NAME_LENGTH), types.payload.size,
            types.payload.alignment) != IOX2_OK) {
        result.release();
        return std::unexpected(Error::IncompatibleTypeLayout);
    }
    if (types.user_header.size > 0U &&
        iox2_service_builder_pub_sub_set_user_header_type_details(
            &pub_sub, types.user_header.variant, types.user_header.type_name,
            strnlen(types.user_header.type_name, IOX2_TYPE_NAME_LENGTH), types.user_header.size,
            types.user_header.alignment) != IOX2_OK) {
        result.release();
        return std::unexpected(Error::IncompatibleTypeLayout);
    }

    iox2_port_factory_pub_sub_h service = nullptr;
    if (iox2_service_builder_pub_sub_open_or_create(pub_sub, nullptr, &service) != IOX2_OK) {
        result.release();
        return std::unexpected(Error::CreationFailed);
    }
    result.service_ = service;

    iox2_port_factory_subscriber_builder_h subscriber_builder =
        iox2_port_factory_pub_sub_subscriber_builder(&service, nullptr);
    if (buffer_size > 0U) {
        iox2_port_factory_subscriber_builder_set_buffer_size(&subscriber_builder, buffer_size);
    }
    iox2_subscriber_h subscriber = nullptr;
    if (iox2_port_factory_subscriber_builder_create(subscriber_builder, nullptr, &subscriber) !=
        IOX2_OK) {
        result.release();
        return std::unexpected(Error::CreationFailed);
    }
    result.subscriber_ = subscriber;
    return result;
}

Result<std::optional<RawSubscriber::Sample>> RawSubscriber::receive() noexcept {
    if (sample_ != nullptr) {
        iox2_sample_drop(static_cast<iox2_sample_h>(sample_));
        sample_ = nullptr;
    }
    iox2_sample_h sample = nullptr;
    iox2_subscriber_h subscriber = static_cast<iox2_subscriber_h>(subscriber_);
    if (iox2_subscriber_receive(&subscriber, nullptr, &sample) != IOX2_OK) {
        return std::unexpected(Error::ConnectionFailure);
    }
    if (sample == nullptr) {
        return std::nullopt;
    }
    sample_ = sample;

    const void* payload = nullptr;
    std::size_t payload_elements = 0;
    iox2_sample_payload(&sample, &payload, &payload_elements);
    static_cast<void>(payload_elements);
    const std::size_t payload_bytes =
        iox2_sample_payload_number_of_bytes(&sample);
    const void* user_header = nullptr;
    iox2_sample_user_header(&sample, &user_header);

    Sample view{};
    view.payload =
        std::span<const std::byte>(static_cast<const std::byte*>(payload),
                                   payload_bytes < payload_size_ ? payload_bytes : payload_size_);
    if (user_header != nullptr && user_header_size_ > 0U) {
        view.user_header = std::span<const std::byte>(static_cast<const std::byte*>(user_header),
                                                      user_header_size_);
    }
    view.payload_type = payload_type_;
    return view;
}

RawSubscriber::RawSubscriber(RawSubscriber&& other) noexcept
    : node_(std::exchange(other.node_, nullptr)),
      service_name_(std::exchange(other.service_name_, nullptr)),
      service_(std::exchange(other.service_, nullptr)),
      subscriber_(std::exchange(other.subscriber_, nullptr)),
      sample_(std::exchange(other.sample_, nullptr)), payload_size_(other.payload_size_),
      user_header_size_(other.user_header_size_), payload_type_(std::move(other.payload_type_)) {}

RawSubscriber& RawSubscriber::operator=(RawSubscriber&& other) noexcept {
    if (this != &other) {
        release();
        node_ = std::exchange(other.node_, nullptr);
        service_name_ = std::exchange(other.service_name_, nullptr);
        service_ = std::exchange(other.service_, nullptr);
        subscriber_ = std::exchange(other.subscriber_, nullptr);
        sample_ = std::exchange(other.sample_, nullptr);
        payload_size_ = other.payload_size_;
        user_header_size_ = other.user_header_size_;
        payload_type_ = std::move(other.payload_type_);
    }
    return *this;
}

RawSubscriber::~RawSubscriber() {
    release();
}

void RawSubscriber::release() noexcept {
    if (sample_ != nullptr) {
        iox2_sample_drop(static_cast<iox2_sample_h>(sample_));
        sample_ = nullptr;
    }
    if (subscriber_ != nullptr) {
        iox2_subscriber_drop(static_cast<iox2_subscriber_h>(subscriber_));
        subscriber_ = nullptr;
    }
    if (service_ != nullptr) {
        iox2_port_factory_pub_sub_drop(static_cast<iox2_port_factory_pub_sub_h>(service_));
        service_ = nullptr;
    }
    if (service_name_ != nullptr) {
        iox2_service_name_drop(static_cast<iox2_service_name_h>(service_name_));
        service_name_ = nullptr;
    }
    if (node_ != nullptr) {
        iox2_node_drop(static_cast<iox2_node_h>(node_));
        node_ = nullptr;
    }
}

} // namespace nexigen::com
