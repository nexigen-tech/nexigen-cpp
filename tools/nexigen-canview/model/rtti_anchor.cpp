// Keeps polymorphic typeinfo in one TU compiled with RTTI enabled so the
// canview test executable (which links nexigen::com) resolves weak typeinfo
// references emitted by its translation units.
#include <typeinfo>

#include "snapshot_source.hpp"

namespace nexigen::canview {

const std::type_info& snapshot_source_typeinfo() noexcept {
    return typeid(SnapshotSource);
}

} // namespace nexigen::canview
