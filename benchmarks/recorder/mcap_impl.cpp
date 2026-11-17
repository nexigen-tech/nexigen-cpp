// MCAP implementation TU for the storage-format benchmark.
//
// This file is compiled with exceptions enabled (see CMakeLists.txt) because
// the pinned MCAP writer contains an explicit throw in FileWriter. It is linked
// only into the benchmark and never into a production target.
#define MCAP_IMPLEMENTATION
#include <mcap/mcap.hpp>
