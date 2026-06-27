#include "gui.hpp"
#include "imgui.h"
#include "implot.h"

static bool sliderDouble(const char *label, double &value, double minValue, double maxValue, const char *format)
{
    return ImGui::SliderScalar(
        label,
        ImGuiDataType_Double,
        &value,
        &minValue,
        &maxValue,
        format);
}

static void guiWindowDevice(const AppState &appState)
{
    ImGui::Begin("CUDA Device");

    if (!appState.cudaAvail)
    {
        ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f),
                           "No CUDA device found.");
        ImGui::TextDisabled("GPU features are unavailable.");
        ImGui::End();
        return;
    }

    const auto &device = appState.deviceInfo;
    ImGui::Text("Name:               %s", device.name);
    ImGui::Text("Compute Capability: %d.%d",
                device.computeCapabilityMajor, device.computeCapabilityMinor);
    ImGui::Text("Total Memory:       %.1f GB",
                static_cast<double>(device.totalMemoryBytes) / (1024.0 * 1024.0 * 1024.0));
    ImGui::Text("Double Precision:   %s",
                device.supportsDouble ? "Yes" : "No");

    ImGui::End();
}

static void guiWindowCompute(AppState &appState)
{
    ImGui::Begin("Convolution");

    bool changed = false;
    changed |= ImGui::SliderInt("GPU steps", &appState.N, 128, 65536);
    changed |= ImGui::SliderInt("CPU steps", &appState.cpuSteps, 128, 65536);
    if (changed)
    {
        appMarkDirty(appState);
    }

    ImGui::Checkbox("Auto recompute dirty data", &appState.autoRecomputeDirty);
    ImGui::SameLine();
    ImGui::TextDisabled("Dirty: %s", appState.dataDirty ? "yes" : "no");
    ImGui::Separator();

    ImGui::Text("GPU signal length: N = %d", appState.N);
    ImGui::Text("CPU reference steps: %d", appState.cpuSteps);
    ImGui::Text("Duration: %.2f s", appState.T);
    ImGui::Separator();

    if (!appState.cudaAvail)
    {
        ImGui::BeginDisabled();
    }
    const bool clicked = ImGui::Button("Run Convolution (GPU + CPU ref.)");
    if (!appState.cudaAvail)
    {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(no GPU)");
    }

    if (clicked)
    {
        appRunComputation(appState);
    }

    if (appState.hasResult)
    {
        ImGui::Separator();
        const auto &result = appState.lastResult;

        if (!result.success)
        {
            ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f),
                               "Error: %s", result.errorMessage);
        }
        else
        {
            ImGui::Text("Transfer CPU -> GPU: %8.3f ms", result.transferToGpuMs);
            ImGui::Text("Kernel:              %8.3f ms", result.kernelMs);
            ImGui::Text("Transfer GPU -> CPU: %8.3f ms", result.transferFromGpuMs);
            ImGui::Text("CPU reference:       %8.3f ms", result.cpuReferenceMs);
            ImGui::Separator();

            if (appState.validationAvailable)
            {
                ImGui::Text("Max absolute error:  %.6e", result.maxAbsError);
                if (result.validationPassed)
                {
                    ImGui::TextColored(ImVec4(0.2f, 1.f, 0.2f, 1.f),
                                       "Status: OK  (tolerance 1e-9)");
                }
                else
                {
                    ImGui::TextColored(ImVec4(1.f, 0.6f, 0.f, 1.f),
                                       "Status: VALIDATION FAILED");
                }
            }
            else
            {
                ImGui::TextDisabled("Validation skipped: CPU/GPU steps differ.");
            }
        }
    }

    ImGui::End();
}

static void guiWindowSignalA(AppState &appState)
{
    ImGui::Begin("Signal A");

    bool changed = false;
    changed |= sliderDouble("Amplitude", appState.signalAParams.amplitude, 0.0, 2.0, "%.3f");
    changed |= sliderDouble("Center", appState.signalAParams.center, 0.0, appState.T, "%.4f");
    changed |= sliderDouble("Sigma", appState.signalAParams.sigma, 0.001, 0.250, "%.4f");

    if (changed)
    {
        appMarkDirty(appState);
    }

    ImGui::End();
}

static void guiWindowSignalB(AppState &appState)
{
    ImGui::Begin("Signal B");

    bool changed = false;
    changed |= sliderDouble("Amplitude", appState.signalBParams.amplitude, 0.0, 2.0, "%.3f");
    changed |= sliderDouble("Start", appState.signalBParams.start, 0.0, appState.T, "%.4f");
    changed |= sliderDouble("End", appState.signalBParams.end, 0.0, appState.T, "%.4f");

    if (changed)
    {
        appMarkDirty(appState);
    }

    ImGui::End();
}

static void guiWindowSettings()
{
    ImGui::Begin("UI Settings");

    ImGuiStyle &style = ImGui::GetStyle();
    ImGui::SliderFloat("Font scale", &style.FontScaleMain, 0.75f, 2.00f, "%.2f");
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        style.FontScaleMain = 1.0f;
    }

    ImGui::End();
}

static ImPlotSpec makeLineSpec(const ImVec4 &color, float weight)
{
    ImPlotSpec spec;
    spec.LineColor = color;
    spec.LineWeight = weight;
    return spec;
}

static void guiWindowPlot(const AppState &appState)
{
    ImGui::Begin("Signal Plot");

    if (appState.plotT.empty())
    {
        ImGui::TextDisabled("No data to display.");
        ImGui::End();
        return;
    }

    const int gpuCount = static_cast<int>(appState.plotT.size());
    const double *gpuTime = appState.plotT.data();

    if (ImPlot::BeginPlot("##signals", ImVec2(-1, -1)))
    {
        ImPlot::SetupAxes("Time [s]", "Amplitude");

        ImPlot::PlotLine("Signal A (Gaussian)",
                         gpuTime, appState.plotA.data(), gpuCount,
                         makeLineSpec(ImVec4(0.3f, 0.7f, 1.0f, 1.f), 1.5f));

        ImPlot::PlotLine("Signal B (Rectangle)",
                         gpuTime, appState.plotB.data(), gpuCount,
                         makeLineSpec(ImVec4(1.0f, 0.7f, 0.2f, 1.f), 1.5f));

        if (appState.hasResult && appState.lastResult.success)
        {
            ImPlot::PlotLine("Convolution (GPU)",
                             gpuTime, appState.plotC.data(), gpuCount,
                             makeLineSpec(ImVec4(0.2f, 1.0f, 0.4f, 1.f), 2.0f));
        }

        if (appState.hasResult && !appState.plotCpuT.empty() && !appState.plotCref.empty())
        {
            const int cpuCount = static_cast<int>(appState.plotCpuT.size());
            ImPlot::PlotLine("Convolution (CPU ref.)",
                             appState.plotCpuT.data(), appState.plotCref.data(), cpuCount,
                             makeLineSpec(ImVec4(1.0f, 0.3f, 0.3f, 0.6f), 1.0f));
        }

        ImPlot::EndPlot();
    }

    ImGui::End();
}

void guiRender(AppState &appState)
{
    ImGui::DockSpaceOverViewport();
    guiWindowDevice(appState);
    guiWindowCompute(appState);
    guiWindowSignalA(appState);
    guiWindowSignalB(appState);
    guiWindowSettings();
    guiWindowPlot(appState);
    appRecomputeIfDirty(appState);
}
