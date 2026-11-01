# nexigen::runner — multi-application suite demonstration (ex_suite_app).
#
# One configurable binary reused as three distinct instances by the suite
# manifest; see examples/suite/run_suite_demo.sh and
# openspec/changes/add-suite-demo for the scenario.
nexigen_add_example(ex_suite_app ex_suite_app.cpp nexigen::app)
