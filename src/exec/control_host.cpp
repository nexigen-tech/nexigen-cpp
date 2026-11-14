// nexigen::exec — COM-free control-host factory slot
// (change unify-exec-suite-control-path).
#include <nexigen/exec/control_host.hpp>

namespace nexigen::exec {

namespace {

ControlHostFactory g_control_host_factory{nullptr};

} // namespace

void set_control_host_factory(ControlHostFactory factory) noexcept {
    g_control_host_factory = factory;
}

ControlHostFactory control_host_factory() noexcept {
    return g_control_host_factory;
}

} // namespace nexigen::exec
