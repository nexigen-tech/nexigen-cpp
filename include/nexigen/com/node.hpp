// nexigen::com — process membership in the communication fabric (change
// add-com-module; spec com/service-management "Node and daemon-less
// discovery").
//
// Thin wrapper over iox2::Node<ServiceType::Ipc>: one Node per process owns
// the local IPC membership; ports are created through it. Discovery is
// decentralized inside iceoryx2 (no broker, no PID binding) — a publisher may
// start before any subscriber and vice versa.
#pragma once

#include <format>
#include <string>
#include <utility>

#include <iox2/iceoryx2.hpp>

#include <nexigen/com/detail/error_mapping.hpp>
#include <nexigen/com/error.hpp>

namespace nexigen::com {

/// @brief One process's membership in the local IPC fabric.
///
/// Thin wrapper over `iox2::Node<ServiceType::Ipc>` so application code never
/// names an `iox2::` type. Every port (`Publisher`, `Subscriber`, `Client`,
/// `Server`, event and field endpoints) is created through a `Node`, which
/// owns the process's transport membership and must outlive those ports.
///
/// Discovery is decentralized inside the transport: there is no broker and no
/// PID binding, so a publisher may start before any subscriber and vice versa.
///
/// The node owns its transport handle, so copying is disabled and moving
/// transfers ownership. It is not internally synchronized; create ports from
/// one thread or provide external synchronization.
///
/// @note Spec `com/service-management`, requirement "Node and daemon-less
///       discovery" and "Protocol-independent boundary".
class Node {
public:
    /// @brief Create the node on the local IPC transport.
    ///
    /// @return The constructed node, or a mapped `com::Error` (`CreationFailed`,
    ///         `Unknown`, ...) when the transport cannot create it.
    /// @note Spec `com/service-management`, requirement "Node and daemon-less
    ///       discovery".
    [[nodiscard]] static Result<Node> create() noexcept {
        auto created = iox2::NodeBuilder().create<iox2::ServiceType::Ipc>();
        if (!created) {
            return std::unexpected(detail::map_error(created.error()));
        }
        return Node{std::move(*created)};
    }

    /// @brief Copying is disabled: the node owns the transport handle.
    Node(const Node&) = delete;

    /// @brief Copy assignment is disabled: the node owns the transport handle.
    Node& operator=(const Node&) = delete;

    /// @brief Move-construct a node, transferring the transport handle.
    ///
    /// The moved-from source no longer owns a transport handle and must not be
    /// used to create ports.
    Node(Node&&) noexcept = default;

    /// @brief Move-assign a node, transferring the transport handle.
    ///
    /// The destination's previous handle is released; self-assignment is a
    /// no-op.
    ///
    /// The moved-from source is left without a transport handle.
    ///
    /// @return `*this`.
    Node& operator=(Node&&) noexcept = default;

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return `Node(name=<name>)`, using the transport-assigned node name.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    [[nodiscard]] std::string to_string() const {
        return "Node(name=" + std::string(inner_.name().to_string().unchecked_access().c_str()) +
               ")";
    }

    /// @brief The underlying transport handle for port creation.
    ///
    /// @return Mutable reference to the wrapped `iox2::Node`.
    /// @note Module-internal surface only: the port wrappers build services
    ///       through it; business code must stay transport-agnostic (spec
    ///       `com/service-management`, requirement "Protocol-independent
    ///       boundary").
    [[nodiscard]] iox2::Node<iox2::ServiceType::Ipc>& handle() noexcept { return inner_; }

    /// @brief The underlying transport handle for read-only use.
    ///
    /// @return Const reference to the wrapped `iox2::Node`.
    /// @note Module-internal surface only (spec `com/service-management`,
    ///       requirement "Protocol-independent boundary").
    [[nodiscard]] const iox2::Node<iox2::ServiceType::Ipc>& handle() const noexcept {
        return inner_;
    }

private:
    /// @brief Adopt an already-created transport node.
    ///
    /// @param inner Transport node returned by the iceoryx2 builder.
    /// @note Private by design: the only supported construction path is
    ///       `create()`.
    explicit Node(iox2::Node<iox2::ServiceType::Ipc> inner) noexcept : inner_(std::move(inner)) {}

    iox2::Node<iox2::ServiceType::Ipc> inner_; ///< Owned transport handle.
};

/// @brief Free helper mirroring `Node::to_string()`.
///
/// @param node Node to render.
/// @return The diagnostic string.
/// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
[[nodiscard]] inline std::string to_string(const Node& node) {
    return node.to_string();
}

} // namespace nexigen::com

/// @brief Formats a `Node` as its diagnostic string.
///
/// @note Mandate 11 (universal `std::formatter`); delegates to
///       `Node::to_string()`, which allocates, so cold path only.
template <> struct std::formatter<nexigen::com::Node> : std::formatter<std::string_view> {
    /// @brief Write the node's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param node Node to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx> auto format(const nexigen::com::Node& node, Ctx& ctx) const {
        const std::string text = node.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
