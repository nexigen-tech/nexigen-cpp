// nexigen::exec — executable integrity digest (change p2-exec-platform-controls;
// promoted to nexigen::core::integrity by add-integrity-primitives).
//
// Compatibility shim: the implementation now lives in the base library, so
// other modules can verify files without depending on the process supervisor.
// The names and signatures are unchanged.
#pragma once

#include <nexigen/core/integrity/sha256.hpp>

namespace nexigen::exec {

/// @brief Incremental SHA-256 (alias of `core::integrity::Sha256`).
using Sha256 = core::integrity::Sha256;

/// @brief SHA-256 of a buffer or string as lowercase hex.
using core::integrity::sha256_hex;

/// @brief SHA-256 of one file as lowercase hex.
using core::integrity::sha256_file;

} // namespace nexigen::exec
