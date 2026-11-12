// nexigen::chrono — the clock authority contract (change
// add-clock-authority-core; spec chrono/provider).
//
// The program's sole time authority: it issues per-source stampers (hot path
// stays on the issued instrument), samples the signed UTC offset for cold-path
// conversion, and delivers UTC-offset snapshots to a bound sink only when
// triggered. One thin provider type serves both daemon and manual modes.
#pragma once

#include <concepts>
#include <cstdint>
#include <string>

#include <nexigen/chrono/concepts/stamper_concept.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::chrono::concepts {

/// @brief A threadless clock authority: issue calibrated stampers, sample UTC, deliver on demand.
///
/// Satisfied by `nexigen::chrono::Provider`. The concept fixes the whole cold
/// surface: a nested `Stamper` satisfying `concepts::Stamper`, an issuing
/// `acquire()` on the cold path, the signed UTC offset and current UTC instant,
/// manual snapshot delivery, sink flush, and a diagnostic `to_string()`. Only
/// `to_string()` may allocate; every other operation is `noexcept` and stays
/// off producer hot paths.
///
/// @tparam P Candidate authority type; its `Stamper` must itself satisfy
///         `concepts::Stamper` and `acquire()` must return a `Stamper*` arm.
/// @note Spec `chrono/provider`, requirement "Stamper concept and issuance";
///       requirement "UTC offset authority"; requirement "Manual snapshot
///       delivery".
template <class P>
concept Provider = requires(P& p) {
    typename P::Stamper;
    requires Stamper<typename P::Stamper>;
    { p.acquire() } noexcept -> std::same_as<core::Result<typename P::Stamper*>>;
    { p.utc_offset() } noexcept -> std::same_as<std::int64_t>;
    { p.utc_now_ns() } noexcept -> std::same_as<std::int64_t>;
    { p.snapshot_now() } noexcept -> std::same_as<core::Status>;
    { p.flush() } noexcept -> std::same_as<core::Status>;
    { p.to_string() } -> std::same_as<std::string>;
};

} // namespace nexigen::chrono::concepts
