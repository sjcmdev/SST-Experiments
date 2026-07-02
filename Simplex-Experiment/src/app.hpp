#pragma once

#include "gpu/gpu_backend.hpp"
#include "solver/nelder_mead.hpp"
#include "solver/solver_types.hpp"

#include <memory>
#include <string>
#include <vector>

enum class LogLevel
{
    Info,
    Warn,
    Error
};

struct LogEntry
{
    std::string text;
    LogLevel level = LogLevel::Info;
};

struct IVData
{
    std::vector<double> V;
    std::vector<double> I_model;
    std::vector<double> I_noisy;
    std::vector<double> I_fitted;
    std::vector<std::vector<double>> I_sweep;
    std::vector<std::vector<double>> I_sweep_noisy;
    std::vector<std::vector<double>> sweep_params;
    std::vector<double> sweep_temperatures;
    std::vector<std::string> sweep_labels;
    bool has_model = false;
    bool has_noisy = false;
    bool has_fitted = false;
    bool has_sweep = false;
    bool has_sweep_noisy = false;
};

struct ModelConfig
{
    bool use_6param = false;
    double T = 300.0;
    double V_min = -0.5;
    double V_max = 0.7;
    int N_points = 100;
    std::vector<FitParam> params;

    static ModelConfig default4param();
    static ModelConfig default6param();
};

struct NoiseConfig
{
    double sigma = 1e-6;
    double reference_current = 1e-3;
    unsigned int seed = 42;
    int sigma_mode = 0;
    bool relative = false;
    bool live_update = false;
};

struct SolverConfig
{
    double nm_alpha = 1.0;
    double nm_gamma = 2.0;
    double nm_rho = 0.5;
    double nm_sigma = 0.5;
    double degenerate_tol = 1e-12;
    bool sa_enabled = false;
    SAConfig sa_config;
    unsigned int rng_seed = 0;
    bool trace_enabled = true;
    int trace_limit = 10000;
    int max_iter = 5000;
    double chi2_tol = 1.0;
};

struct SweepConfig
{
    bool temperature_enabled = false;
    double T_min = 250.0;
    double T_max = 350.0;
    double T_step = 25.0;
    bool parameter_enabled = false;
    int parameter_index = 1;
    double parameter_min = 1.0;
    double parameter_max = 2.0;
    double parameter_step = 0.25;
    int temperature_relation = 0;
    double T_ref = 300.0;
    double A_slope_per_K = 0.0;
    double log10_I0_slope_per_K = 0.0;
    int max_curves = 128;
};

struct DataImportState
{
    std::vector<std::string> files;
    int selected_index = 0;
    int current_column = 1;
    std::string status;
};

enum class SolverState
{
    Idle,
    Ready,
    Running,
    Converged,
    MaxIter,
    Failed
};

struct SingleFitState
{
    SolverState state = SolverState::Idle;
    FitResult last_result;
    std::vector<double> last_start_params;
    int trace_current_idx = 0;
    bool trace_play = false;
    float trace_play_speed = 5.0f;
    std::string status_message;
    std::unique_ptr<SANelderMead> solver;
    std::vector<TraceStep> snapshot_trace;
};

struct MultiFitCase
{
    std::string label;
    FitResult result;
    std::vector<double> start_params;
    std::vector<double> fitted_curve;
    std::vector<TraceStep> trace;
    bool converged = false;
};

struct MultiFitState
{
    std::vector<MultiFitCase> cases;
    int selected = 0;
    bool use_noisy = true;
    int max_cases = 32;
};

struct BatchRow
{
    std::vector<double> true_params;
    bool nm_converged = false;
    bool sa_converged = false;
    FitResult nm_result;
    FitResult sa_result;
};

struct BatchConfig
{
    int N = 100;
    unsigned int rng_seed = 12345;
    bool run_nm = true;
    bool run_sa = true;
    int max_iter = 3000;
    double chi2_tol = 1.0;
};

struct BatchState
{
    std::vector<BatchRow> results;
    int progress = 0;
    int total = 0;
    bool running = false;
    double elapsed_s = 0.0;
};

struct GpuState
{
    GpuDeviceStatus device;
    GpuLambertWValidationResult lambertw_validation;
    GpuNumericValidationResult current_validation;
    GpuNumericValidationResult noise_validation;
    GpuNumericValidationResult simplex_validation;
    GpuNumericValidationResult simplex_full_validation;
    bool validation_ran = false;
};

struct AppState
{
    int active_tab = 0;
    bool plot_log_y = false;
    bool plot_autoscale = true;
    bool log_auto_scroll = true;
    bool show_simplex_2d = true;
    bool history_log_y = true;
    bool history_log_x = false;
    bool plot_auto_fit_live = true;
    float plot_auto_fit_margin_wheels = 1.0f;
    int simplex_axis_x = 0;
    int simplex_axis_y = 1;

    ModelConfig model;
    NoiseConfig noise;
    SolverConfig solver_cfg;
    SweepConfig sweep_cfg;
    DataImportState data_import;
    IVData iv_data;
    SingleFitState fit_state;
    MultiFitState multi_fit;
    BatchConfig batch_cfg;
    BatchState batch_state;
    GpuState gpu_state;
    std::vector<LogEntry> log_entries;

    void log(LogLevel level, const std::string& message);
};

void appInit(AppState& appState);
void appGenerateIVCurve(AppState& appState);
void appGenerateSweepCurves(AppState& appState);
void appRefreshDataFiles(AppState& appState);
void appLoadSelectedDataFile(AppState& appState);
void appLoadAllDataFiles(AppState& appState);
void appAddNoise(AppState& appState);
void appClearCurves(AppState& appState);
void appRunFit(AppState& appState);
void appRunMultiFit(AppState& appState);
void appInitStepSolver(AppState& appState);
void appStepFit(AppState& appState, int steps);
void appGenerateBatchPreview(AppState& appState);
void appInitGpu(AppState& appState);
void appValidateGpuLambertW(AppState& appState);
void appValidateGpuCore(AppState& appState);
