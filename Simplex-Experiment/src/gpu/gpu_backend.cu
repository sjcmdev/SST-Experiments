#include "gpu_backend.hpp"

#include "diode_model_device.cuh"

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace
{
struct LambertWCase
{
    double input;
    double expected;
    double tolerance;
};

__global__ void validateLambertWKernel(const double* inputs, double* outputs, double* residuals, int count)
{
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count)
        return;

    const double x = inputs[index];
    const double w = LambertW0_d(x);
    outputs[index] = w;
    if (isnan(w))
    {
        residuals[index] = 0.0;
        return;
    }

    const double residual = w * exp(w) - x;
    const double scale = fmax(1.0, fabs(x));
    residuals[index] = fabs(residual) / scale;
}

std::string cudaErrorMessage(cudaError_t error)
{
    return std::string(cudaGetErrorString(error));
}
}

GpuDeviceStatus gpuQueryDevice()
{
    GpuDeviceStatus status;

    int count = 0;
    cudaError_t error = cudaGetDeviceCount(&count);
    if (error != cudaSuccess || count <= 0)
    {
        status.message = "CUDA unavailable: " + cudaErrorMessage(error);
        return status;
    }

    cudaDeviceProp props{};
    error = cudaGetDeviceProperties(&props, 0);
    if (error != cudaSuccess)
    {
        status.message = "cudaGetDeviceProperties failed: " + cudaErrorMessage(error);
        return status;
    }

    status.available = true;
    status.name = props.name;
    status.compute_major = props.major;
    status.compute_minor = props.minor;
    status.message = "CUDA device ready";
    return status;
}

GpuLambertWValidationResult gpuValidateLambertW()
{
    GpuLambertWValidationResult result;
    const GpuDeviceStatus device = gpuQueryDevice();
    result.cuda_available = device.available;
    if (!device.available)
    {
        result.message = device.message;
        return result;
    }

    constexpr double kE = 2.71828182845904523536;
    constexpr double kMinusInvE = -0.36787944117144232159;
    const LambertWCase cases[] = {
        {0.0, 0.0, 1e-14},
        {1.0e-10, 9.999999999e-11, 1e-18},
        {1.0e-4, 9.999000149973338e-5, 1e-14},
        {0.01, 0.009901473843595012, 1e-14},
        {1.0, 0.56714329040978387299, 1e-13},
        {kE, 1.0, 1e-13},
        {100.0, 3.38563014029005, 1e-12},
        {1.0e6, 11.3833580861401, 1e-10},
        {kMinusInvE, -1.0, 2e-8},
        {kMinusInvE - 1e-12, 0.0, 0.0},
    };

    const int count = static_cast<int>(sizeof(cases) / sizeof(cases[0]));
    std::vector<double> inputs(count);
    std::vector<double> outputs(count);
    std::vector<double> residuals(count);
    for (int i = 0; i < count; ++i)
        inputs[i] = cases[i].input;

    double* deviceInputs = nullptr;
    double* deviceOutputs = nullptr;
    double* deviceResiduals = nullptr;
    const size_t bytes = static_cast<size_t>(count) * sizeof(double);

    cudaError_t error = cudaMalloc(&deviceInputs, bytes);
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMalloc(&deviceOutputs, bytes);
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMalloc(&deviceResiduals, bytes);
    if (error != cudaSuccess)
        goto cuda_failure;

    error = cudaMemcpy(deviceInputs, inputs.data(), bytes, cudaMemcpyHostToDevice);
    if (error != cudaSuccess)
        goto cuda_failure;

    validateLambertWKernel<<<1, 32>>>(deviceInputs, deviceOutputs, deviceResiduals, count);
    error = cudaGetLastError();
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaDeviceSynchronize();
    if (error != cudaSuccess)
        goto cuda_failure;

    error = cudaMemcpy(outputs.data(), deviceOutputs, bytes, cudaMemcpyDeviceToHost);
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMemcpy(residuals.data(), deviceResiduals, bytes, cudaMemcpyDeviceToHost);
    if (error != cudaSuccess)
        goto cuda_failure;

    result.cases_checked = count;
    result.passed = true;
    for (int i = 0; i < count; ++i)
    {
        const bool expectNan = cases[i].input < kMinusInvE;
        if (expectNan)
        {
            if (!std::isnan(outputs[i]))
                result.passed = false;
            continue;
        }

        const double absError = std::abs(outputs[i] - cases[i].expected);
        result.max_abs_error = std::max(result.max_abs_error, absError);
        result.max_residual = std::max(result.max_residual, residuals[i]);
        if (absError > cases[i].tolerance || residuals[i] > 1e-12)
            result.passed = false;
    }

    {
        std::ostringstream stream;
        stream << (result.passed ? "GPU LambertW validation PASS" : "GPU LambertW validation FAIL")
               << ": cases=" << result.cases_checked
               << ", max_abs_error=" << result.max_abs_error
               << ", max_residual=" << result.max_residual;
        result.message = stream.str();
    }

    cudaFree(deviceInputs);
    cudaFree(deviceOutputs);
    cudaFree(deviceResiduals);
    return result;

cuda_failure:
    result.message = "CUDA LambertW validation failed: " + cudaErrorMessage(error);
    if (deviceInputs)
        cudaFree(deviceInputs);
    if (deviceOutputs)
        cudaFree(deviceOutputs);
    if (deviceResiduals)
        cudaFree(deviceResiduals);
    return result;
}

