// nexigen-canview — compatibility aliases for the shared CANopen vocabulary.
#pragma once

#include <nexigen/canopen/co_service.hpp>
#include <nexigen/canopen/eds_access.hpp>
#include <nexigen/canopen/eds_data_kind.hpp>
#include <nexigen/canopen/eds_data_type_info.hpp>
#include <nexigen/canopen/nmt_state.hpp>

namespace nexigen::canview {

using canopen::CoService;
using canopen::EdsAccess;
using canopen::EdsDataKind;
using canopen::EdsDataTypeInfo;
using canopen::NmtState;
using canopen::eds_data_type;
using canopen::parse_eds_access;

} // namespace nexigen::canview
