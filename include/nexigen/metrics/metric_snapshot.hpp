// nexigen::metrics — snapshot and record value types (design D12;
// spec metrics/metric-provider "Record and batch snapshot serialization").
//
// Each value type now lives in its own header; this file forwards to all of
// them so existing consumers keep compiling while they migrate to granular
// includes.
#pragma once

#include <nexigen/metrics/histogram_snapshot.hpp>
#include <nexigen/metrics/metric_batch_snapshot.hpp>
#include <nexigen/metrics/metric_identity.hpp>
#include <nexigen/metrics/metric_record.hpp>
#include <nexigen/metrics/scalar_value.hpp>
#include <nexigen/metrics/state_value.hpp>
#include <nexigen/metrics/statistics_snapshot.hpp>
