# Runnable nexigen::chrono examples (change add-chrono-clock-service; task 6.1).
#
# Each executable is registered through the shared nexigen_add_example helper:
# linked against nexigen::chrono (warnings/optimizations/sanitizers arrive
# transitively via nexigen_core) and exposed as a CTest smoke test.

nexigen_add_example(ex_chrono_snapshot ex_chrono_snapshot.cpp nexigen::chrono)
