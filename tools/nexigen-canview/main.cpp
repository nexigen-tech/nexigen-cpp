// nexigen-canview — entry point.
#include <cstdio>
#include <string>

#include "canview_config.hpp"
#include "cli.hpp"
#include "headless.hpp"
#include "model/version.hpp"

#if defined(NEXIGEN_CANVIEW_GUI)
#include "ui/canview_window.hpp"
#endif

int main(int argc, char* argv[]) {
    const auto parsed = nexigen::canview::parse_cli(argc, argv);
    if (!parsed) {
        std::fputs(nexigen::canview::usage_text().data(), stderr);
        return 2;
    }
    if (parsed->help) {
        std::fputs(nexigen::canview::usage_text().data(), stdout);
        return 0;
    }
    if (parsed->version) {
        std::printf("%s %s\n", nexigen::canview::kToolName.data(),
                    nexigen::canview::version().data());
        return 0;
    }

    const std::string config_path =
        parsed->config_given ? parsed->config_path : nexigen::canview::default_config_path();
    bool corrupted = false;
    nexigen::canview::CanViewConfig config = nexigen::canview::load_config(config_path, &corrupted);
    if (corrupted) {
        std::fprintf(stderr, "canview: configuration is malformed; using defaults\n");
    }
    nexigen::canview::apply_cli_overrides(config, *parsed);
    if (!config.validate()) {
        std::fputs(nexigen::canview::usage_text().data(), stderr);
        return 2;
    }
    if (parsed->headless) {
        return nexigen::canview::run_headless(*parsed, config);
    }
#if defined(NEXIGEN_CANVIEW_GUI)
    const int code = nexigen::canview::run_gui(*parsed, config);
    if (code == 0 && !corrupted) {
        static_cast<void>(nexigen::canview::save_config(config_path, config));
    }
    return code;
#else
    std::fputs("canview: GUI is not available in this build\n", stderr);
    return 1;
#endif
}
