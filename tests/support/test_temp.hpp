// Shared scratch directory for file-system tests.
//
// The suites use a common root so their pid-unique names cannot collide with
// unrelated /tmp entries. Create it before main instead of depending on the
// developer machine or CI to pre-create it.
#pragma once

#include <filesystem>
#include <system_error>

namespace nexigen::test {

[[maybe_unused]] inline const bool scratch_root_ready = [] {
    std::error_code error;
    std::filesystem::create_directories("/tmp/opencode", error);
    return true;
}();

} // namespace nexigen::test
