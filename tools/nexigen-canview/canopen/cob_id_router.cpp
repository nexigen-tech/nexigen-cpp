#include "cob_id_router.hpp"

#include <array>
#include <span>

#include "canopen_registry.hpp"

namespace nexigen::canview {

void CobIdRouter::rebuild(const CanOpenRegistry& registry) {
    std::array<canopen::CobIdBinding, CanOpenRegistry::capacity> bindings{};
    std::size_t count = 0U;
    for (std::uint16_t slot = 0U; slot < CanOpenRegistry::capacity; ++slot) {
        const NodeBinding* binding = registry.at(slot);
        if (binding == nullptr) {
            continue;
        }
        bindings[count].slot = binding->registry_slot;
        bindings[count].channel_id = binding->channel_id;
        bindings[count].node_id = binding->node_id;
        bindings[count].dictionary = binding->dictionary.get();
        ++count;
    }
    canopen::CobIdRouter::rebuild(std::span<const canopen::CobIdBinding>{bindings.data(), count});
}

} // namespace nexigen::canview
