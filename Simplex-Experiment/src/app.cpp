#include "app.hpp"

#include "solver/diode_model.hpp"
#include "solver/diode_objective.hpp"

#ifndef NDEBUG
#include "tests/test_stage1.hpp"
#include "tests/test_stage2.hpp"
#include "tests/test_stage3.hpp"
#include "tests/test_stage4.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <sstream>

namespace
{
struct LoadedCurve
{
    std::vector<double> voltage;
    std::vector<double> current;
    double temperature = 300.0;
    std::string label;
};

std::vector<double> currentParamValues(const ModelConfig& model)
{
    std::vector<double> values;
    values.reserve(model.params.size());
    for (const FitParam& param : model.params)
        values.push_back(param.value);
    return values;
}

bool isLogStartParam(const std::string& name)
{
    return name == "I0" || name == "Rs" || name == "Rsh" || name == "Rsh2";
}

int freeParamCount(const ModelConfig& model)
{
    int count = 0;
    for (const FitParam& param : model.params)
    {
        if (param.free)
            ++count;
    }
    return count;
}

double reducedChi2Scale(const ModelConfig& model, size_t pointCount)
{
    return static_cast<double>(std::max(1, static_cast<int>(pointCount) - freeParamCount(model)));
}

std::vector<std::filesystem::path> dataFolderCandidates()
{
    std::vector<std::filesystem::path> paths;
    const std::filesystem::path cwd = std::filesystem::current_path();
    paths.push_back(cwd / "dat");
    paths.push_back(cwd / "Simplex-Experiment" / "dat");
    paths.push_back(cwd / ".." / "Simplex-Experiment" / "dat");
    paths.push_back(cwd / ".." / ".." / "Simplex-Experiment" / "dat");
    paths.push_back(cwd / ".." / ".." / ".." / "Simplex-Experiment" / "dat");
    paths.push_back(cwd / ".." / ".." / ".." / ".." / "Simplex-Experiment" / "dat");
    return paths;
}

std::filesystem::path findDataFolder()
{
    for (const std::filesystem::path& path : dataFolderCandidates())
    {
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && std::filesystem::is_directory(path, ec))
            return std::filesystem::weakly_canonical(path, ec);
    }
    return {};
}

double parseTemperatureFromFilename(const std::filesystem::path& path)
{
    const std::string name = path.filename().string();
    const size_t marker = name.find("_T");
    if (marker == std::string::npos)
        return 300.0;

    size_t begin = marker + 2;
    size_t end = begin;
    while (end < name.size() && std::isdigit(static_cast<unsigned char>(name[end])))
        ++end;
    if (end == begin)
        return 300.0;

    return std::stod(name.substr(begin, end - begin));
}

LoadedCurve loadDatCurve(const std::filesystem::path& path, int currentColumn)
{
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Cannot open data file: " + path.string());

    LoadedCurve curve;
    curve.temperature = parseTemperatureFromFilename(path);
    curve.label = path.stem().string() + " T=" + std::to_string(curve.temperature) + "K";

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream stream(line);
        std::vector<double> columns;
        double value = 0.0;
        while (stream >> value)
            columns.push_back(value);

        const int column = std::clamp(currentColumn, 1, 2);
        if (columns.size() > static_cast<size_t>(column))
        {
            curve.voltage.push_back(columns[0]);
            curve.current.push_back(columns[static_cast<size_t>(column)]);
        }
    }

    if (curve.voltage.size() < 2 || curve.current.size() != curve.voltage.size())
        throw std::runtime_error("Invalid data file: " + path.string());
    return curve;
}

void clearFitAfterDataChange(AppState& appState)
{
    appState.iv_data.I_fitted.clear();
    appState.iv_data.has_fitted = false;
    appState.fit_state.state = SolverState::Ready;
    appState.fit_state.solver.reset();
    appState.fit_state.snapshot_trace.clear();
    appState.fit_state.trace_current_idx = 0;
    appState.fit_state.last_result = FitResult{};
    appState.fit_state.last_start_params.clear();
    appState.multi_fit.cases.clear();
    appState.multi_fit.selected = 0;
}

double evaluateCurrentModel(const ModelConfig& model, double voltage, const double* params)
{
    if (model.use_6param)
        return evaluateDiodeIV6(voltage, params, model.T);
    return evaluateDiodeIV4(voltage, params, model.T);
}

std::vector<double> makeSigmaVector(const AppState& appState)
{
    const std::vector<double>& curve = appState.iv_data.has_model ? appState.iv_data.I_model : appState.iv_data.V;
    const size_t count = curve.size();
    if (appState.noise.sigma_mode == 3)
    {
        std::vector<double> sigma(count);
        const double factor = std::clamp(appState.noise.sigma, 0.0, 100.0) / 100.0;
        for (size_t i = 0; i < count; ++i)
            sigma[i] = std::max(std::abs(curve[i]) * factor, 1e-30);
        return sigma;
    }

    double sigma = appState.noise.sigma;
    if ((appState.noise.sigma_mode == 1 || appState.noise.relative) && appState.iv_data.has_model)
    {
        double maxAbs = 0.0;
        for (double value : curve)
            maxAbs = std::max(maxAbs, std::abs(value));
        sigma *= std::max(maxAbs, 1e-30);
    }
    else if (appState.noise.sigma_mode == 2)
    {
        sigma *= std::max(std::abs(appState.noise.reference_current), 1e-30);
    }
    return std::vector<double>(count, std::max(sigma, 1e-30));
}

double noiseSigmaForCurve(const AppState& appState, const std::vector<double>& curve)
{
    double sigma = appState.noise.sigma;
    if (appState.noise.sigma_mode == 1 || appState.noise.relative)
    {
        double maxAbs = 0.0;
        for (double value : curve)
            maxAbs = std::max(maxAbs, std::abs(value));
        sigma *= std::max(maxAbs, 1e-30);
    }
    else if (appState.noise.sigma_mode == 2)
    {
        sigma *= std::max(std::abs(appState.noise.reference_current), 1e-30);
    }
    return std::max(sigma, 1e-30);
}

std::vector<double> noiseSigmaVectorForCurve(const AppState& appState, const std::vector<double>& curve)
{
    if (appState.noise.sigma_mode == 3)
    {
        std::vector<double> sigma(curve.size());
        const double factor = std::clamp(appState.noise.sigma, 0.0, 100.0) / 100.0;
        for (size_t i = 0; i < curve.size(); ++i)
            sigma[i] = std::max(std::abs(curve[i]) * factor, 1e-30);
        return sigma;
    }
    return std::vector<double>(curve.size(), noiseSigmaForCurve(appState, curve));
}

std::vector<double> addNoiseToCurve(AppState& appState, const std::vector<double>& curve, std::mt19937& rng)
{
    std::vector<double> noisy(curve.size());
    if (appState.noise.sigma_mode == 3)
    {
        const double factor = std::clamp(appState.noise.sigma, 0.0, 100.0) / 100.0;
        std::normal_distribution<double> distribution(0.0, 1.0);
        for (size_t i = 0; i < curve.size(); ++i)
        {
            const double noise = curve[i] * factor;
            noisy[i] = curve[i] + distribution(rng) * noise;
        }
        return noisy;
    }

    const std::vector<double> sigma = noiseSigmaVectorForCurve(appState, curve);
    for (size_t i = 0; i < curve.size(); ++i)
    {
        std::normal_distribution<double> distribution(0.0, sigma[i]);
        noisy[i] = curve[i] + distribution(rng);
    }
    return noisy;
}

void applyTemperatureRelation(const AppState& appState, std::vector<double>& params, double temperature)
{
    const double deltaT = temperature - appState.sweep_cfg.T_ref;
    const int relation = appState.sweep_cfg.temperature_relation;

    if ((relation == 1 || relation == 3) && params.size() > 1)
    {
        const FitParam& A = appState.model.params[1];
        params[1] = std::clamp(A.value + appState.sweep_cfg.A_slope_per_K * deltaT, A.min, A.max);
    }

    if ((relation == 2 || relation == 3) && !params.empty())
    {
        const FitParam& I0 = appState.model.params[0];
        const double safeI0 = std::max(std::abs(I0.value), 1e-30);
        const double scaled = safeI0 * std::pow(10.0, appState.sweep_cfg.log10_I0_slope_per_K * deltaT);
        params[0] = std::clamp(scaled, I0.min, I0.max);
    }
}

std::string solverStateName(SolverState state)
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

std::vector<double> sigmaVectorForCurve(const AppState& appState, const std::vector<double>& curve)
{
    return noiseSigmaVectorForCurve(appState, curve);
}

void randomizeStartParams(AppState& appState, std::vector<FitParam>& solverParams, std::vector<double>* starts)
{
    std::mt19937 rng(appState.solver_cfg.rng_seed);
    if (starts != nullptr)
    {
        starts->clear();
        starts->reserve(solverParams.size());
    }

    for (FitParam& param : solverParams)
    {
        if (param.free)
        {
            if (isLogStartParam(param.name))
            {
                const double safeValue = std::max(std::abs(param.value), 1e-30);
                const double low = std::max(param.min, safeValue * 1e-2);
                const double high = std::min(param.max, safeValue * 1e2);
                if (low > 0.0 && high > low)
                {
                    std::uniform_real_distribution<double> logDistribution(std::log10(low), std::log10(high));
                    param.value = std::pow(10.0, logDistribution(rng));
                }
            }
            else if (param.max > param.min)
            {
                std::uniform_real_distribution<double> distribution(param.min, param.max);
                param.value = distribution(rng);
            }
        }
        if (starts != nullptr)
            starts->push_back(param.value);
    }
}

std::unique_ptr<SANelderMead> makeSolverForData(
    AppState& appState,
    const std::vector<double>& measured,
    const std::vector<double>& sigma,
    double temperature,
    std::vector<double>* starts)
{
    std::vector<FitParam> solverParams = appState.model.params;
    randomizeStartParams(appState, solverParams, starts);
    const bool useSixParam = appState.model.use_6param;

    auto modelFn = [useSixParam, temperature](double voltage, const double* params) {
        if (useSixParam)
            return evaluateDiodeIV6(voltage, params, temperature);
        return evaluateDiodeIV4(voltage, params, temperature);
    };

    SANelderMead::ObjectiveFn objective = makeDiodeIVObjective(
        solverParams,
        modelFn,
        appState.iv_data.V,
        measured,
        sigma);

    return std::make_unique<SANelderMead>(
        solverParams,
        objective,
        appState.solver_cfg.nm_alpha,
        appState.solver_cfg.nm_gamma,
        appState.solver_cfg.nm_rho,
        appState.solver_cfg.nm_sigma,
        appState.solver_cfg.degenerate_tol,
        appState.solver_cfg.sa_enabled,
        appState.solver_cfg.sa_config,
        appState.solver_cfg.rng_seed,
        appState.solver_cfg.trace_enabled,
        reducedChi2Scale(appState.model, measured.size()));
}

std::unique_ptr<SANelderMead> makeSolver(AppState& appState)
{
    const std::vector<double>& measured = appState.iv_data.has_noisy
        ? appState.iv_data.I_noisy
        : appState.iv_data.I_model;
    const std::vector<double> sigma = makeSigmaVector(appState);
    return makeSolverForData(appState, measured, sigma, appState.model.T, &appState.fit_state.last_start_params);
}

void updateFittedCurve(AppState& appState, const std::vector<double>& fullParams)
{
    appState.iv_data.I_fitted.resize(appState.iv_data.V.size());
    for (size_t i = 0; i < appState.iv_data.V.size(); ++i)
        appState.iv_data.I_fitted[i] = evaluateCurrentModel(appState.model, appState.iv_data.V[i], fullParams.data());
    appState.iv_data.has_fitted = true;
}

std::vector<double> fittedCurveFor(
    const AppState& appState,
    const std::vector<double>& fullParams,
    double temperature)
{
    std::vector<double> curve(appState.iv_data.V.size());
    for (size_t i = 0; i < appState.iv_data.V.size(); ++i)
    {
        if (appState.model.use_6param)
            curve[i] = evaluateDiodeIV6(appState.iv_data.V[i], fullParams.data(), temperature);
        else
            curve[i] = evaluateDiodeIV4(appState.iv_data.V[i], fullParams.data(), temperature);
    }
    return curve;
}
}

ModelConfig ModelConfig::default4param()
{
    ModelConfig config;
    config.use_6param = false;
    config.params = {
        FitParam("I0", 1e-10, 1e-15, 1e-5, true),
        FitParam("A", 1.5, 0.5, 3.0, true),
        FitParam("Rs", 0.1, 0.0, 10.0, true),
        FitParam("Rsh", 1000.0, 10.0, 1e6, true),
    };
    return config;
}

ModelConfig ModelConfig::default6param()
{
    ModelConfig config = default4param();
    config.use_6param = true;
    config.params.push_back(FitParam("alpha", 1.5, 0.5, 3.0, true));
    config.params.push_back(FitParam("Rsh2", 500.0, 1.0, 1e5, true));
    return config;
}

void AppState::log(LogLevel level, const std::string& message)
{
    log_entries.push_back(LogEntry{message, level});
    if (log_entries.size() > 500)
        log_entries.erase(log_entries.begin(), log_entries.begin() + static_cast<long long>(log_entries.size() - 500));
}

void appInit(AppState& appState)
{
#ifndef NDEBUG
    runStage1Tests();
    runStage2Tests();
    runStage3Tests();
    runStage4Tests();
#endif

    appState.model = ModelConfig::default4param();
    appState.solver_cfg.sa_config.T_initial = 5.0;
    appState.solver_cfg.sa_config.T_current = 5.0;
    appState.solver_cfg.sa_config.schedule = CoolingSchedule::Geometric;
    appState.solver_cfg.sa_config.geometric_rate = 0.995;
    appInitGpu(appState);
    appState.log(LogLevel::Info, "Application initialized");
    appRefreshDataFiles(appState);
    appGenerateIVCurve(appState);
}

void appInitGpu(AppState& appState)
{
    appState.gpu_state.device = gpuQueryDevice();
    if (appState.gpu_state.device.available)
    {
        appState.log(LogLevel::Info,
            "GPU detected: " + appState.gpu_state.device.name +
            " sm_" + std::to_string(appState.gpu_state.device.compute_major) +
            std::to_string(appState.gpu_state.device.compute_minor));
        appValidateGpuLambertW(appState);
    }
    else
    {
        appState.log(LogLevel::Warn, appState.gpu_state.device.message);
    }
}

void appValidateGpuLambertW(AppState& appState)
{
    appState.gpu_state.lambertw_validation = gpuValidateLambertW();
    appState.gpu_state.validation_ran = true;
    appState.log(
        appState.gpu_state.lambertw_validation.passed ? LogLevel::Info : LogLevel::Error,
        appState.gpu_state.lambertw_validation.message);
}

void appValidateGpuCore(AppState& appState)
{
    appValidateGpuLambertW(appState);

    appState.gpu_state.current_validation = gpuValidateDiodeCurrent();
    appState.log(
        appState.gpu_state.current_validation.passed ? LogLevel::Info : LogLevel::Error,
        appState.gpu_state.current_validation.message);

    appState.gpu_state.noise_validation = gpuValidateNoise();
    appState.log(
        appState.gpu_state.noise_validation.passed ? LogLevel::Info : LogLevel::Error,
        appState.gpu_state.noise_validation.message);

    appState.gpu_state.simplex_validation = gpuValidateSimplexOneStep();
    appState.log(
        appState.gpu_state.simplex_validation.passed ? LogLevel::Info : LogLevel::Error,
        appState.gpu_state.simplex_validation.message);
}

void appGenerateIVCurve(AppState& appState)
{
    if (appState.model.N_points < 2)
        appState.model.N_points = 2;
    if (appState.model.V_max <= appState.model.V_min)
        appState.model.V_max = appState.model.V_min + 0.1;

    const std::vector<double> params = currentParamValues(appState.model);
    const int count = appState.model.N_points;

    appState.iv_data.V.resize(count);
    appState.iv_data.I_model.resize(count);
    for (int i = 0; i < count; ++i)
    {
        const double t = static_cast<double>(i) / static_cast<double>(count - 1);
        const double voltage = appState.model.V_min + t * (appState.model.V_max - appState.model.V_min);
        appState.iv_data.V[i] = voltage;
        appState.iv_data.I_model[i] = evaluateCurrentModel(appState.model, voltage, params.data());
    }

    appState.iv_data.I_noisy.clear();
    appState.iv_data.I_fitted.clear();
    appState.iv_data.I_sweep.clear();
    appState.iv_data.I_sweep_noisy.clear();
    appState.iv_data.sweep_params.clear();
    appState.iv_data.sweep_temperatures.clear();
    appState.iv_data.sweep_labels.clear();
    appState.iv_data.has_model = true;
    appState.iv_data.has_noisy = false;
    appState.iv_data.has_fitted = false;
    appState.iv_data.has_sweep = false;
    appState.iv_data.has_sweep_noisy = false;
    appState.fit_state.state = SolverState::Ready;
    appState.fit_state.solver.reset();
    appState.fit_state.snapshot_trace.clear();
    appState.fit_state.trace_current_idx = 0;
    appState.log(LogLevel::Info, "IV curve generated: " + std::to_string(count) + " points");
}

void appGenerateSweepCurves(AppState& appState)
{
    if (!appState.iv_data.has_model)
        appGenerateIVCurve(appState);

    const bool refreshNoise = appState.iv_data.has_noisy || appState.noise.live_update;
    appState.iv_data.I_sweep.clear();
    appState.iv_data.I_sweep_noisy.clear();
    appState.iv_data.sweep_params.clear();
    appState.iv_data.sweep_temperatures.clear();
    appState.iv_data.sweep_labels.clear();
    appState.iv_data.has_sweep = false;
    appState.iv_data.has_sweep_noisy = false;

    std::vector<double> temperatures;
    if (appState.sweep_cfg.temperature_enabled)
    {
        const double step = std::abs(appState.sweep_cfg.T_step) > 0.0 ? std::abs(appState.sweep_cfg.T_step) : 1.0;
        for (double value = appState.sweep_cfg.T_min; value <= appState.sweep_cfg.T_max + 0.5 * step; value += step)
            temperatures.push_back(value);
    }
    else
    {
        temperatures.push_back(appState.model.T);
    }

    std::vector<double> parameterValues;
    const int parameterIndex = std::clamp(appState.sweep_cfg.parameter_index, 0, static_cast<int>(appState.model.params.size()) - 1);
    if (appState.sweep_cfg.parameter_enabled)
    {
        const double step = std::abs(appState.sweep_cfg.parameter_step) > 0.0 ? std::abs(appState.sweep_cfg.parameter_step) : 1.0;
        for (double value = appState.sweep_cfg.parameter_min; value <= appState.sweep_cfg.parameter_max + 0.5 * step; value += step)
            parameterValues.push_back(value);
    }
    else
    {
        parameterValues.push_back(appState.model.params[parameterIndex].value);
    }

    const int maxCurves = std::max(1, appState.sweep_cfg.max_curves);
    std::vector<double> params = currentParamValues(appState.model);
    int generated = 0;
    for (double temperature : temperatures)
    {
        for (double parameterValue : parameterValues)
        {
            if (generated >= maxCurves)
                break;
            std::vector<double> curve(appState.iv_data.V.size());
            params = currentParamValues(appState.model);
            applyTemperatureRelation(appState, params, temperature);
            if (appState.sweep_cfg.parameter_enabled && parameterIndex >= 0 && parameterIndex < static_cast<int>(params.size()))
                params[parameterIndex] = parameterValue;
            for (size_t i = 0; i < appState.iv_data.V.size(); ++i)
            {
                if (appState.model.use_6param)
                    curve[i] = evaluateDiodeIV6(appState.iv_data.V[i], params.data(), temperature);
                else
                    curve[i] = evaluateDiodeIV4(appState.iv_data.V[i], params.data(), temperature);
            }
            std::ostringstream label;
            label << "T=" << temperature;
            if (appState.sweep_cfg.temperature_relation == 1 || appState.sweep_cfg.temperature_relation == 3)
                label << " A(T)=" << params[1];
            if (appState.sweep_cfg.temperature_relation == 2 || appState.sweep_cfg.temperature_relation == 3)
                label << " I0(T)=" << params[0];
            if (appState.sweep_cfg.parameter_enabled)
                label << " " << appState.model.params[parameterIndex].name << "=" << parameterValue;
            appState.iv_data.I_sweep.push_back(std::move(curve));
            appState.iv_data.sweep_params.push_back(params);
            appState.iv_data.sweep_temperatures.push_back(temperature);
            appState.iv_data.sweep_labels.push_back(label.str());
            ++generated;
        }
    }

    appState.iv_data.has_sweep = !appState.iv_data.I_sweep.empty();
    if (refreshNoise)
        appAddNoise(appState);
    appState.log(LogLevel::Info, "Sweep curves generated: " + std::to_string(generated));
}

void appRefreshDataFiles(AppState& appState)
{
    appState.data_import.files.clear();
    const std::filesystem::path folder = findDataFolder();
    if (folder.empty())
    {
        appState.data_import.status = "Data folder not found";
        appState.log(LogLevel::Warn, appState.data_import.status);
        return;
    }

    std::error_code ec;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, ec))
    {
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() == ".dat")
            appState.data_import.files.push_back(entry.path().string());
    }

    std::sort(appState.data_import.files.begin(), appState.data_import.files.end(), [](const std::string& lhs, const std::string& rhs) {
        const double tLhs = parseTemperatureFromFilename(lhs);
        const double tRhs = parseTemperatureFromFilename(rhs);
        if (tLhs == tRhs)
            return lhs < rhs;
        return tLhs < tRhs;
    });

    appState.data_import.selected_index = std::clamp(
        appState.data_import.selected_index,
        0,
        std::max(0, static_cast<int>(appState.data_import.files.size()) - 1));
    appState.data_import.status = "Found " + std::to_string(appState.data_import.files.size()) + " dat files";
    appState.log(LogLevel::Info, appState.data_import.status);
}

void appLoadSelectedDataFile(AppState& appState)
{
    if (appState.data_import.files.empty())
        appRefreshDataFiles(appState);
    if (appState.data_import.files.empty())
        return;

    try
    {
        const int index = std::clamp(appState.data_import.selected_index, 0, static_cast<int>(appState.data_import.files.size()) - 1);
        const LoadedCurve curve = loadDatCurve(appState.data_import.files[index], appState.data_import.current_column);

        appState.model.T = curve.temperature;
        appState.model.N_points = static_cast<int>(curve.voltage.size());
        appState.model.V_min = curve.voltage.front();
        appState.model.V_max = curve.voltage.back();

        appState.iv_data = IVData{};
        appState.iv_data.V = curve.voltage;
        appState.iv_data.I_model = curve.current;
        appState.iv_data.has_model = true;
        clearFitAfterDataChange(appState);

        appState.data_import.status = "Loaded " + curve.label;
        appState.log(LogLevel::Info, appState.data_import.status);
    }
    catch (const std::exception& exception)
    {
        appState.data_import.status = exception.what();
        appState.log(LogLevel::Error, exception.what());
    }
}

void appLoadAllDataFiles(AppState& appState)
{
    if (appState.data_import.files.empty())
        appRefreshDataFiles(appState);
    if (appState.data_import.files.empty())
        return;

    try
    {
        std::vector<LoadedCurve> curves;
        curves.reserve(appState.data_import.files.size());
        for (const std::string& file : appState.data_import.files)
            curves.push_back(loadDatCurve(file, appState.data_import.current_column));

        const LoadedCurve& first = curves.front();
        appState.model.T = first.temperature;
        appState.model.N_points = static_cast<int>(first.voltage.size());
        appState.model.V_min = first.voltage.front();
        appState.model.V_max = first.voltage.back();

        appState.iv_data = IVData{};
        appState.iv_data.V = first.voltage;
        appState.iv_data.I_model = first.current;
        appState.iv_data.has_model = true;

        for (size_t i = 0; i < curves.size(); ++i)
        {
            if (curves[i].voltage.size() != appState.iv_data.V.size())
                continue;
            appState.iv_data.I_sweep.push_back(curves[i].current);
            appState.iv_data.sweep_temperatures.push_back(curves[i].temperature);
            appState.iv_data.sweep_labels.push_back(curves[i].label);
        }
        appState.iv_data.has_sweep = !appState.iv_data.I_sweep.empty();

        clearFitAfterDataChange(appState);
        appState.data_import.status = "Loaded " + std::to_string(appState.iv_data.I_sweep.size()) + " curves from dat";
        appState.log(LogLevel::Info, appState.data_import.status);
    }
    catch (const std::exception& exception)
    {
        appState.data_import.status = exception.what();
        appState.log(LogLevel::Error, exception.what());
    }
}

void appAddNoise(AppState& appState)
{
    if (!appState.iv_data.has_model)
    {
        appState.log(LogLevel::Warn, "Cannot add noise before generating model data");
        return;
    }

    const std::vector<double> sigma = makeSigmaVector(appState);
    std::mt19937 rng(appState.noise.seed);
    appState.iv_data.I_noisy = addNoiseToCurve(appState, appState.iv_data.I_model, rng);

    appState.iv_data.I_sweep_noisy.clear();
    if (appState.iv_data.has_sweep)
    {
        appState.iv_data.I_sweep_noisy.reserve(appState.iv_data.I_sweep.size());
        for (const std::vector<double>& curve : appState.iv_data.I_sweep)
            appState.iv_data.I_sweep_noisy.push_back(addNoiseToCurve(appState, curve, rng));
    }
    appState.iv_data.has_noisy = true;
    appState.iv_data.has_sweep_noisy = appState.iv_data.has_sweep && !appState.iv_data.I_sweep_noisy.empty();
    appState.iv_data.has_fitted = false;
    appState.log(LogLevel::Info, "Noise added: sigma=" + std::to_string(sigma.empty() ? 0.0 : sigma.front()));
}

void appClearCurves(AppState& appState)
{
    appState.iv_data = IVData{};
    appState.fit_state.state = SolverState::Idle;
    appState.fit_state.solver.reset();
    appState.fit_state.snapshot_trace.clear();
    appState.fit_state.trace_current_idx = 0;
    appState.fit_state.last_result = FitResult{};
    appState.fit_state.last_start_params.clear();
    appState.multi_fit.cases.clear();
    appState.multi_fit.selected = 0;
    appState.log(LogLevel::Info, "All curves and fit preview cleared");
}

void appInitStepSolver(AppState& appState)
{
    if (!appState.iv_data.has_model)
        appGenerateIVCurve(appState);

    try
    {
        appState.fit_state.solver = makeSolver(appState);
        appState.fit_state.solver->initSimplex();
        appState.fit_state.state = SolverState::Ready;
        appState.fit_state.snapshot_trace.clear();
        appState.fit_state.trace_current_idx = 0;
        appState.log(LogLevel::Info, "Step solver initialized");
    }
    catch (const std::exception& exception)
    {
        appState.fit_state.state = SolverState::Failed;
        appState.fit_state.status_message = exception.what();
        appState.log(LogLevel::Error, exception.what());
    }
}

void appRunFit(AppState& appState)
{
    if (!appState.iv_data.has_model)
        appGenerateIVCurve(appState);

    try
    {
        appState.fit_state.state = SolverState::Running;
        appState.fit_state.solver = makeSolver(appState);
        appState.fit_state.solver->initSimplex();
        const FitResult result = appState.fit_state.solver->runUntilConvergence(
            appState.solver_cfg.max_iter,
            appState.solver_cfg.chi2_tol);

        appState.fit_state.last_result = result;
        appState.fit_state.snapshot_trace = appState.fit_state.solver->getTrace();
        appState.fit_state.trace_current_idx = appState.fit_state.snapshot_trace.empty()
            ? 0
            : static_cast<int>(appState.fit_state.snapshot_trace.size()) - 1;

        updateFittedCurve(appState, appState.fit_state.solver->bestFullParams());
        appState.fit_state.state = result.converged ? SolverState::Converged : SolverState::MaxIter;
        appState.log(LogLevel::Info, "Fit finished: " + solverStateName(appState.fit_state.state) +
            ", iter=" + std::to_string(result.iterations));
    }
    catch (const std::exception& exception)
    {
        appState.fit_state.state = SolverState::Failed;
        appState.fit_state.status_message = exception.what();
        appState.log(LogLevel::Error, exception.what());
    }
}

void appRunMultiFit(AppState& appState)
{
    if (!appState.iv_data.has_model)
        appGenerateIVCurve(appState);

    appState.multi_fit.cases.clear();
    const bool useNoisySweep = appState.multi_fit.use_noisy && appState.iv_data.has_sweep_noisy;
    const bool useNoisyModel = appState.multi_fit.use_noisy && appState.iv_data.has_noisy;
    const int maxCases = std::max(1, appState.multi_fit.max_cases);

    try
    {
        if (appState.iv_data.has_sweep)
        {
            const std::vector<std::vector<double>>& source = useNoisySweep
                ? appState.iv_data.I_sweep_noisy
                : appState.iv_data.I_sweep;
            const int count = std::min(maxCases, static_cast<int>(source.size()));
            for (int i = 0; i < count; ++i)
            {
                MultiFitCase fitCase;
                fitCase.label = i < static_cast<int>(appState.iv_data.sweep_labels.size())
                    ? appState.iv_data.sweep_labels[i]
                    : "sweep " + std::to_string(i);
                fitCase.label += useNoisySweep ? " noisy" : "";
                const double temperature = i < static_cast<int>(appState.iv_data.sweep_temperatures.size())
                    ? appState.iv_data.sweep_temperatures[i]
                    : appState.model.T;
                const std::vector<double> sigma = sigmaVectorForCurve(appState, source[i]);
                auto solver = makeSolverForData(appState, source[i], sigma, temperature, &fitCase.start_params);
                solver->initSimplex();
                fitCase.result = solver->runUntilConvergence(appState.solver_cfg.max_iter, appState.solver_cfg.chi2_tol);
                fitCase.trace = solver->getTrace();
                fitCase.fitted_curve = fittedCurveFor(appState, solver->bestFullParams(), temperature);
                fitCase.converged = fitCase.result.converged;
                appState.multi_fit.cases.push_back(std::move(fitCase));
            }
        }
        else
        {
            const std::vector<double>& source = useNoisyModel ? appState.iv_data.I_noisy : appState.iv_data.I_model;
            MultiFitCase fitCase;
            fitCase.label = useNoisyModel ? "main noisy" : "main model";
            const std::vector<double> sigma = sigmaVectorForCurve(appState, source);
            auto solver = makeSolverForData(appState, source, sigma, appState.model.T, &fitCase.start_params);
            solver->initSimplex();
            fitCase.result = solver->runUntilConvergence(appState.solver_cfg.max_iter, appState.solver_cfg.chi2_tol);
            fitCase.trace = solver->getTrace();
            fitCase.fitted_curve = fittedCurveFor(appState, solver->bestFullParams(), appState.model.T);
            fitCase.converged = fitCase.result.converged;
            appState.multi_fit.cases.push_back(std::move(fitCase));
        }

        appState.multi_fit.selected = std::clamp(appState.multi_fit.selected, 0, std::max(0, static_cast<int>(appState.multi_fit.cases.size()) - 1));
        if (!appState.multi_fit.cases.empty())
        {
            const MultiFitCase& selected = appState.multi_fit.cases[appState.multi_fit.selected];
            appState.fit_state.snapshot_trace = selected.trace;
            appState.fit_state.last_result = selected.result;
            appState.fit_state.last_start_params = selected.start_params;
            appState.fit_state.trace_current_idx = selected.trace.empty() ? 0 : static_cast<int>(selected.trace.size()) - 1;
        }
        appState.log(LogLevel::Info, "Multi-fit finished: " + std::to_string(appState.multi_fit.cases.size()) + " curves");
    }
    catch (const std::exception& exception)
    {
        appState.fit_state.state = SolverState::Failed;
        appState.fit_state.status_message = exception.what();
        appState.log(LogLevel::Error, exception.what());
    }
}

void appStepFit(AppState& appState, int steps)
{
    if (!appState.fit_state.solver)
        appInitStepSolver(appState);
    if (!appState.fit_state.solver)
        return;

    try
    {
        for (int i = 0; i < steps && appState.fit_state.solver->iteration() < appState.solver_cfg.max_iter; ++i)
            appState.fit_state.solver->step();

        appState.fit_state.snapshot_trace = appState.fit_state.solver->getTrace();
        if (!appState.fit_state.snapshot_trace.empty())
            appState.fit_state.trace_current_idx = static_cast<int>(appState.fit_state.snapshot_trace.size()) - 1;

        updateFittedCurve(appState, appState.fit_state.solver->bestFullParams());
        appState.fit_state.last_result.best_params = appState.fit_state.solver->bestParams();
        appState.fit_state.last_result.chi2_min = appState.fit_state.solver->bestChiSquared();
        appState.fit_state.last_result.reduced_chi2_min =
            appState.fit_state.last_result.chi2_min / reducedChi2Scale(appState.model, appState.iv_data.V.size());
        appState.fit_state.last_result.iterations = appState.fit_state.solver->iteration();
        appState.fit_state.state = appState.fit_state.last_result.reduced_chi2_min < appState.solver_cfg.chi2_tol
            ? SolverState::Converged
            : SolverState::Ready;
    }
    catch (const std::exception& exception)
    {
        appState.fit_state.state = SolverState::Failed;
        appState.fit_state.status_message = exception.what();
        appState.log(LogLevel::Error, exception.what());
    }
}

void appGenerateBatchPreview(AppState& appState)
{
    appState.batch_state.results.clear();
    appState.batch_state.total = std::max(1, appState.batch_cfg.N);
    appState.batch_state.progress = appState.batch_state.total;
    appState.batch_state.running = false;

    std::mt19937 rng(appState.batch_cfg.rng_seed);
    std::uniform_real_distribution<double> scale(0.9, 1.1);
    for (int i = 0; i < appState.batch_state.total; ++i)
    {
        BatchRow row;
        for (const FitParam& param : appState.model.params)
            row.true_params.push_back(param.value * scale(rng));
        row.nm_converged = appState.batch_cfg.run_nm;
        row.sa_converged = appState.batch_cfg.run_sa;
        row.nm_result.chi2_min = std::abs(scale(rng) - 1.0);
        row.sa_result.chi2_min = row.nm_result.chi2_min * 0.8;
        appState.batch_state.results.push_back(row);
    }
    appState.log(LogLevel::Info, "Batch preview generated: " + std::to_string(appState.batch_state.total) + " rows");
}
