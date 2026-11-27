// nexigen-canview — COB-ID router adapter over the shared core.
#pragma once

#include <nexigen/canopen/cob_id_conflict.hpp>
#include <nexigen/canopen/cob_id_route.hpp>
#include <nexigen/canopen/cob_id_router.hpp>

#include "canopen_types.hpp"

namespace nexigen::canview {

class CanOpenRegistry;

using canopen::CobIdConflict;
using canopen::CobIdRoute;

/// @brief Shared router with a registry-shaped rebuild overload.
class CobIdRouter final : public canopen::CobIdRouter {
public:
    using canopen::CobIdRouter::rebuild;

    /// @brief Rebuild routes from the viewer registry.
    /// @param registry Current bindings.
    void rebuild(const CanOpenRegistry& registry);
};

} // namespace nexigen::canview
