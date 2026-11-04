// Negative-compile probe for the fan-out member pack
// (dedup-sink-plumbing; spec core/sink-plumbing "Duplicate or absent member
// types fail at compile time" + "Index... beyond the pack").
//
// DELIBERATELY NOT part of any build target: tests/CMakeLists.txt registers
// it through tests/core/negative_compile.sh, which runs the compiler and
// requires the build to FAIL with the pack's static_assert diagnostics.
// Every statement below must be a compile-time error:
//   - get<DupA>() on a pack containing DupA twice  → "type absent or duplicated…
//   - get<int>() on a pack without int             → same diagnostics
//   - get<3>() on a three-member pack              → "index beyond the member pack"
#include <cstddef>

#include <nexigen/core/sink_pack.hpp>

struct DupA { std::size_t visit{}; };
struct Ala { std::size_t visit{}; };
struct Bet { std::size_t visit{}; };

using DupPack = nexigen::core::sink_pack::Fanout<DupA, DupA>;
using Triple = nexigen::core::sink_pack::Fanout<DupA, Ala, Bet>;

int main() {
    DupPack dup{ DupA{}, DupA{} };
    static_cast<void>(dup.get<DupA>());            // duplicated type
    Triple triple{ DupA{}, Ala{}, Bet{} };
    static_cast<void>(triple.get<int>());          // absent type
    static_cast<void>(triple.template get<3U>());  // index beyond pack
    return 0;
}
