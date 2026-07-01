#include "gui.hpp"

#include "imgui.h"
#include "implot.h"
#include "solver/trace_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
struct PlotAutoFitConfig
{
    std::string id;
    bool enabled = true;
    float zoom_out_wheels = 3.0f;
};

bool sliderDouble(const char* label, double* value, double minValue, double maxValue, const char* format = "%.3g")
{
    return ImGui::SliderScalar(label, ImGuiDataType_Double, value, &minValue, &maxValue, format);
}

PlotAutoFitConfig& plotAutoFitConfig(const char* id)
{
    static std::vector<PlotAutoFitConfig> configs;
    for (PlotAutoFitConfig& config : configs)
    {
        if (config.id == id)
            return config;
    }
    configs.push_back(PlotAutoFitConfig{id, true, 3.0f});
    return configs.back();
}

void plotAutoFitControls(const char* id)
{
    PlotAutoFitConfig& config = plotAutoFitConfig(id);
    ImGui::Checkbox(("Auto-fit##" + config.id).c_str(), &config.enabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    ImGui::SliderFloat(("Zoom-out##" + config.id).c_str(), &config.zoom_out_wheels, 0.0f, 8.0f, "%.1f");
}

const char* coolingScheduleName(CoolingSchedule schedule)
{
    switch (schedule)
    {
    case CoolingSchedule::Boltzmann:
        return "Boltzmann";
    case CoolingSchedule::Geometric:
        return "Geometric";
    case CoolingSchedule::Adaptive:
        return "Adaptive";
    }
    return "Unknown";
}

const char* solverStateName(SolverState state)
{
    switch (state)
    {
    case SolverState::Idle:
        return "Idle";
    case SolverState::Ready:
        return "Ready";
    case SolverState::Running:
        return "Running";
    case SolverState::Converged:
        return "Converged";
    case SolverState::MaxIter:
        return "MaxIter";
    case SolverState::Failed:
        return "Failed";
    }
    return "Unknown";
}

ImVec4 solverStateColor(SolverState state)
{
    switch (state)
    {
    case SolverState::Converged:
        return ImVec4(0.25f, 0.9f, 0.35f, 1.0f);
    case SolverState::Running:
        return ImVec4(1.0f, 0.85f, 0.25f, 1.0f);
    case SolverState::Failed:
        return ImVec4(1.0f, 0.25f, 0.25f, 1.0f);
    default:
        return ImVec4(0.72f, 0.72f, 0.72f, 1.0f);
    }
}

ImVec4 chi2HeatColor(double value, double minValue, double maxValue)
{
    double t = 0.0;
    if (maxValue > minValue)
        t = std::clamp((value - minValue) / (maxValue - minValue), 0.0, 1.0);
    return ImVec4(static_cast<float>(0.2 + 0.8 * t), static_cast<float>(0.9 - 0.65 * t), 0.15f, 1.0f);
}

bool isLogScaleParamName(const char* name)
{
    const std::string value(name);
    return value == "I0" || value == "A" || value == "Rs" || value == "Rsh" || value == "Rsh2" || value == "alpha";
}

std::vector<double> logSafeValues(const std::vector<double>& input)
{
    std::vector<double> output(input.size());
    for (size_t i = 0; i < input.size(); ++i)
        output[i] = std::max(std::abs(input[i]), 1e-30);
    return output;
}

bool expandedRange(const std::vector<double>& values, double wheels, double& minValue, double& maxValue, bool logScale = false)
{
    if (values.empty())
        return false;

    std::vector<double> finiteValues;
    finiteValues.reserve(values.size());
    for (double value : values)
    {
        if (std::isfinite(value) && (!logScale || value > 0.0))
            finiteValues.push_back(logScale ? std::log10(value) : value);
    }
    if (finiteValues.empty())
        return false;

    auto [minIt, maxIt] = std::minmax_element(finiteValues.begin(), finiteValues.end());
    minValue = *minIt;
    maxValue = *maxIt;
    if (!std::isfinite(minValue) || !std::isfinite(maxValue))
        return false;
    if (minValue == maxValue)
    {
        const double delta = logScale ? 0.05 : std::max(std::abs(minValue) * 0.01, 1e-30);
        minValue -= delta;
        maxValue += delta;
    }
    const double factor = std::pow(1.20, std::max(0.0, wheels));
    const double center = 0.5 * (minValue + maxValue);
    const double half = 0.5 * (maxValue - minValue) * factor;
    minValue = center - half;
    maxValue = center + half;
    if (logScale)
    {
        minValue = std::pow(10.0, minValue);
        maxValue = std::pow(10.0, maxValue);
    }
    return minValue < maxValue;
}

void setupAutoFitAxes(const char* id, const std::vector<double>& xs, const std::vector<double>& ys, bool xLog = false, bool yLog = false)
{
    const PlotAutoFitConfig& config = plotAutoFitConfig(id);
    if (!config.enabled)
        return;
    double xMin = 0.0;
    double xMax = 0.0;
    double yMin = 0.0;
    double yMax = 0.0;
    if (expandedRange(xs, config.zoom_out_wheels, xMin, xMax, xLog) &&
        expandedRange(ys, config.zoom_out_wheels, yMin, yMax, yLog))
    {
        ImPlot::SetupAxesLimits(xMin, xMax, yMin, yMax, ImPlotCond_Always);
    }
}

std::vector<int> freeParamIndices(const ModelConfig& model)
{
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(model.params.size()); ++i)
    {
        if (model.params[i].free)
            indices.push_back(i);
    }
    return indices;
}

int freeAxisForParam(const AppState& state, const char* name)
{
    int freeAxis = 0;
    for (const FitParam& param : state.model.params)
    {
        if (param.free)
        {
            if (param.name == name)
                return freeAxis;
            ++freeAxis;
        }
        else if (param.name == name)
        {
            return -1;
        }
    }
    return -1;
}

int degreesOfFreedom(const AppState& state)
{
    const int points = static_cast<int>(state.iv_data.V.size());
    const int freeCount = static_cast<int>(freeParamIndices(state.model).size());
    return std::max(1, points - freeCount);
}

void plotColoredSimplexPair(
    const char* title,
    const SimplexState& simplexState,
    int xAxis,
    int yAxis,
    const char* xLabel,
    const char* yLabel,
    const ImVec2& size)
{
    ImGui::Begin(title);
    plotAutoFitControls(title);
    if (xAxis < 0 || yAxis < 0)
    {
        ImGui::TextDisabled("%s: one of selected params is fixed or missing", title);
        ImGui::End();
        return;
    }

    double minChi2 = 0.0;
    double maxChi2 = 0.0;
    if (!simplexState.chi2_values.empty())
    {
        auto [minIt, maxIt] = std::minmax_element(simplexState.chi2_values.begin(), simplexState.chi2_values.end());
        minChi2 = *minIt;
        maxChi2 = *maxIt;
    }

    std::vector<double> xs;
    std::vector<double> ys;
    for (const std::vector<double>& vertex : simplexState.vertices)
    {
        if (xAxis >= static_cast<int>(vertex.size()) || yAxis >= static_cast<int>(vertex.size()))
            continue;
        if ((isLogScaleParamName(xLabel) && vertex[xAxis] <= 0.0) ||
            (isLogScaleParamName(yLabel) && vertex[yAxis] <= 0.0))
            continue;
        xs.push_back(vertex[xAxis]);
        ys.push_back(vertex[yAxis]);
    }

    if (ImPlot::BeginPlot(title, size))
    {
        ImPlot::SetupAxes(xLabel, yLabel);
        if (isLogScaleParamName(xLabel))
            ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        if (isLogScaleParamName(yLabel))
            ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
        setupAutoFitAxes(title, xs, ys, isLogScaleParamName(xLabel), isLogScaleParamName(yLabel));
        for (size_t i = 0; i < simplexState.vertices.size(); ++i)
        {
            const std::vector<double>& vertex = simplexState.vertices[i];
            if (xAxis >= static_cast<int>(vertex.size()) || yAxis >= static_cast<int>(vertex.size()))
                continue;
            if ((isLogScaleParamName(xLabel) && vertex[xAxis] <= 0.0) ||
                (isLogScaleParamName(yLabel) && vertex[yAxis] <= 0.0))
                continue;

            const double x = vertex[xAxis];
            const double y = vertex[yAxis];
            const double chi2 = i < simplexState.chi2_values.size() ? simplexState.chi2_values[i] : minChi2;
            const ImVec4 color = chi2HeatColor(chi2, minChi2, maxChi2);
            ImPlotSpec spec;
            spec.LineColor = color;
            spec.Marker = ImPlotMarker_Circle;
            spec.MarkerSize = 7.0f;
            spec.MarkerFillColor = color;
            spec.MarkerLineColor = color;
            spec.Flags = ImPlotItemFlags_NoLegend;
            char label[32];
            std::snprintf(label, sizeof(label), "v%zu", i);
            ImPlot::PlotScatter(label, &x, &y, 1, spec);
        }
        ImPlot::EndPlot();
    }
    ImGui::End();
}

void modelParamRow(FitParam& param)
{
    ImGui::PushID(param.name.c_str());
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(param.name.c_str());
    ImGui::TableNextColumn();
    if (!param.free)
        ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputDouble("##value", &param.value, 0.0, 0.0, "%.3e");
    if (!param.free)
        ImGui::EndDisabled();
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputDouble("##min", &param.min, 0.0, 0.0, "%.3e");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputDouble("##max", &param.max, 0.0, 0.0, "%.3e");
    ImGui::TableNextColumn();
    ImGui::Checkbox("##free", &param.free);
    ImGui::PopID();
}

void guiModeTabs(AppState& state)
{
    ImGui::Begin("Mode");
    const char* tabs[] = {"Single Fit", "Batch Fitting", "Simplex Inspector"};
    for (int i = 0; i < 3; ++i)
    {
        if (i > 0)
            ImGui::SameLine();
        if (ImGui::Selectable(tabs[i], state.active_tab == i, 0, ImVec2(150, 0)))
            state.active_tab = i;
    }
    ImGui::End();
}

void guiWindowSettings()
{
    ImGui::Begin("UI Settings");
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SliderFloat("Font scale", &style.FontScaleMain, 0.75f, 2.00f, "%.2f");
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
        style.FontScaleMain = 1.0f;
    ImGui::TextDisabled("Docking + multi-viewport enabled in main.cpp");
    ImGui::End();
}

void guiTabModel(AppState& state)
{
    bool useSixParam = state.model.use_6param;
    if (ImGui::RadioButton("4-param", !useSixParam))
    {
        state.model = ModelConfig::default4param();
        state.iv_data = IVData{};
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("6-param", useSixParam))
    {
        state.model = ModelConfig::default6param();
        state.iv_data = IVData{};
    }

    ImGui::InputDouble("Temperature [K]", &state.model.T, 1.0, 10.0, "%.2f");
    ImGui::InputDouble("V min [V]", &state.model.V_min, 0.01, 0.1, "%.3f");
    ImGui::InputDouble("V max [V]", &state.model.V_max, 0.01, 0.1, "%.3f");
    ImGui::SliderInt("Points", &state.model.N_points, 2, 2000);

    if (ImGui::BeginTable("Parameters", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Param");
        ImGui::TableSetupColumn("Val");
        ImGui::TableSetupColumn("Min");
        ImGui::TableSetupColumn("Max");
        ImGui::TableSetupColumn("Free");
        ImGui::TableHeadersRow();
        for (FitParam& param : state.model.params)
            modelParamRow(param);
        ImGui::EndTable();
    }

    if (ImGui::Button("Generate IV Curve"))
        appGenerateIVCurve(state);
    ImGui::SameLine();
    if (ImGui::Button("Clear All Curves"))
        appClearCurves(state);

    ImGui::SeparatorText("Load data from dat/");
    const char* currentColumns[] = {"Column 2: current [A]", "Column 3: auxiliary/current density"};
    int currentColumnUi = std::clamp(state.data_import.current_column - 1, 0, 1);
    if (ImGui::Combo("Current column", &currentColumnUi, currentColumns, 2))
        state.data_import.current_column = currentColumnUi + 1;
    if (ImGui::Button("Refresh dat list"))
        appRefreshDataFiles(state);
    if (!state.data_import.files.empty())
    {
        std::vector<std::string> fileLabels;
        std::vector<const char*> fileLabelPtrs;
        fileLabels.reserve(state.data_import.files.size());
        fileLabelPtrs.reserve(state.data_import.files.size());
        for (const std::string& path : state.data_import.files)
        {
            const size_t slash = path.find_last_of("\\/");
            fileLabels.push_back(slash == std::string::npos ? path : path.substr(slash + 1));
            fileLabelPtrs.push_back(fileLabels.back().c_str());
        }
        state.data_import.selected_index = std::clamp(
            state.data_import.selected_index,
            0,
            static_cast<int>(fileLabelPtrs.size()) - 1);
        ImGui::Combo("Data file", &state.data_import.selected_index, fileLabelPtrs.data(), static_cast<int>(fileLabelPtrs.size()));
        if (ImGui::Button("Load selected data"))
            appLoadSelectedDataFile(state);
        ImGui::SameLine();
        if (ImGui::Button("Load all as sweep"))
            appLoadAllDataFiles(state);
    }
    else
    {
        ImGui::TextDisabled("No .dat files found. Use Refresh dat list.");
    }
    if (!state.data_import.status.empty())
        ImGui::TextWrapped("%s", state.data_import.status.c_str());

    ImGui::SeparatorText("Range / Sweep Generator");
    ImGui::Checkbox("Sweep temperature", &state.sweep_cfg.temperature_enabled);
    if (!state.sweep_cfg.temperature_enabled)
        ImGui::BeginDisabled();
    ImGui::InputDouble("T from", &state.sweep_cfg.T_min, 1.0, 10.0, "%.2f");
    ImGui::InputDouble("T to", &state.sweep_cfg.T_max, 1.0, 10.0, "%.2f");
    ImGui::InputDouble("T step", &state.sweep_cfg.T_step, 1.0, 10.0, "%.2f");
    if (!state.sweep_cfg.temperature_enabled)
        ImGui::EndDisabled();

    const char* relations[] = {
        "T only: A/I0 fixed",
        "A(T) = A0 + slope*(T-Tref)",
        "I0(T) = I00*10^(slope*(T-Tref))",
        "A(T) + I0(T)"
    };
    state.sweep_cfg.temperature_relation = std::clamp(state.sweep_cfg.temperature_relation, 0, 3);
    ImGui::Combo("T relation", &state.sweep_cfg.temperature_relation, relations, 4);
    ImGui::InputDouble("T ref [K]", &state.sweep_cfg.T_ref, 1.0, 10.0, "%.2f");
    if (state.sweep_cfg.temperature_relation != 1 && state.sweep_cfg.temperature_relation != 3)
        ImGui::BeginDisabled();
    ImGui::InputDouble("A slope / K", &state.sweep_cfg.A_slope_per_K, 0.001, 0.01, "%.4e");
    if (state.sweep_cfg.temperature_relation != 1 && state.sweep_cfg.temperature_relation != 3)
        ImGui::EndDisabled();
    if (state.sweep_cfg.temperature_relation != 2 && state.sweep_cfg.temperature_relation != 3)
        ImGui::BeginDisabled();
    ImGui::InputDouble("log10(I0) slope / K", &state.sweep_cfg.log10_I0_slope_per_K, 0.001, 0.01, "%.4e");
    if (state.sweep_cfg.temperature_relation != 2 && state.sweep_cfg.temperature_relation != 3)
        ImGui::EndDisabled();

    ImGui::Checkbox("Sweep parameter", &state.sweep_cfg.parameter_enabled);
    if (!state.sweep_cfg.parameter_enabled)
        ImGui::BeginDisabled();
    std::vector<const char*> paramNames;
    for (const FitParam& param : state.model.params)
        paramNames.push_back(param.name.c_str());
    if (!paramNames.empty())
    {
        state.sweep_cfg.parameter_index = std::clamp(state.sweep_cfg.parameter_index, 0, static_cast<int>(paramNames.size()) - 1);
        ImGui::Combo("Parameter", &state.sweep_cfg.parameter_index, paramNames.data(), static_cast<int>(paramNames.size()));
    }
    ImGui::InputDouble("Param from", &state.sweep_cfg.parameter_min, 0.0, 0.0, "%.3e");
    ImGui::InputDouble("Param to", &state.sweep_cfg.parameter_max, 0.0, 0.0, "%.3e");
    ImGui::InputDouble("Param step", &state.sweep_cfg.parameter_step, 0.0, 0.0, "%.3e");
    if (!state.sweep_cfg.parameter_enabled)
        ImGui::EndDisabled();
    ImGui::InputInt("Max curves", &state.sweep_cfg.max_curves);
    if (ImGui::Button("Generate Sweep Curves"))
        appGenerateSweepCurves(state);
}

void guiTabNoise(AppState& state)
{
    const char* sigmaModes[] = {
        "Absolute [A]",
        "Relative to curve |Imax|",
        "Relative to I reference",
        "Percent per point"
    };
    if (ImGui::Combo("Sigma mode", &state.noise.sigma_mode, sigmaModes, 4))
        state.noise.relative = state.noise.sigma_mode == 1;

    const double sliderMin = state.noise.sigma_mode == 3 ? 0.0 : 1e-12;
    const double sliderMax = state.noise.sigma_mode == 0 ? 1e-3 : (state.noise.sigma_mode == 3 ? 100.0 : 1.0);
    const char* sigmaFormat = state.noise.sigma_mode == 3 ? "%.2f %%" : "%.3e";
    bool noiseChanged = sliderDouble(state.noise.sigma_mode == 3 ? "Noise [%]" : "Sigma slider", &state.noise.sigma, sliderMin, sliderMax, sigmaFormat);
    noiseChanged = ImGui::InputDouble(state.noise.sigma_mode == 3 ? "Noise value [%]" : "Sigma", &state.noise.sigma, 0.0, 0.0, sigmaFormat) || noiseChanged;
    if (state.noise.sigma_mode == 3)
        state.noise.sigma = std::clamp(state.noise.sigma, 0.0, 100.0);
    ImGui::InputScalar("Seed", ImGuiDataType_U32, &state.noise.seed);

    if (state.noise.sigma_mode != 2)
        ImGui::BeginDisabled();
    ImGui::InputDouble("I reference [A]", &state.noise.reference_current, 0.0, 0.0, "%.3e");
    if (state.noise.sigma_mode != 2)
        ImGui::EndDisabled();
    if (state.noise.sigma_mode == 3)
        ImGui::TextDisabled("Noise: I += N(0,1) * (I * noise / 100) for every point.");
    ImGui::Checkbox("Live update noise", &state.noise.live_update);
    if (state.noise.live_update && noiseChanged && state.iv_data.has_model)
        appAddNoise(state);

    if (!state.iv_data.has_model)
        ImGui::BeginDisabled();
    if (ImGui::Button("Add Noise to Data"))
        appAddNoise(state);
    if (!state.iv_data.has_model)
        ImGui::EndDisabled();

    double maxAbs = 0.0;
    for (double value : state.iv_data.I_model)
        maxAbs = std::max(maxAbs, std::abs(value));
    double sigmaAbs = state.noise.sigma;
    if (state.noise.sigma_mode == 1 || state.noise.relative)
        sigmaAbs *= maxAbs;
    else if (state.noise.sigma_mode == 2)
        sigmaAbs *= std::max(std::abs(state.noise.reference_current), 1e-30);
    else if (state.noise.sigma_mode == 3)
        sigmaAbs = maxAbs * std::clamp(state.noise.sigma, 0.0, 100.0) / 100.0;
    ImGui::SeparatorText("Preview");
    ImGui::Text("I_max: %.3e A", maxAbs);
    ImGui::Text("sigma_abs: %.3e A", sigmaAbs);
    if (sigmaAbs > 0.0 && maxAbs > 0.0)
        ImGui::Text("SNR: %.1f dB", 20.0 * std::log10(maxAbs / sigmaAbs));
}

void guiTabSolver(AppState& state)
{
    ImGui::SeparatorText("Nelder-Mead");
    sliderDouble("alpha reflection", &state.solver_cfg.nm_alpha, 0.1, 3.0);
    sliderDouble("gamma expansion", &state.solver_cfg.nm_gamma, 1.0, 4.0);
    sliderDouble("rho contraction", &state.solver_cfg.nm_rho, 0.05, 1.0);
    sliderDouble("sigma shrink", &state.solver_cfg.nm_sigma, 0.05, 1.0);
    ImGui::InputDouble("Degenerate tol", &state.solver_cfg.degenerate_tol, 0.0, 0.0, "%.3e");

    ImGui::SeparatorText("Simulated Annealing");
    ImGui::Checkbox("Enable SA", &state.solver_cfg.sa_enabled);
    if (!state.solver_cfg.sa_enabled)
        ImGui::BeginDisabled();
    ImGui::InputDouble("T initial", &state.solver_cfg.sa_config.T_initial, 0.1, 1.0, "%.3f");
    int schedule = static_cast<int>(state.solver_cfg.sa_config.schedule);
    const char* schedules[] = {"Boltzmann", "Geometric", "Adaptive"};
    if (ImGui::Combo("Schedule", &schedule, schedules, 3))
        state.solver_cfg.sa_config.schedule = static_cast<CoolingSchedule>(schedule);
    if (state.solver_cfg.sa_config.schedule != CoolingSchedule::Geometric)
        ImGui::BeginDisabled();
    sliderDouble("Geometric rate", &state.solver_cfg.sa_config.geometric_rate, 0.8, 0.9999, "%.4f");
    if (state.solver_cfg.sa_config.schedule != CoolingSchedule::Geometric)
        ImGui::EndDisabled();
    ImGui::InputScalar("RNG seed", ImGuiDataType_U32, &state.solver_cfg.rng_seed);
    if (!state.solver_cfg.sa_enabled)
        ImGui::EndDisabled();

    ImGui::SeparatorText("Trace");
    ImGui::Checkbox("Enable trace", &state.solver_cfg.trace_enabled);
    ImGui::InputInt("Trace limit", &state.solver_cfg.trace_limit);

    ImGui::SeparatorText("Stopping");
    ImGui::InputInt("Max iterations", &state.solver_cfg.max_iter);
    ImGui::InputDouble("Reduced chi2 tol", &state.solver_cfg.chi2_tol, 0.0, 0.0, "%.3e");

    if (ImGui::Button("Run Fit"))
        appRunFit(state);
    ImGui::SameLine();
    if (ImGui::Button("Run Multi-Fit"))
        appRunMultiFit(state);
    ImGui::SameLine();
    if (ImGui::Button("Step x1"))
        appStepFit(state, 1);
    ImGui::SameLine();
    if (ImGui::Button("Step x10"))
        appStepFit(state, 10);

    ImGui::TextColored(solverStateColor(state.fit_state.state), "State: %s", solverStateName(state.fit_state.state));
    ImGui::Text("Iteration: %d / %d", state.fit_state.last_result.iterations, state.solver_cfg.max_iter);
    ImGui::Text("chi2: %.6e", state.fit_state.last_result.chi2_min);
    ImGui::Text("reduced chi2: %.6e", state.fit_state.last_result.reduced_chi2_min);
    ImGui::Checkbox("Multi-fit uses noisy curves", &state.multi_fit.use_noisy);
    ImGui::InputInt("Multi-fit max cases", &state.multi_fit.max_cases);
}

void guiPanelControls(AppState& state)
{
    ImGui::Begin("Controls");
    if (ImGui::BeginTabBar("ControlTabs"))
    {
        if (ImGui::BeginTabItem("Model"))
        {
            guiTabModel(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Noise"))
        {
            guiTabNoise(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Solver"))
        {
            guiTabSolver(state);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void guiPanelIVCurve(AppState& state)
{
    ImGui::Begin("IV Curve");
    ImGui::Checkbox("Semi-log Y abs(I)", &state.plot_log_y);
    ImGui::SameLine();
    if (ImGui::Button("Autoscale"))
        state.plot_autoscale = true;
    ImGui::SameLine();
    plotAutoFitControls("IV Characteristic");
    ImGui::SameLine();
    ImGui::Text("Chi2: %.3e", state.fit_state.last_result.chi2_min);

    const ImVec2 plotSize(-1.0f, -1.0f);
    std::vector<double> modelAbs;
    std::vector<double> noisyAbs;
    std::vector<double> fittedAbs;
    std::vector<std::vector<double>> sweepAbs;
    std::vector<std::vector<double>> sweepNoisyAbs;
    if (state.plot_log_y)
    {
        auto toAbs = [](const std::vector<double>& input) {
            std::vector<double> output(input.size());
            for (size_t i = 0; i < input.size(); ++i)
                output[i] = std::max(std::abs(input[i]), 1e-30);
            return output;
        };
        if (state.iv_data.has_model)
            modelAbs = toAbs(state.iv_data.I_model);
        if (state.iv_data.has_noisy)
            noisyAbs = toAbs(state.iv_data.I_noisy);
        if (state.iv_data.has_fitted)
            fittedAbs = toAbs(state.iv_data.I_fitted);
        if (state.iv_data.has_sweep)
        {
            for (const std::vector<double>& curve : state.iv_data.I_sweep)
                sweepAbs.push_back(toAbs(curve));
        }
        if (state.iv_data.has_sweep_noisy)
        {
            for (const std::vector<double>& curve : state.iv_data.I_sweep_noisy)
                sweepNoisyAbs.push_back(toAbs(curve));
        }
    }

    std::vector<double> autoFitY;
    auto appendValues = [&autoFitY](const std::vector<double>& values) {
        autoFitY.insert(autoFitY.end(), values.begin(), values.end());
    };
    if (state.iv_data.has_model)
        appendValues(state.plot_log_y ? modelAbs : state.iv_data.I_model);
    if (state.iv_data.has_noisy)
        appendValues(state.plot_log_y ? noisyAbs : state.iv_data.I_noisy);
    if (state.iv_data.has_fitted)
        appendValues(state.plot_log_y ? fittedAbs : state.iv_data.I_fitted);
    if (state.iv_data.has_sweep)
    {
        for (size_t i = 0; i < state.iv_data.I_sweep.size(); ++i)
            appendValues(state.plot_log_y ? sweepAbs[i] : state.iv_data.I_sweep[i]);
    }
    if (state.iv_data.has_sweep_noisy)
    {
        for (size_t i = 0; i < state.iv_data.I_sweep_noisy.size(); ++i)
            appendValues(state.plot_log_y ? sweepNoisyAbs[i] : state.iv_data.I_sweep_noisy[i]);
    }

    if (ImPlot::BeginPlot("IV Characteristic", plotSize))
    {
        ImPlot::SetupAxes("V [V]", state.plot_log_y ? "|I| [A]" : "I [A]");
        if (state.plot_log_y)
            ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
        setupAutoFitAxes("IV Characteristic", state.iv_data.V, autoFitY, false, state.plot_log_y);
        if (state.iv_data.has_model)
            ImPlot::PlotLine("Model true", state.iv_data.V.data(), state.plot_log_y ? modelAbs.data() : state.iv_data.I_model.data(), static_cast<int>(state.iv_data.V.size()));
        if (state.iv_data.has_noisy)
            ImPlot::PlotScatter("Noisy data", state.iv_data.V.data(), state.plot_log_y ? noisyAbs.data() : state.iv_data.I_noisy.data(), static_cast<int>(state.iv_data.V.size()));
        if (state.iv_data.has_fitted)
            ImPlot::PlotLine("Fitted", state.iv_data.V.data(), state.plot_log_y ? fittedAbs.data() : state.iv_data.I_fitted.data(), static_cast<int>(state.iv_data.V.size()));
        if (state.iv_data.has_sweep)
        {
            for (size_t i = 0; i < state.iv_data.I_sweep.size(); ++i)
            {
                const std::vector<double>& curve = state.plot_log_y ? sweepAbs[i] : state.iv_data.I_sweep[i];
                const char* label = i < state.iv_data.sweep_labels.size() ? state.iv_data.sweep_labels[i].c_str() : "sweep";
                ImPlot::PlotLine(label, state.iv_data.V.data(), curve.data(), static_cast<int>(state.iv_data.V.size()));
            }
        }
        if (state.iv_data.has_sweep_noisy)
        {
            for (size_t i = 0; i < state.iv_data.I_sweep_noisy.size(); ++i)
            {
                const std::vector<double>& curve = state.plot_log_y ? sweepNoisyAbs[i] : state.iv_data.I_sweep_noisy[i];
                std::string label = i < state.iv_data.sweep_labels.size() ? state.iv_data.sweep_labels[i] + " noisy" : "sweep noisy";
                ImPlot::PlotScatter(label.c_str(), state.iv_data.V.data(), curve.data(), static_cast<int>(state.iv_data.V.size()));
            }
        }
        ImPlot::EndPlot();
    }
    ImGui::End();
}

void guiFitResults(AppState& state)
{
    ImGui::Text("Status: %s", solverStateName(state.fit_state.state));
    ImGui::Text("Iterations: %d", state.fit_state.last_result.iterations);
    ImGui::Text("Chi2 min: %.6e", state.fit_state.last_result.chi2_min);
    ImGui::Text("Reduced chi2: %.6e", state.fit_state.last_result.reduced_chi2_min);
    if (state.fit_state.state == SolverState::Failed)
        ImGui::TextColored(ImVec4(1, 0.25f, 0.25f, 1), "%s", state.fit_state.status_message.c_str());

    if (ImGui::BeginTable("ResultParams", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Param");
        ImGui::TableSetupColumn("True");
        ImGui::TableSetupColumn("Start");
        ImGui::TableSetupColumn("Fit");
        ImGui::TableSetupColumn("Free");
        ImGui::TableHeadersRow();
        size_t fitIndex = 0;
        for (size_t paramIndex = 0; paramIndex < state.model.params.size(); ++paramIndex)
        {
            const FitParam& param = state.model.params[paramIndex];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(param.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.6e", param.value);
            ImGui::TableNextColumn();
            if (paramIndex < state.fit_state.last_start_params.size())
                ImGui::Text("%.6e", state.fit_state.last_start_params[paramIndex]);
            else
                ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            if (param.free && fitIndex < state.fit_state.last_result.best_params.size())
                ImGui::Text("%.6e", state.fit_state.last_result.best_params[fitIndex++]);
            else
                ImGui::TextDisabled("fixed");
            ImGui::TableNextColumn();
            ImGui::Text("%s", param.free ? "yes" : "no");
        }
        ImGui::EndTable();
    }
}

void guiTraceTab(AppState& state)
{
    const int traceSize = static_cast<int>(state.fit_state.snapshot_trace.size());
    ImGui::Text("Trace entries: %d", traceSize);
    if (traceSize == 0)
        return;

    if (ImGui::Button("|<"))
        state.fit_state.trace_current_idx = 0;
    ImGui::SameLine();
    if (ImGui::Button("<"))
        state.fit_state.trace_current_idx = std::max(0, state.fit_state.trace_current_idx - 1);
    ImGui::SameLine();
    if (ImGui::Button(">"))
        state.fit_state.trace_current_idx = std::min(traceSize - 1, state.fit_state.trace_current_idx + 1);
    ImGui::SameLine();
    if (ImGui::Button(">|"))
        state.fit_state.trace_current_idx = traceSize - 1;

    ImGui::SliderInt("Trace idx", &state.fit_state.trace_current_idx, 0, traceSize - 1);
    const TraceStep& step = state.fit_state.snapshot_trace[state.fit_state.trace_current_idx];
    ImGui::TextUnformatted(traceStepToString(step).c_str());

    if (ImGui::BeginTable("TraceVertices", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Slot");
        ImGui::TableSetupColumn("Before chi2");
        ImGui::TableSetupColumn("After chi2");
        ImGui::TableSetupColumn("After values");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < step.state_after.vertices.size(); ++i)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%zu", i);
            ImGui::TableNextColumn();
            ImGui::Text("%.3e", step.state_before.chi2_values[i]);
            ImGui::TableNextColumn();
            ImGui::Text("%.3e", step.state_after.chi2_values[i]);
            ImGui::TableNextColumn();
            std::string values;
            for (double value : step.state_after.vertices[i])
            {
                char buffer[48];
                std::snprintf(buffer, sizeof(buffer), "%.3g ", value);
                values += buffer;
            }
            ImGui::TextUnformatted(values.c_str());
        }
        ImGui::EndTable();
    }
}

void guiPanelResults(AppState& state)
{
    ImGui::Begin("Results");
    if (ImGui::BeginTabBar("ResultsTabs"))
    {
        if (ImGui::BeginTabItem("Fit Results"))
        {
            guiFitResults(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Trace"))
        {
            guiTraceTab(state);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void guiPanelSimplex2D(AppState& state)
{
    ImGui::Begin("Simplex Controls");
    const int traceSize = static_cast<int>(state.fit_state.snapshot_trace.size());
    ImGui::Checkbox("Show Simplex 2D", &state.show_simplex_2d);
    ImGui::InputInt("Axis X", &state.simplex_axis_x);
    ImGui::InputInt("Axis Y", &state.simplex_axis_y);
    if (!state.multi_fit.cases.empty())
    {
        ImGui::SeparatorText("Multi-fit history source");
        state.multi_fit.selected = std::clamp(state.multi_fit.selected, 0, static_cast<int>(state.multi_fit.cases.size()) - 1);
        if (ImGui::BeginListBox("##multiFitCases", ImVec2(-1, 120)))
        {
            for (int i = 0; i < static_cast<int>(state.multi_fit.cases.size()); ++i)
            {
                const MultiFitCase& fitCase = state.multi_fit.cases[i];
                const bool selected = i == state.multi_fit.selected;
                std::string label = std::to_string(i) + ": " + fitCase.label + " reduced chi2=" + std::to_string(fitCase.result.reduced_chi2_min);
                if (ImGui::Selectable(label.c_str(), selected))
                {
                    state.multi_fit.selected = i;
                    state.fit_state.snapshot_trace = fitCase.trace;
                    state.fit_state.last_result = fitCase.result;
                    state.fit_state.last_start_params = fitCase.start_params;
                    state.fit_state.trace_current_idx = fitCase.trace.empty() ? 0 : static_cast<int>(fitCase.trace.size()) - 1;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndListBox();
        }
    }
    ImGui::End();

    const SimplexState* simplexState = nullptr;
    const int refreshedTraceSize = static_cast<int>(state.fit_state.snapshot_trace.size());
    if (refreshedTraceSize > 0)
    {
        state.fit_state.trace_current_idx = std::clamp(state.fit_state.trace_current_idx, 0, refreshedTraceSize - 1);
        simplexState = &state.fit_state.snapshot_trace[state.fit_state.trace_current_idx].state_after;
    }
    else if (state.fit_state.solver)
    {
        simplexState = &state.fit_state.solver->state();
    }

    if (!state.show_simplex_2d || simplexState == nullptr || simplexState->vertices.empty())
    {
        ImGui::Begin("Simplex Projection");
        ImGui::TextDisabled("No simplex yet. Use Step x1/x10 or Run Fit with trace enabled.");
        ImGui::End();
        return;
    }

    plotColoredSimplexPair(
        "Simplex projection",
        *simplexState,
        state.simplex_axis_x,
        state.simplex_axis_y,
        "axis X",
        "axis Y",
        ImVec2(-1, -1));

    const int axisI0 = freeAxisForParam(state, "I0");
    const int axisA = freeAxisForParam(state, "A");
    const int axisRs = freeAxisForParam(state, "Rs");
    const int axisRsh = freeAxisForParam(state, "Rsh");
    const int axisAlpha = freeAxisForParam(state, "alpha");
    const int axisRsh2 = freeAxisForParam(state, "Rsh2");
    plotColoredSimplexPair("A - I0", *simplexState, axisA, axisI0, "A", "I0", ImVec2(-1, -1));
    plotColoredSimplexPair("Rs - Rsh", *simplexState, axisRs, axisRsh, "Rs", "Rsh", ImVec2(-1, -1));
    plotColoredSimplexPair("alpha - Rsh2", *simplexState, axisAlpha, axisRsh2, "alpha", "Rsh2", ImVec2(-1, -1));

    ImGui::Begin("Simplex Vertices");
    if (ImGui::BeginTable("LiveSimplexVertices", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Vertex");
        ImGui::TableSetupColumn("chi2");
        ImGui::TableSetupColumn("x");
        ImGui::TableSetupColumn("y");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < simplexState->vertices.size(); ++i)
        {
            ImGui::TableNextRow();
            const double chi2 = i < simplexState->chi2_values.size() ? simplexState->chi2_values[i] : 0.0;
            double minChi2 = chi2;
            double maxChi2 = chi2;
            if (!simplexState->chi2_values.empty())
            {
                auto [minIt, maxIt] = std::minmax_element(simplexState->chi2_values.begin(), simplexState->chi2_values.end());
                minChi2 = *minIt;
                maxChi2 = *maxIt;
            }
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::ColorConvertFloat4ToU32(chi2HeatColor(chi2, minChi2, maxChi2)));
            ImGui::TableNextColumn();
            ImGui::Text("%zu%s%s", i, static_cast<int>(i) == simplexState->best_idx ? " best" : "", static_cast<int>(i) == simplexState->worst_idx ? " worst" : "");
            ImGui::TableNextColumn();
            ImGui::Text("%.3e", chi2);
            ImGui::TableNextColumn();
            const std::vector<double>& vertex = simplexState->vertices[i];
            ImGui::Text("%.6g", state.simplex_axis_x >= 0 && state.simplex_axis_x < static_cast<int>(vertex.size()) ? vertex[state.simplex_axis_x] : 0.0);
            ImGui::TableNextColumn();
            ImGui::Text("%.6g", state.simplex_axis_y >= 0 && state.simplex_axis_y < static_cast<int>(vertex.size()) ? vertex[state.simplex_axis_y] : 0.0);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void guiPanelParameterHistories(AppState& state)
{
    const int traceSize = static_cast<int>(state.fit_state.snapshot_trace.size());
    ImGui::Begin("History Controls");
    ImGui::Checkbox("Semi-log parameter Y", &state.history_log_y);
    ImGui::Checkbox("Log iteration X", &state.history_log_x);
    if (traceSize == 0)
    {
        ImGui::TextDisabled("No trace yet.");
        ImGui::End();
        return;
    }

    const std::vector<int> freeIndices = freeParamIndices(state.model);
    const int currentIndex = std::clamp(state.fit_state.trace_current_idx, 0, traceSize - 1);
    ImGui::Text("Current trace step: %d / %d", currentIndex, traceSize - 1);
    ImGui::End();

    std::vector<double> chiXs;
    std::vector<double> chiYs;
    chiXs.reserve(state.fit_state.snapshot_trace.size());
    chiYs.reserve(state.fit_state.snapshot_trace.size());
    const double chi2Scale = static_cast<double>(degreesOfFreedom(state));
    for (const TraceStep& step : state.fit_state.snapshot_trace)
    {
        chiXs.push_back(state.history_log_x ? static_cast<double>(step.iteration + 1) : static_cast<double>(step.iteration));
        chiYs.push_back(std::max(step.chi2_min / chi2Scale, 1e-30));
    }
    ImGui::Begin("History - chi2");
    plotAutoFitControls("History - chi2");
    if (ImPlot::BeginPlot("reduced chi2 vs iteration", ImVec2(-1, -1)))
    {
        ImPlot::SetupAxes(state.history_log_x ? "iteration + 1" : "iteration", "reduced chi2");
        if (state.history_log_x)
            ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
        setupAutoFitAxes("History - chi2", chiXs, chiYs, state.history_log_x, true);
        if (!chiXs.empty())
        {
            ImPlot::PlotLine("chi2", chiXs.data(), chiYs.data(), static_cast<int>(chiXs.size()));
            const int sampleIndex = std::clamp(currentIndex, 0, static_cast<int>(chiXs.size()) - 1);
            const double currentX = chiXs[sampleIndex];
            const double currentY = chiYs[sampleIndex];
            ImPlotSpec spec;
            spec.Marker = ImPlotMarker_Diamond;
            spec.MarkerSize = 8.0f;
            spec.MarkerFillColor = ImVec4(1.0f, 0.3f, 0.1f, 1.0f);
            spec.MarkerLineColor = ImVec4(1.0f, 0.3f, 0.1f, 1.0f);
            ImPlot::PlotScatter("current", &currentX, &currentY, 1, spec);
        }
        ImPlot::EndPlot();
    }
    ImGui::End();

    for (int freeAxis = 0; freeAxis < static_cast<int>(freeIndices.size()); ++freeAxis)
    {
        const int paramIndex = freeIndices[freeAxis];
        std::vector<double> xs;
        std::vector<double> ys;
        xs.reserve(state.fit_state.snapshot_trace.size());
        ys.reserve(state.fit_state.snapshot_trace.size());
        for (const TraceStep& step : state.fit_state.snapshot_trace)
        {
            const SimplexState& simplexState = step.state_after;
            if (simplexState.best_idx >= 0 &&
                simplexState.best_idx < static_cast<int>(simplexState.vertices.size()) &&
                freeAxis < static_cast<int>(simplexState.vertices[simplexState.best_idx].size()))
            {
                xs.push_back(state.history_log_x ? static_cast<double>(step.iteration + 1) : static_cast<double>(step.iteration));
                ys.push_back(simplexState.vertices[simplexState.best_idx][freeAxis]);
            }
        }

        const char* paramName = state.model.params[paramIndex].name.c_str();
        const std::string windowName = "History - " + state.model.params[paramIndex].name;
        std::vector<double> plotYs = (state.history_log_y && isLogScaleParamName(paramName)) ? logSafeValues(ys) : ys;
        ImGui::Begin(windowName.c_str());
        plotAutoFitControls(windowName.c_str());
        if (ImPlot::BeginPlot(paramName, ImVec2(-1, -1)))
        {
            ImPlot::SetupAxes(state.history_log_x ? "iteration + 1" : "iteration", paramName);
            if (state.history_log_x)
                ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
            if (state.history_log_y && isLogScaleParamName(paramName))
                ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
            setupAutoFitAxes(windowName.c_str(), xs, plotYs, state.history_log_x, state.history_log_y && isLogScaleParamName(paramName));
            if (!xs.empty())
            {
                ImPlot::PlotLine("best", xs.data(), plotYs.data(), static_cast<int>(xs.size()));
                const int sampleIndex = std::clamp(currentIndex, 0, static_cast<int>(xs.size()) - 1);
                const double currentX = xs[sampleIndex];
                const double currentY = plotYs[sampleIndex];
                ImPlotSpec spec;
                spec.Marker = ImPlotMarker_Diamond;
                spec.MarkerSize = 8.0f;
                spec.MarkerFillColor = ImVec4(1.0f, 0.3f, 0.1f, 1.0f);
                spec.MarkerLineColor = ImVec4(1.0f, 0.3f, 0.1f, 1.0f);
                ImPlot::PlotScatter("current", &currentX, &currentY, 1, spec);
            }
            ImPlot::EndPlot();
        }
        ImGui::End();
    }
}

void guiPanelBatchConfig(AppState& state)
{
    ImGui::Begin("Batch Config");
    ImGui::InputInt("N", &state.batch_cfg.N);
    ImGui::InputScalar("Seed", ImGuiDataType_U32, &state.batch_cfg.rng_seed);
    ImGui::Checkbox("Run NM", &state.batch_cfg.run_nm);
    ImGui::Checkbox("Run SA-NM", &state.batch_cfg.run_sa);
    ImGui::InputInt("Max iter", &state.batch_cfg.max_iter);
    ImGui::InputDouble("Reduced chi2 tol", &state.batch_cfg.chi2_tol, 0.0, 0.0, "%.3e");
    if (ImGui::Button("Generate Batch Preview"))
        appGenerateBatchPreview(state);
    const float fraction = state.batch_state.total > 0
        ? static_cast<float>(state.batch_state.progress) / static_cast<float>(state.batch_state.total)
        : 0.0f;
    ImGui::ProgressBar(fraction, ImVec2(-1, 0));
    ImGui::End();
}

void guiPanelBatchResults(AppState& state)
{
    ImGui::Begin("Batch Results");
    if (ImGui::BeginTable("BatchTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("NM");
        ImGui::TableSetupColumn("NM chi2");
        ImGui::TableSetupColumn("SA");
        ImGui::TableSetupColumn("SA chi2");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < state.batch_state.results.size(); ++i)
        {
            const BatchRow& row = state.batch_state.results[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%zu", i);
            ImGui::TableNextColumn();
            ImGui::Text("%s", row.nm_converged ? "ok" : "-");
            ImGui::TableNextColumn();
            ImGui::Text("%.3e", row.nm_result.chi2_min);
            ImGui::TableNextColumn();
            ImGui::Text("%s", row.sa_converged ? "ok" : "-");
            ImGui::TableNextColumn();
            ImGui::Text("%.3e", row.sa_result.chi2_min);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void guiPanelBatchStats(AppState& state)
{
    ImGui::Begin("Statistics");
    const int total = static_cast<int>(state.batch_state.results.size());
    int nmOk = 0;
    int saOk = 0;
    std::vector<double> nmChi2;
    std::vector<double> saChi2;
    for (const BatchRow& row : state.batch_state.results)
    {
        nmOk += row.nm_converged ? 1 : 0;
        saOk += row.sa_converged ? 1 : 0;
        nmChi2.push_back(row.nm_result.chi2_min);
        saChi2.push_back(row.sa_result.chi2_min);
    }
    ImGui::Text("Total: %d   NM: %d   SA-NM: %d", total, nmOk, saOk);
    if (ImPlot::BeginPlot("Chi2 distribution", ImVec2(-1, -1)))
    {
        ImPlot::SetupAxes("row", "chi2");
        if (!nmChi2.empty())
            ImPlot::PlotLine("NM chi2", nmChi2.data(), static_cast<int>(nmChi2.size()));
        if (!saChi2.empty())
            ImPlot::PlotLine("SA chi2", saChi2.data(), static_cast<int>(saChi2.size()));
        ImPlot::EndPlot();
    }
    ImGui::End();
}

void guiPanelLog(AppState& state)
{
    ImGui::Begin("Log");
    if (ImGui::Button("Clear"))
        state.log_entries.clear();
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &state.log_auto_scroll);
    ImGui::Separator();
    ImGui::BeginChild("LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const LogEntry& entry : state.log_entries)
    {
        ImVec4 color(1, 1, 1, 1);
        const char* prefix = "INFO";
        if (entry.level == LogLevel::Warn)
        {
            color = ImVec4(1.0f, 0.85f, 0.1f, 1.0f);
            prefix = "WARN";
        }
        else if (entry.level == LogLevel::Error)
        {
            color = ImVec4(1.0f, 0.3f, 0.2f, 1.0f);
            prefix = "ERROR";
        }
        ImGui::TextColored(color, "%s  %s", prefix, entry.text.c_str());
    }
    if (state.log_auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}
}

void guiRender(AppState& appState)
{
    ImGui::DockSpaceOverViewport();
    guiWindowSettings();
    guiModeTabs(appState);
    guiPanelControls(appState);

    if (appState.active_tab == 0)
    {
        guiPanelIVCurve(appState);
        guiPanelResults(appState);
        guiPanelSimplex2D(appState);
        guiPanelParameterHistories(appState);
    }
    else if (appState.active_tab == 1)
    {
        guiPanelBatchConfig(appState);
        guiPanelBatchResults(appState);
        guiPanelBatchStats(appState);
    }
    else
    {
        guiPanelIVCurve(appState);
        guiPanelSimplex2D(appState);
        guiPanelResults(appState);
        guiPanelParameterHistories(appState);
    }

    guiPanelLog(appState);
}
