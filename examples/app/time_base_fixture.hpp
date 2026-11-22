// nexigen::examples — compatibility alias for the app library's embedded base.
//
// The implementation moved to app::detail::EmbeddedTimeBase so App<Policy>
// Embedded mode and the examples share one publisher. The alias keeps existing
// example code unchanged.
#pragma once

#include <nexigen/app/detail/embedded_time_base.hpp>

namespace nexigen::examples {

using LocalTimeBase = nexigen::app::detail::EmbeddedTimeBase;

} // namespace nexigen::examples
