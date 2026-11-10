// Negative-compile probe (spec com/publish-subscribe "Trivially-copyable
// message contract"; design D3): a payload that is not trivially copyable —
// owning members (std::string), virtual functions — must be rejected at
// compile time by the Message<T> constraint.
//
// The probe is expected to FAIL to compile with a static_assert diagnostic;
// tests/com registers it through core/negative_compile.sh.
#include <string>
#include <vector>

#include <nexigen/com/concepts/message_concept.hpp>

struct OwnsString {
    std::string name;
};

struct HasVirtual {
    virtual ~HasVirtual() = default;
};

struct OwnsContainer {
    std::vector<int> values;
};

// All three violate the trivially-copyable contract — each line must trip.
static_assert(nexigen::com::concepts::Message<OwnsString>);
static_assert(nexigen::com::concepts::Message<HasVirtual>);
static_assert(nexigen::com::concepts::Message<OwnsContainer>);
