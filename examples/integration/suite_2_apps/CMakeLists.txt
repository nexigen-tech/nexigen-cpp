# nexigen::integration — two-application suite demonstration (suite_2_apps).
#
# ex_generator publishes one DataSample per second over nexigen::com with a
# TraceHeader; ex_receiver subscribes and inherits the trace. Both publish their
# telemetry to the three daemons through the iceoryx2 sinks. Built only when
# nexigen::com is enabled (the iceoryx2 backend needs the Rust toolchain); see
# run_suite_2_apps.sh for the full scenario.
if(NEXIGEN_ENABLE_COM)
    nexigen_add_example(ex_generator ex_generator.cpp nexigen::app
        nexigen::com nexigen::logging_com nexigen::metrics_com nexigen::tracing_com)
    nexigen_add_example(ex_receiver ex_receiver.cpp nexigen::app
        nexigen::com nexigen::logging_com nexigen::metrics_com nexigen::tracing_com)
endif()
