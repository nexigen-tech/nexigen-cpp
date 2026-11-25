// Compile-time smoke check for the pinned ImPlot target (task 1.3).
//
// The object is built but never run: it proves that the pinned ImPlot revision
// exposes the heatmap API against the pinned ImGui docking revision.
#include <implot.h>

namespace nexigen::canview {

void implot_smoke_compile_check() {
    if (ImPlot::GetCurrentContext() == nullptr) {
        return;
    }
    const double values[] = {0.0, 1.0, 0.0};
    ImPlot::BeginPlot("canview-smoke");
    ImPlot::PlotHeatmap("load", values, 1, 3, 0.0, 1.0, nullptr, ImPlotPoint{0.0, 0.0},
                        ImPlotPoint{1.0, 3.0});
    ImPlot::EndPlot();
}

} // namespace nexigen::canview
