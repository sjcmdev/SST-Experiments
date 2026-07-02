#pragma once

#include <string>

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

GpuDeviceStatus gpuQueryDevice();
GpuLambertWValidationResult gpuValidateLambertW();

