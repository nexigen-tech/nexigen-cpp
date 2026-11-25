// nexigen-canview — SDO expedited and segmented decoding.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <nexigen/hw/can/can_message.hpp>

#include "co_decoded.hpp"

namespace nexigen::canview {

class EdsDictionary;

/// @brief One successful SDO write observed on the bus.
struct SdoCompletedWrite {
    std::uint16_t index{0};                ///< Object index.
    std::uint8_t subindex{0};              ///< Object subindex.
    std::array<std::uint8_t, 4096> data{}; ///< Written payload.
    std::size_t size{0};                   ///< Payload byte count.
};

/// @brief Decodes SDO traffic and assembles segmented transfers.
///
/// One assembler and one pending write exist per node, so interleaved node
/// transfers never mix. Writes are reported only after the server response.
class SdoDecoder {
public:
    /// @brief Assembly buffer size.
    static constexpr std::size_t max_transfer_bytes{4096U};
    /// @brief Segmented-transfer timeout.
    static constexpr std::uint64_t timeout_ns{1'000'000'000ULL};

    /// @brief Consume one SDO frame.
    /// @param message Frame.
    /// @param response Whether the frame came from `0x580 + node`.
    /// @param out Decoded output.
    /// @param now_ns Host monotonic time.
    /// @return `true` when the frame looked like SDO traffic.
    bool feed(const nexigen::hw::can::CanMessage& message, bool response, CoDecoded& out,
              std::uint64_t now_ns, const EdsDictionary* dictionary = nullptr);

    /// @brief Take the next completed write.
    /// @return Completed write or `std::nullopt`.
    std::optional<SdoCompletedWrite> take_completed();

    /// @brief Drop assemblers older than the timeout.
    /// @param now_ns Host monotonic time.
    void expire(std::uint64_t now_ns) noexcept;

    /// @brief Drop every assembler, pending write and completed write.
    void reset() noexcept;

    /// @brief Number of in-flight assemblies.
    /// @return Assembly count.
    [[nodiscard]] std::size_t active_assemblies() const noexcept;

private:
    /// @brief Segmented transfer state for one node and direction.
    struct Assembly {
        bool active{false};
        bool request{false};
        std::uint16_t index{0};
        std::uint8_t subindex{0};
        std::size_t expected{0};
        std::size_t length{0};
        std::uint64_t started{0};
        std::array<std::uint8_t, max_transfer_bytes> data{};
    };

    /// @brief Download waiting for the server response.
    struct PendingWrite {
        bool active{false};
        std::uint16_t index{0};
        std::uint8_t subindex{0};
        std::size_t length{0};
        bool complete{false};
        bool response_ok{false};
        std::array<std::uint8_t, max_transfer_bytes> data{};
    };

    /// @brief Node index from an SDO identifier.
    [[nodiscard]] static std::size_t
    node_index(const nexigen::hw::can::CanMessage& message) noexcept;
    /// @brief Append seven segment bytes.
    [[nodiscard]] bool append_segment(Assembly& assembly,
                                      const nexigen::hw::can::CanMessage& message) noexcept;
    /// @brief Emit a pending write when both data and response are complete.
    void maybe_complete(std::size_t node, CoDecoded* out);
    /// @brief Handle one download request.
    void handle_request(std::size_t node, const nexigen::hw::can::CanMessage& message,
                        std::uint8_t command, CoDecoded& out, std::uint64_t now_ns);
    /// @brief Handle one download segment.
    [[nodiscard]] bool handle_download_segment(std::size_t node,
                                               const nexigen::hw::can::CanMessage& message,
                                               CoDecoded& out);
    /// @brief Handle one upload segment response.
    [[nodiscard]] bool handle_upload_segment(std::size_t node,
                                             const nexigen::hw::can::CanMessage& message,
                                             CoDecoded& out);
    /// @brief Handle one upload/segment response.
    void handle_response(std::size_t node, const nexigen::hw::can::CanMessage& message,
                         std::uint8_t command, CoDecoded& out, std::uint64_t now_ns,
                         const EdsDictionary* dictionary);
    /// @brief Handle an SDO abort.
    void handle_abort(std::size_t node, const nexigen::hw::can::CanMessage& message,
                      CoDecoded& out) noexcept;

    std::array<Assembly, 128> assemblies_{};     ///< Per-node assembly state.
    std::array<PendingWrite, 128> pending_{};    ///< Per-node pending write.
    std::vector<SdoCompletedWrite> completed_{}; ///< Completed writes.
};

} // namespace nexigen::canview
