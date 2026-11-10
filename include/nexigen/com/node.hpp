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

/// One process's membership in the local IPC fabric.
class Node {
public:
    /// Create the node on the local IPC transport; failure maps to com::Error.
    [[nodiscard]] static Result<Node> create() noexcept {
        auto created = iox2::NodeBuilder().create<iox2::ServiceType::Ipc>();
        if (!created) {
            return std::unexpected(detail::map_error(created.error()));
        }
        return Node{std::move(*created)};
    }

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&) noexcept = default;
    Node& operator=(Node&&) noexcept = default;

    /// Cold-path diagnostic (mandate 9): the node name.
    [[nodiscard]] std::string to_string() const {
        return "Node(name=" + std::string(inner_.name().to_string().unchecked_access().c_str()) + ")";
    }

    /// The underlying transport handle (module-internal surface only; the
    /// port wrappers build services through it).
    [[nodiscard]] iox2::Node<iox2::ServiceType::Ipc>& handle() noexcept { return inner_; }
    [[nodiscard]] const iox2::Node<iox2::ServiceType::Ipc>& handle() const noexcept {
        return inner_;
    }

private:
    explicit Node(iox2::Node<iox2::ServiceType::Ipc> inner) noexcept : inner_(std::move(inner)) {}

    iox2::Node<iox2::ServiceType::Ipc> inner_;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const Node& node) {
    return node.to_string();
}

} // namespace nexigen::com

/// The node formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::com::Node> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::com::Node& node, Ctx& ctx) const {
        const std::string text = node.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
