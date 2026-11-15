# nexigen::integration — full-stack simulation suite demonstration.
#
# Three applications in one Function Group exercised together with the
# Execution Manager, the State Manager, and Platform Health Management: one
# generator, one healthy receiver, and one receiver that crashes on a timer so
# the platform has to raise it again. Built only when nexigen::com is enabled
# (the iceoryx2 backend needs the Rust toolchain); see run_simulation_suite.sh
# for the full scenario.
if(NEXIGEN_ENABLE_COM)
    nexigen_add_example(ex_sim_generator ex_sim_generator.cpp nexigen::app
        nexigen::com nexigen::exec nexigen::phm
        nexigen::logging_com nexigen::metrics_com nexigen::tracing_com)
    nexigen_add_example(ex_sim_receiver ex_sim_receiver.cpp nexigen::app
        nexigen::com nexigen::exec nexigen::phm
        nexigen::logging_com nexigen::metrics_com nexigen::tracing_com)
    nexigen_add_example(ex_sim_flaky_receiver ex_sim_flaky_receiver.cpp nexigen::app
        nexigen::com nexigen::exec nexigen::phm
        nexigen::logging_com nexigen::metrics_com nexigen::tracing_com)
endif()
