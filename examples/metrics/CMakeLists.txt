# Runnable nexigen::metrics examples (design D6; spec metrics/examples).
#
# Each executable is registered through the shared nexigen_add_example helper:
# linked against nexigen::metrics (nexigen_options warnings/optimizations/
# sanitizers arrive transitively via nexigen_core) and exposed as a CTest
# smoke test with the "examples" label.

nexigen_add_example(ex_counters_and_gauges ex_counters_and_gauges.cpp nexigen::metrics)
nexigen_add_example(ex_descriptive_statistics ex_descriptive_statistics.cpp nexigen::metrics)
nexigen_add_example(ex_histograms ex_histograms.cpp nexigen::metrics)
nexigen_add_example(ex_metric_provider_and_sinks ex_metric_provider_and_sinks.cpp nexigen::metrics)

