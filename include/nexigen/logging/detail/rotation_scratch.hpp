// nexigen::logging — caller-owned path scratch for rotation (design D11).
#pragma once

namespace nexigen::logging::detail {

/// Caller-owned path scratch: two fixed buffers, reused every rotation.
/// Both sized 560: base(≤511) + '/' + label(≤"epoch-"+20) fits; file adds
/// '/' + file_name(≤255) checked by put()'s bounds logic at every append.
struct RotationScratch {
    char dir[560]{};
    char file[560]{};
};

} // namespace nexigen::logging::detail
