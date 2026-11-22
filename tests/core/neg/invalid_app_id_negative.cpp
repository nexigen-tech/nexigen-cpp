// Negative-compile probe for the AppId alphabet.
//
// DELIBERATELY NOT part of any build target: tests/CMakeLists.txt registers it
// through tests/core/negative_compile.sh. The literal contains a space, so the
// compile-time predicate used by Iceoryx2Policy must reject it.
#include <nexigen/core/app_id.hpp>

static_assert(nexigen::core::AppId::literal_is_valid("BAD ID"),
              "a tag outside the AppId alphabet must fail to compile");

// The public literal factory must also reject the same input at compile time.
constexpr auto invalid_literal = nexigen::core::AppId::from_literal("BAD ID");
static_assert(invalid_literal.empty());
