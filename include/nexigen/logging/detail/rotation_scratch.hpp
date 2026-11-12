// nexigen::logging — caller-owned path scratch for rotation (design D11).
#pragma once

namespace nexigen::logging::detail {

/// @brief Caller-owned path scratch: two fixed buffers, reused every rotation.
///
/// Both buffers are sized 560 bytes: `base_dir` (≤511) + '/' + a label
/// (≤`"epoch-"` + 20 digits) fits, and the file path adds '/' + `file_name`
/// (≤255); the rotation builder's `put()` bounds logic checks every append.
/// The owner is the consumer thread, which reuses one instance for the
/// logger's lifetime, so rotation performs no heap allocation.
///
/// @note Spec `logging/file-logger`, requirement "Hitch-free scheduled
///       directory rotation"; size-based segments reuse the same buffers under
///       requirement "Size-based file rotation".
struct RotationScratch {
    char dir[560]{};  ///< `"<base_dir>/<label>"` staging buffer, NUL-terminated.
    char file[560]{}; ///< `"<dir>/<file_name>"` staging buffer, NUL-terminated.
};

} // namespace nexigen::logging::detail
