// nexigen-canview — compatibility aliases for the shared EDS parser.
#pragma once

#include <nexigen/canopen/eds_diagnostic.hpp>
#include <nexigen/canopen/eds_limits.hpp>
#include <nexigen/canopen/eds_parse_result.hpp>
#include <nexigen/canopen/eds_parser.hpp>

#include "eds_dictionary.hpp"

namespace nexigen::canview {

using canopen::EdsDiagnostic;
using canopen::EdsLimits;
using canopen::EdsParseResult;
using canopen::EdsParser;
using canopen::eds_content_hash;

} // namespace nexigen::canview
