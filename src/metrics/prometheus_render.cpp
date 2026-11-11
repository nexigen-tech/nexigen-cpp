// nexigen::metrics — Prometheus text exposition renderer (change
// add-metrics-daemon; design D6). Cold path; no allocation.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include <nexigen/metrics/detail/prometheus_render.hpp>
#include <nexigen/metrics/metric_identity.hpp>

namespace nexigen::metrics::detail {

namespace {

/// Bounds-checked appender into caller-owned fixed storage.
struct Appender {
    char* out{nullptr};
    std::size_t cap{0};
    std::size_t used{0};
    bool ok{true};

    void raw(const char* data, std::size_t size) noexcept {
        if (!ok) {
            return;
        }
        if (size > cap - used) {
            ok = false;
            return;
        }
        std::memcpy(out + used, data, size);
        used += size;
    }
    void lit(std::string_view text) noexcept { raw(text.data(), text.size()); }
    void ch(char c) noexcept { raw(&c, 1U); }

    void u64(std::uint64_t value) noexcept {
        char text[24];
        const int n =
            std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(value));
        raw(text, n > 0 ? static_cast<std::size_t>(n) : 0U);
    }
    void i64(std::int64_t value) noexcept {
        char text[24];
        const int n = std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(value));
        raw(text, n > 0 ? static_cast<std::size_t>(n) : 0U);
    }
    void dbl(double value) noexcept {
        char text[40];
        const int n = std::snprintf(text, sizeof(text), "%.17g", value);
        raw(text, n > 0 ? static_cast<std::size_t>(n) : 0U);
    }
    /// Escaped, double-quoted label value (", \, and newline are escaped).
    void quoted(std::string_view text) noexcept {
        ch('"');
        for (const char c : text) {
            switch (c) {
                case '"':  lit("\\\""); break;
                case '\\': lit("\\\\"); break;
                case '\n': lit("\\n"); break;
                default:   ch(c); break;
            }
        }
        ch('"');
    }
};

/// The shared app/meter/version/pid label set (no surrounding braces).
void emit_labels(Appender& a, const MetricFrame& frame) noexcept {
    a.lit("app=");
    a.quoted(frame.app_id_text());
    a.lit(",meter=");
    a.quoted(view_name(frame.record.identity.meter_name));
    a.lit(",version=");
    a.quoted(view_name(frame.record.identity.meter_version));
    a.lit(",pid=\"");
    a.u64(frame.pid);
    a.ch('"');
}

void emit_type(Appender& a, std::string_view base, std::string_view suffix,
               std::string_view type) noexcept {
    a.lit("# TYPE ");
    a.lit(base);
    a.lit(suffix);
    a.ch(' ');
    a.lit(type);
    a.ch('\n');
}

void emit_unit(Appender& a, std::string_view base, std::string_view suffix,
               std::string_view unit) noexcept {
    if (unit.empty()) {
        return;
    }
    a.lit("# UNIT ");
    a.lit(base);
    a.lit(suffix);
    a.ch(' ');
    a.lit(unit);
    a.ch('\n');
}

void emit_sample_head(Appender& a, std::string_view base, std::string_view suffix,
                      const MetricFrame& frame) noexcept {
    a.lit(base);
    a.lit(suffix);
    a.ch('{');
    emit_labels(a, frame);
    a.lit("} ");
}

/// Emit a double sample of the derived `<base><suffix>` family.
void emit_double_family(Appender& a, std::string_view base, std::string_view suffix,
                        std::string_view unit, const MetricFrame& frame, double value) noexcept {
    emit_type(a, base, suffix, "gauge");
    emit_unit(a, base, suffix, unit);
    emit_sample_head(a, base, suffix, frame);
    a.dbl(value);
    a.ch('\n');
}

/// Emit a uint64 sample of the derived `<base><suffix>` family.
void emit_count_family(Appender& a, std::string_view base, std::string_view suffix,
                       std::string_view unit, const MetricFrame& frame,
                       std::uint64_t value) noexcept {
    emit_type(a, base, suffix, "gauge");
    emit_unit(a, base, suffix, unit);
    emit_sample_head(a, base, suffix, frame);
    a.u64(value);
    a.ch('\n');
}

/// Emit one `_bucket{...,le="<le>"}` line from a running cumulative count.
void emit_bucket(Appender& a, std::string_view base, const MetricFrame& frame, double le,
                 std::uint64_t cumulative) noexcept {
    a.lit(base);
    a.lit("_bucket{");
    emit_labels(a, frame);
    a.lit(",le=\"");
    a.dbl(le);
    a.lit("\"} ");
    a.u64(cumulative);
    a.ch('\n');
}

void emit_inf_bucket(Appender& a, std::string_view base, const MetricFrame& frame,
                     std::uint64_t total) noexcept {
    a.lit(base);
    a.lit("_bucket{");
    emit_labels(a, frame);
    a.lit(",le=\"+Inf\"} ");
    a.u64(total);
    a.ch('\n');
}

void render_histogram(Appender& a, std::string_view base, std::string_view unit,
                      const MetricFrame& frame) noexcept {
    const HistogramSnapshot& histogram = frame.record.histogram;
    emit_type(a, base, {}, "histogram");
    emit_unit(a, base, {}, unit);

    // buckets_count[i] is per-bucket; Prometheus needs cumulative counts with
    // `le` at each bucket's upper edge (buckets_lower[i + 1]). The final bucket
    // is the overflow range and folds into the mandatory +Inf line.
    std::uint64_t cumulative = 0;
    const std::uint32_t buckets = histogram.bucket_count;
    for (std::uint32_t i = 0; i + 1U < buckets; ++i) {
        cumulative += histogram.buckets_count[i];
        emit_bucket(a, base, frame, histogram.buckets_lower[i + 1U], cumulative);
    }
    emit_inf_bucket(a, base, frame, histogram.count);

    emit_sample_head(a, base, "_sum", frame);
    a.dbl(histogram.sum);
    a.ch('\n');

    emit_sample_head(a, base, "_count", frame);
    a.u64(histogram.count);
    a.ch('\n');
}

} // namespace

std::size_t render_prometheus(const MetricFrame& frame, char* out, std::size_t used,
                              std::size_t cap) noexcept {
    if (out == nullptr || used > cap) {
        return used;
    }
    const MetricRecord& record = frame.record;
    const std::string_view name = view_name(record.identity.metric_name);
    const std::string_view unit = view_name(record.identity.unit);
    if (name.empty()) {
        return used;
    }

    Appender a{out, cap, used, true};
    switch (record.kind) {
        case MetricKind::Counter:
            emit_type(a, name, {}, "counter");
            emit_unit(a, name, {}, unit);
            emit_sample_head(a, name, {}, frame);
            a.u64(record.scalar.counter);
            a.ch('\n');
            break;
        case MetricKind::Gauge:
            emit_type(a, name, {}, "gauge");
            emit_unit(a, name, {}, unit);
            emit_sample_head(a, name, {}, frame);
            if (record.scalar.is_double) {
                a.dbl(record.scalar.gauge_double);
            } else {
                a.i64(record.scalar.gauge_int);
            }
            a.ch('\n');
            break;
        case MetricKind::DescriptiveStatistics: {
            const StatisticsSnapshot& stats = record.statistics;
            emit_double_family(a, name, "_mean", unit, frame, stats.mean);
            emit_double_family(a, name, "_stddev", unit, frame, stats.stddev);
            emit_double_family(a, name, "_min", unit, frame, stats.min);
            emit_double_family(a, name, "_max", unit, frame, stats.max);
            emit_count_family(a, name, "_count", unit, frame, stats.count);
            break;
        }
        case MetricKind::ExplicitHistogram:
        case MetricKind::ExponentialHistogram: render_histogram(a, name, unit, frame); break;
        case MetricKind::State:
            emit_type(a, name, {}, "gauge");
            emit_unit(a, name, {}, unit);
            a.lit(name);
            a.lit("{");
            emit_labels(a, frame);
            a.lit(",state=");
            a.quoted(view_name(record.state.value));
            a.lit("} ");
            a.u64(1U);
            a.ch('\n');
            break;
    }
    return a.ok ? a.used : used;
}

} // namespace nexigen::metrics::detail
