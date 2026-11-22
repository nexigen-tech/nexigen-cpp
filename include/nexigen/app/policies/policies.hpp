/// @file
/// @brief Umbrella for the COM-free infrastructure policies.
///
/// @note The iceoryx2 policy is intentionally not included here because it
///       requires `NEXIGEN_ENABLE_COM=ON` and the `*_com` link targets.
#pragma once

#include <nexigen/app/policies/console_policy.hpp>
#include <nexigen/app/policies/file_policy.hpp>
#include <nexigen/app/policies/null_policy.hpp>
