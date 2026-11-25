// nexigen-canview — offline replay of imported sessions.
#pragma once

#include <cstddef>

#include <memory>
#include <string>
#include <vector>

#include "can_record_ring.hpp"
#include "canopen_decoder.hpp"
#include "canopen_registry.hpp"
#include "cob_id_router.hpp"
#include "eds_library.hpp"
#include "session_import.hpp"

namespace nexigen::canview {

/// @brief Replay counters.
struct ReplayReport {
    std::size_t frames{0};   ///< Frames written to history.
    std::size_t gaps{0};     ///< Loss markers written to history.
    std::size_t statuses{0}; ///< Status samples observed.
};

/// @brief Where an offline CANopen text came from.
enum class OfflineDecodeSource : std::uint8_t {
    None,     ///< No decode available.
    Exported, ///< File's `co_decoded` text.
    Rebuilt,  ///< Re-decoded from a matching/available EDS.
};

/// @brief Offline CANopen reconstruction from a session header.
struct OfflineDecoded {
    CoDecoded decoded{};                                   ///< Decoded presentation.
    OfflineDecodeSource source{OfflineDecodeSource::None}; ///< Provenance.
    bool hash_mismatch{false};                             ///< EDS hash differed from the header.
};

/// @brief Rebuilds CANopen bindings and decode text without COM.
class OfflineCanopen {
public:
    /// @brief Install bindings from the session header.
    /// @param result Imported session.
    /// @param library Optional EDS cache; `nullptr` keeps exported text only.
    void build(const ImportResult& result, EdsLibrary* library = nullptr);

    /// @brief Decode one imported frame.
    /// @param record Imported record.
    /// @return Decoded text and provenance.
    [[nodiscard]] OfflineDecoded decode(const SessionRecord& record) const;

    /// @brief Bindings reconstructed from the header.
    /// @return Registry reference.
    [[nodiscard]] const CanOpenRegistry& registry() const noexcept { return registry_; }
    /// @brief Non-fatal build warnings (missing EDS, hash mismatch).
    /// @return Warning list.
    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept { return warnings_; }
    /// @brief Live channel status in an offline session.
    /// @return Always `false`.
    [[nodiscard]] bool live_status_available() const noexcept { return false; }

private:
    CanOpenRegistry registry_{};                ///< Reconstructed bindings.
    CobIdRouter router_{};                      ///< Routes built from the bindings.
    std::unique_ptr<CanOpenDecoder> decoder_{}; ///< Decoder over the bindings.
    std::vector<std::string> warnings_{};       ///< Build warnings.
    std::vector<std::pair<std::uint16_t, std::uint8_t>>
        hash_mismatches_{}; ///< Mismatched bindings.
};

/// @brief Write imported frames and gaps into a history ring.
/// @param result Imported session.
/// @param ring Destination history.
/// @return Replay counters.
[[nodiscard]] ReplayReport replay_into_ring(const ImportResult& result, CanRecordRing& ring);

} // namespace nexigen::canview
