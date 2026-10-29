# nexigen::diagnostics runnable examples.
nexigen_add_example(ex_stopwatch ex_stopwatch.cpp nexigen::diagnostics)
# Cross-module: the stopwatch feeds an explicit-bucket histogram, so the
# example links diagnostics and metrics (metrics links core/chrono itself).
nexigen_add_example(ex_stopwatch_histogram ex_stopwatch_histogram.cpp
                    nexigen::diagnostics nexigen::metrics)
