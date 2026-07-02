#pragma once

#include "gpu_types.hpp"

#include <string>
#include <vector>

struct GpuDeviceStatus
{
    bool available = false;
    std::string name;
    int compute_major = 0;
    int compute_minor = 0;
    std::string message;
};

struct GpuLambertWValidationResult
{
    bool cuda_available = false;
    bool passed = false;
    int cases_checked = 0;
    double max_abs_error = 0.0;
    double max_residual = 0.0;
    std::string message;
};

struct GpuNumericValidationResult
{
    bool cuda_available = false;
    bool passed = false;
    int cases_checked = 0;
    double max_abs_error = 0.0;
    double max_rel_error = 0.0;
    std::string message;
};

struct GpuMonteCarloRequest
{
    GpuModelType model_type = GpuModelType::Diode4P;
    GpuParamLayout layout;
    GpuNmConfig nm_config;
    std::vector<double> voltages;
    std::vector<double> true_current;
    int n_samples = 0;
    double noise_pct = 0.0;
    unsigned int noise_seed = 42;
};

struct GpuMonteCarloOutput
{
    bool success = false;
    std::string message;
    std::vector<GpuMcResult> results;
    double upload_ms = 0.0;
    double noise_ms = 0.0;
    double simplex_ms = 0.0;
    double download_ms = 0.0;
    double total_ms = 0.0;
};

GpuDeviceStatus gpuQueryDevice();
GpuLambertWValidationResult gpuValidateLambertW();
GpuNumericValidationResult gpuValidateDiodeCurrent();
GpuNumericValidationResult gpuValidateNoise();
GpuNumericValidationResult gpuValidateSimplexOneStep();
GpuNumericValidationResult gpuValidateSimplexFull();
GpuMonteCarloOutput gpuRunMonteCarlo(const GpuMonteCarloRequest& request);
