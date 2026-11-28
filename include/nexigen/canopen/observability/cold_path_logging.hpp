// nexigen::canopen::observability — cold-path logging helpers (change
// add-canopen-observability; spec canopen/observability; design D7).
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <nexigen/canopen/eds_dictionary.hpp>
#include <nexigen/canopen/eds_parse_result.hpp>
#include <nexigen/canopen/nmt_state.hpp>
#include <nexigen/canopen/observability/can_open_log_policy.hpp>
#include <nexigen/canopen/observability/can_open_runtime_state.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/core/serialization_mandate.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>

namespace nexigen::canopen::observability {

/// @brief Log the outcome of one EDS/DCF load on the cold path.
///
/// A successful load emits one `Info` record with the path, content hash,
/// object count, and PDO mapping count. A failed load emits one `Error` record
/// with the diagnostic count, then the first `max_cold_diagnostics`
/// diagnostics at `Warning`, then one trailing `Warning` record with the number
/// of omitted diagnostics. A failure without diagnostics stops after the
/// `Error` record.
///
/// @tparam Logger Logger satisfying `logging::concepts::Logger`.
/// @param logger Destination logger.
/// @param result Parser outcome to report.
/// @param bus Bus name used for correlation.
/// @param path Source path used for correlation.
/// @param policy Policy carrying `max_cold_diagnostics`.
/// @note Cold path: allocates inside the logger; never call per frame.
/// @note Spec `canopen/observability`, requirement "Cold-path logging is
///       detailed and bounded".
template <logging::concepts::Logger Logger>
void log_eds_result(Logger& logger, const canopen::EdsParseResult& result, std::string_view bus,
                    std::string_view path, const CanOpenLogPolicy& policy = {}) noexcept {
    if (result.ok()) {
        const canopen::EdsDictionary& dictionary = *result.dictionary;
        logger.log(core::LogLevel::Info,
                   "canopen eds loaded bus={} path={} hash=0x{:016x} objects={} pdo_mappings={}",
                   bus, path, dictionary.content_hash(), dictionary.object_count(),
                   dictionary.mappings().size());
        return;
    }
    logger.log(core::LogLevel::Error, "canopen eds load failed bus={} path={} diagnostics={}", bus,
               path, result.diagnostics.size());
    const std::size_t limit =
        std::min<std::size_t>(policy.max_cold_diagnostics, result.diagnostics.size());
    for (std::size_t index = 0U; index < limit; ++index) {
        const canopen::EdsDiagnostic& diagnostic = result.diagnostics[index];
        logger.log(core::LogLevel::Warning,
                   "canopen eds diagnostic bus={} path={} line={} message={}", bus, path,
                   diagnostic.line, diagnostic.message);
    }
    if (result.diagnostics.size() > limit) {
        logger.log(core::LogLevel::Warning,
                   "canopen eds diagnostics omitted bus={} path={} omitted={}", bus, path,
                   result.diagnostics.size() - limit);
    }
}

/// @brief Log one runtime lifecycle transition at `Info`.
///
/// @tparam Logger Logger satisfying `logging::concepts::Logger`.
/// @param logger Destination logger.
/// @param bus Bus name used for correlation.
/// @param from Previous runtime state.
/// @param to New runtime state.
/// @note Cold path (lifecycle edges only).
/// @note Spec `canopen/observability`, requirement "Cold-path logging is
///       detailed and bounded".
template <logging::concepts::Logger Logger>
void log_runtime_transition(Logger& logger, std::string_view bus, CanOpenRuntimeState from,
                            CanOpenRuntimeState to) noexcept {
    logger.log(core::LogLevel::Info, "canopen runtime transition bus={} from={} to={}", bus, from,
               to);
}

/// @brief Log one NMT state transition at `Info`.
///
/// @tparam Logger Logger satisfying `logging::concepts::Logger`.
/// @param logger Destination logger.
/// @param bus Bus name used for correlation.
/// @param node Node identifier that changed state.
/// @param from Previous NMT state.
/// @param to New NMT state.
/// @note Cold path (transition edge only).
/// @note Spec `canopen/observability`, requirement "Cold-path logging is
///       detailed and bounded".
template <logging::concepts::Logger Logger>
void log_nmt_transition(Logger& logger, std::string_view bus, std::uint8_t node,
                        canopen::NmtState from, canopen::NmtState to) noexcept {
    logger.log(core::LogLevel::Info, "canopen nmt transition bus={} node={} from={} to={}", bus,
               static_cast<unsigned>(node), from, to);
}

} // namespace nexigen::canopen::observability
