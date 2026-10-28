// nexigen::tracing — provider cold-path compilation unit (design D2): the
// lifecycle, consumer loop, and flush handshake for the DEFAULT parameters
// are compiled here through explicit instantiation, so downstream TUs using
// TracerProvider<> link against this object instead of re-instantiating the
// cold machinery (non-default parameters implicitly instantiate in the
// using TU — tests only).
#include <nexigen/tracing/tracer_provider.hpp>

namespace nexigen::tracing {

// Design D2: provider.cpp owns the cold path for the shipped default shape.
template class TracerProvider<1024, 32>;

} // namespace nexigen::tracing
