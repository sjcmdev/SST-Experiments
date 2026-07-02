#include "gpu_backend.hpp"

#include "diode_model_device.cuh"

#include "solver/diode_model.hpp"
#include "solver/diode_objective.hpp"
#include "solver/nelder_mead.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <vector>

namespace
{
double hostTimeMs()
{
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
}

struct LambertWCase
{
    double input;
    double expected;
    double tolerance;
};

__device__ __forceinline__ uint64_t splitmix64(uint64_t value)
{
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

__host__ __device__ __forceinline__ double unitFromBits(uint64_t bits)
{
    return ((bits >> 11) + 1.0) * (1.0 / 9007199254740993.0);
}

__host__ __device__ __forceinline__ double deterministicNormal(uint64_t seed, int index)
{
    const uint64_t base = seed + static_cast<uint64_t>(index) * 0x9E3779B97F4A7C15ULL;
#ifdef __CUDA_ARCH__
    const double u1 = unitFromBits(splitmix64(base));
    const double u2 = unitFromBits(splitmix64(base + 0xD1B54A32D192ED03ULL));
    return sqrt(-2.0 * log(u1)) * cos(6.28318530717958647692 * u2);
#else
    auto splitmix64Host = [](uint64_t value) {
        value += 0x9E3779B97F4A7C15ULL;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
        return value ^ (value >> 31);
    };
    const double u1 = unitFromBits(splitmix64Host(base));
    const double u2 = unitFromBits(splitmix64Host(base + 0xD1B54A32D192ED03ULL));
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.28318530717958647692 * u2);
#endif
}

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

__global__ void evaluateCurrentKernel(GpuModelType model, const double* voltages, const double* params, double temperature, double* output, int count)
{
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count)
        return;
    output[index] = model == GpuModelType::Diode6P
        ? evaluateDiodeIV6_d(voltages[index], params, temperature)
        : evaluateDiodeIV4_d(voltages[index], params, temperature);
}

__global__ void addPercentNoiseKernel(const double* input, double* output, double noise_pct, uint64_t seed, int count)
{
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count)
        return;
    const double noise = input[index] * noise_pct / 100.0;
    output[index] = input[index] + deterministicNormal(seed, index) * noise;
}

__global__ void addPercentNoiseMcKernel(
    const double* input,
    const double* sigma,
    double* output,
    uint64_t seed,
    int point_count,
    int sample_count)
{
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    const int total = point_count * sample_count;
    if (index >= total)
        return;
    const int sample = index / point_count;
    const int point = index - sample * point_count;
    output[index] = input[point] + deterministicNormal(seed + static_cast<uint64_t>(sample) * 0xD1B54A32D192ED03ULL, point) * sigma[point];
}

__device__ __forceinline__ void buildFullParams_d(const GpuParamLayout& layout, const double* free_values, double* full)
{
    for (int i = 0; i < layout.n_total; ++i)
        full[i] = layout.fixed_values[i];
    for (int j = 0; j < layout.n_free; ++j)
        full[layout.free_to_total[j]] = free_values[j];
}

__device__ double evaluateChi2Serial_d(
    GpuModelType model,
    const GpuParamLayout& layout,
    const double* free_values,
    const double* voltages,
    const double* measured,
    const double* sigma,
    int count)
{
    const double kDeviceInf = __longlong_as_double(0x7FF0000000000000ULL);
    double params[GpuParamLayout::MAX_PARAMS] = {};
    buildFullParams_d(layout, free_values, params);

    double chi2 = 0.0;
    for (int i = 0; i < count; ++i)
    {
        if (sigma[i] <= 0.0)
            return kDeviceInf;
        const double current = model == GpuModelType::Diode6P
            ? evaluateDiodeIV6_d(voltages[i], params, layout.T)
            : evaluateDiodeIV4_d(voltages[i], params, layout.T);
        if (!isfinite(current))
            return kDeviceInf;
        const double residual = (measured[i] - current) / sigma[i];
        chi2 += residual * residual;
    }
    return chi2;
}

__device__ __forceinline__ void clipToBounds_d(const GpuParamLayout& layout, double* values)
{
    for (int j = 0; j < layout.n_free; ++j)
    {
        if (!isnan(values[j]))
            values[j] = fmin(fmax(values[j], layout.min_bounds[j]), layout.max_bounds[j]);
    }
}

__device__ __forceinline__ void sortOrder_d(const double* chi2, int count, int* order)
{
    for (int i = 0; i < count; ++i)
        order[i] = i;
    for (int i = 1; i < count; ++i)
    {
        const int key = order[i];
        int j = i - 1;
        while (j >= 0 && chi2[order[j]] > chi2[key])
        {
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = key;
    }
}

__global__ void simplexOneStepKernel(
    GpuModelType model,
    GpuParamLayout layout,
    GpuNmConfig config,
    const double* voltages,
    const double* measured,
    const double* sigma,
    int point_count,
    GpuSimplexStepResult* result)
{
    if (threadIdx.x != 0 || blockIdx.x != 0)
        return;

    double vertices[GpuSimplexStepResult::MAX_VERTICES][GpuSimplexStepResult::MAX_FREE] = {};
    double chi2[GpuSimplexStepResult::MAX_VERTICES] = {};
    int order[GpuSimplexStepResult::MAX_VERTICES] = {};
    const int n_free = layout.n_free;
    const int n_vertices = n_free + 1;

    for (int j = 0; j < n_free; ++j)
        vertices[0][j] = layout.fixed_values[layout.free_to_total[j]];

    for (int vertex = 1; vertex < n_vertices; ++vertex)
    {
        for (int j = 0; j < n_free; ++j)
            vertices[vertex][j] = vertices[0][j];
        const int dim = vertex - 1;
        const double range = layout.max_bounds[dim] - layout.min_bounds[dim];
        const double delta = 0.05 * range;
        double candidate = vertices[0][dim] + delta;
        if (candidate > layout.max_bounds[dim])
            candidate = vertices[0][dim] - delta;
        vertices[vertex][dim] = candidate;
        clipToBounds_d(layout, vertices[vertex]);
    }

    for (int vertex = 0; vertex < n_vertices; ++vertex)
        chi2[vertex] = evaluateChi2Serial_d(model, layout, vertices[vertex], voltages, measured, sigma, point_count);

    sortOrder_d(chi2, n_vertices, order);
    const int best = order[0];
    const int second_worst = order[n_vertices - 2];
    const int worst = order[n_vertices - 1];

    double centroid[GpuSimplexStepResult::MAX_FREE] = {};
    for (int vertex = 0; vertex < n_vertices; ++vertex)
    {
        if (vertex == worst)
            continue;
        for (int j = 0; j < n_free; ++j)
            centroid[j] += vertices[vertex][j];
    }
    for (int j = 0; j < n_free; ++j)
        centroid[j] /= static_cast<double>(n_free);

    double reflected[GpuSimplexStepResult::MAX_FREE] = {};
    for (int j = 0; j < n_free; ++j)
        reflected[j] = centroid[j] + config.alpha * (centroid[j] - vertices[worst][j]);
    clipToBounds_d(layout, reflected);
    const double f_reflected = evaluateChi2Serial_d(model, layout, reflected, voltages, measured, sigma, point_count);

    int step_type = 0;
    if (f_reflected < chi2[best])
    {
        double expanded[GpuSimplexStepResult::MAX_FREE] = {};
        for (int j = 0; j < n_free; ++j)
            expanded[j] = centroid[j] + config.gamma * (reflected[j] - centroid[j]);
        clipToBounds_d(layout, expanded);
        const double f_expanded = evaluateChi2Serial_d(model, layout, expanded, voltages, measured, sigma, point_count);
        if (f_expanded < f_reflected)
        {
            for (int j = 0; j < n_free; ++j)
                vertices[worst][j] = expanded[j];
            chi2[worst] = f_expanded;
            step_type = 1;
        }
        else
        {
            for (int j = 0; j < n_free; ++j)
                vertices[worst][j] = reflected[j];
            chi2[worst] = f_reflected;
            step_type = 0;
        }
    }
    else if (f_reflected < chi2[second_worst])
    {
        for (int j = 0; j < n_free; ++j)
            vertices[worst][j] = reflected[j];
        chi2[worst] = f_reflected;
        step_type = 0;
    }
    else
    {
        double contracted[GpuSimplexStepResult::MAX_FREE] = {};
        for (int j = 0; j < n_free; ++j)
            contracted[j] = centroid[j] + config.rho * (vertices[worst][j] - centroid[j]);
        clipToBounds_d(layout, contracted);
        const double f_contracted = evaluateChi2Serial_d(model, layout, contracted, voltages, measured, sigma, point_count);
        if (f_contracted < chi2[worst])
        {
            for (int j = 0; j < n_free; ++j)
                vertices[worst][j] = contracted[j];
            chi2[worst] = f_contracted;
            step_type = 2;
        }
        else
        {
            for (int vertex = 0; vertex < n_vertices; ++vertex)
            {
                if (vertex == best)
                    continue;
                for (int j = 0; j < n_free; ++j)
                    vertices[vertex][j] = vertices[best][j] + config.sigma_shrink * (vertices[vertex][j] - vertices[best][j]);
                clipToBounds_d(layout, vertices[vertex]);
                chi2[vertex] = evaluateChi2Serial_d(model, layout, vertices[vertex], voltages, measured, sigma, point_count);
            }
            step_type = 3;
        }
    }

    sortOrder_d(chi2, n_vertices, order);
    result->n_free = n_free;
    result->n_vertices = n_vertices;
    result->best_idx = order[0];
    result->worst_idx = order[n_vertices - 1];
    result->step_type = step_type;
    result->iteration = 1;
    for (int vertex = 0; vertex < n_vertices; ++vertex)
    {
        result->chi2[vertex] = chi2[vertex];
        for (int j = 0; j < n_free; ++j)
            result->vertices[vertex][j] = vertices[vertex][j];
    }
}

__global__ void simplexFullKernel(
    GpuModelType model,
    GpuParamLayout layout,
    GpuNmConfig config,
    const double* voltages,
    const double* measured,
    const double* sigma,
    int point_count,
    GpuMcResult* result)
{
    if (threadIdx.x != 0)
        return;

    const int run = blockIdx.x;
    const double* measured_run = measured + static_cast<size_t>(run) * point_count;
    GpuMcResult* out = result + run;
    double vertices[GpuSimplexStepResult::MAX_VERTICES][GpuSimplexStepResult::MAX_FREE] = {};
    double chi2[GpuSimplexStepResult::MAX_VERTICES] = {};
    int order[GpuSimplexStepResult::MAX_VERTICES] = {};
    const int n_free = layout.n_free;
    const int n_vertices = n_free + 1;
    int iteration = 0;

    for (int j = 0; j < n_free; ++j)
        vertices[0][j] = layout.fixed_values[layout.free_to_total[j]];

    for (int vertex = 1; vertex < n_vertices; ++vertex)
    {
        for (int j = 0; j < n_free; ++j)
            vertices[vertex][j] = vertices[0][j];
        const int dim = vertex - 1;
        const double range = layout.max_bounds[dim] - layout.min_bounds[dim];
        const double delta = 0.05 * range;
        double candidate = vertices[0][dim] + delta;
        if (candidate > layout.max_bounds[dim])
            candidate = vertices[0][dim] - delta;
        vertices[vertex][dim] = candidate;
        clipToBounds_d(layout, vertices[vertex]);
    }

    for (int vertex = 0; vertex < n_vertices; ++vertex)
        chi2[vertex] = evaluateChi2Serial_d(model, layout, vertices[vertex], voltages, measured_run, sigma, point_count);
    const double ref_reduced_chi2 = chi2[0] / static_cast<double>(layout.dof);

    for (; iteration < config.max_iter; ++iteration)
    {
        sortOrder_d(chi2, n_vertices, order);
        const int best = order[0];
        if (chi2[best] / static_cast<double>(layout.dof) < config.reduced_chi2_tol)
            break;

        const int second_worst = order[n_vertices - 2];
        const int worst = order[n_vertices - 1];

        double maxDistanceSquared = 0.0;
        for (int a = 0; a < n_vertices; ++a)
        {
            for (int b = a + 1; b < n_vertices; ++b)
            {
                double distanceSquared = 0.0;
                for (int j = 0; j < n_free; ++j)
                {
                    const double delta = vertices[a][j] - vertices[b][j];
                    distanceSquared += delta * delta;
                }
                maxDistanceSquared = fmax(maxDistanceSquared, distanceSquared);
            }
        }
        if (sqrt(maxDistanceSquared) < config.degenerate_tol)
        {
            const double bestPoint[GpuSimplexStepResult::MAX_FREE] = {
                vertices[best][0], vertices[best][1], vertices[best][2], vertices[best][3],
                vertices[best][4], vertices[best][5], vertices[best][6], vertices[best][7]
            };
            for (int j = 0; j < n_free; ++j)
                vertices[0][j] = bestPoint[j];
            for (int vertex = 1; vertex < n_vertices; ++vertex)
            {
                for (int j = 0; j < n_free; ++j)
                    vertices[vertex][j] = bestPoint[j];
                const int dim = vertex - 1;
                const double range = layout.max_bounds[dim] - layout.min_bounds[dim];
                const double delta = 0.05 * range;
                double candidate = bestPoint[dim] + delta;
                if (candidate > layout.max_bounds[dim])
                    candidate = bestPoint[dim] - delta;
                vertices[vertex][dim] = candidate;
                clipToBounds_d(layout, vertices[vertex]);
            }
            for (int vertex = 0; vertex < n_vertices; ++vertex)
                chi2[vertex] = evaluateChi2Serial_d(model, layout, vertices[vertex], voltages, measured_run, sigma, point_count);
            continue;
        }

        double centroid[GpuSimplexStepResult::MAX_FREE] = {};
        for (int vertex = 0; vertex < n_vertices; ++vertex)
        {
            if (vertex == worst)
                continue;
            for (int j = 0; j < n_free; ++j)
                centroid[j] += vertices[vertex][j];
        }
        for (int j = 0; j < n_free; ++j)
            centroid[j] /= static_cast<double>(n_free);

        double reflected[GpuSimplexStepResult::MAX_FREE] = {};
        for (int j = 0; j < n_free; ++j)
            reflected[j] = centroid[j] + config.alpha * (centroid[j] - vertices[worst][j]);
        clipToBounds_d(layout, reflected);
        const double f_reflected = evaluateChi2Serial_d(model, layout, reflected, voltages, measured_run, sigma, point_count);

        if (f_reflected < chi2[best])
        {
            double expanded[GpuSimplexStepResult::MAX_FREE] = {};
            for (int j = 0; j < n_free; ++j)
                expanded[j] = centroid[j] + config.gamma * (reflected[j] - centroid[j]);
            clipToBounds_d(layout, expanded);
            const double f_expanded = evaluateChi2Serial_d(model, layout, expanded, voltages, measured_run, sigma, point_count);
            if (f_expanded < f_reflected)
            {
                for (int j = 0; j < n_free; ++j)
                    vertices[worst][j] = expanded[j];
                chi2[worst] = f_expanded;
            }
            else
            {
                for (int j = 0; j < n_free; ++j)
                    vertices[worst][j] = reflected[j];
                chi2[worst] = f_reflected;
            }
        }
        else if (f_reflected < chi2[second_worst])
        {
            for (int j = 0; j < n_free; ++j)
                vertices[worst][j] = reflected[j];
            chi2[worst] = f_reflected;
        }
        else
        {
            double contracted[GpuSimplexStepResult::MAX_FREE] = {};
            for (int j = 0; j < n_free; ++j)
                contracted[j] = centroid[j] + config.rho * (vertices[worst][j] - centroid[j]);
            clipToBounds_d(layout, contracted);
            const double f_contracted = evaluateChi2Serial_d(model, layout, contracted, voltages, measured_run, sigma, point_count);
            if (f_contracted < chi2[worst])
            {
                for (int j = 0; j < n_free; ++j)
                    vertices[worst][j] = contracted[j];
                chi2[worst] = f_contracted;
            }
            else
            {
                for (int vertex = 0; vertex < n_vertices; ++vertex)
                {
                    if (vertex == best)
                        continue;
                    for (int j = 0; j < n_free; ++j)
                        vertices[vertex][j] = vertices[best][j] + config.sigma_shrink * (vertices[vertex][j] - vertices[best][j]);
                    clipToBounds_d(layout, vertices[vertex]);
                    chi2[vertex] = evaluateChi2Serial_d(model, layout, vertices[vertex], voltages, measured_run, sigma, point_count);
                }
            }
        }
    }

    sortOrder_d(chi2, n_vertices, order);
    const int best = order[0];
    for (int j = 0; j < n_free; ++j)
        out->best_free_params[j] = vertices[best][j];
    out->chi2_min = chi2[best];
    out->reduced_chi2_min = chi2[best] / static_cast<double>(layout.dof);
    out->ref_reduced_chi2 = ref_reduced_chi2;
    out->delta_reduced_chi2 = out->reduced_chi2_min - ref_reduced_chi2;
    out->iterations = iteration;
    out->converged = out->reduced_chi2_min < config.reduced_chi2_tol ? 1 : 0;
    out->n_free = n_free;
}

std::string cudaErrorMessage(cudaError_t error)
{
    return std::string(cudaGetErrorString(error));
}

template <typename T>
void freeDevice(T*& pointer)
{
    if (pointer)
    {
        cudaFree(pointer);
        pointer = nullptr;
    }
}

double relativeError(double value, double expected)
{
    return std::abs(value - expected) / std::max(1.0, std::abs(expected));
}

GpuParamLayout makeFourParamLayout(const double* params, const double* min_bounds, const double* max_bounds, int point_count)
{
    GpuParamLayout layout;
    layout.n_total = 4;
    layout.n_free = 4;
    layout.T = 300.0;
    layout.dof = std::max(1, point_count - layout.n_free);
    for (int i = 0; i < 4; ++i)
    {
        layout.free_to_total[i] = i;
        layout.fixed_values[i] = params[i];
        layout.min_bounds[i] = min_bounds[i];
        layout.max_bounds[i] = max_bounds[i];
    }
    return layout;
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

GpuNumericValidationResult gpuValidateDiodeCurrent()
{
    GpuNumericValidationResult result;
    const GpuDeviceStatus device = gpuQueryDevice();
    result.cuda_available = device.available;
    if (!device.available)
    {
        result.message = device.message;
        return result;
    }

    const std::vector<double> voltages = {-0.5, -0.25, 0.0, 0.1, 0.3, 0.5, 0.7};
    const double params4[] = {1e-10, 1.5, 0.1, 1000.0};
    const double params6[] = {1e-10, 1.5, 0.1, 1000.0, 2.0, 5000.0};
    constexpr double temperature = 300.0;
    const int count = static_cast<int>(voltages.size());

    double* deviceVoltages = nullptr;
    double* deviceParams = nullptr;
    double* deviceOutput = nullptr;
    std::vector<double> output(count);
    cudaError_t error = cudaMalloc(&deviceVoltages, voltages.size() * sizeof(double));
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMalloc(&deviceParams, 6 * sizeof(double));
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMalloc(&deviceOutput, voltages.size() * sizeof(double));
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMemcpy(deviceVoltages, voltages.data(), voltages.size() * sizeof(double), cudaMemcpyHostToDevice);
    if (error != cudaSuccess)
        goto cuda_failure;

    result.passed = true;
    for (int modelIndex = 0; modelIndex < 2; ++modelIndex)
    {
        const bool use6 = modelIndex == 1;
        const double* params = use6 ? params6 : params4;
        const size_t paramBytes = (use6 ? 6 : 4) * sizeof(double);
        error = cudaMemcpy(deviceParams, params, paramBytes, cudaMemcpyHostToDevice);
        if (error != cudaSuccess)
            goto cuda_failure;
        evaluateCurrentKernel<<<1, 32>>>(use6 ? GpuModelType::Diode6P : GpuModelType::Diode4P, deviceVoltages, deviceParams, temperature, deviceOutput, count);
        error = cudaGetLastError();
        if (error != cudaSuccess)
            goto cuda_failure;
        error = cudaDeviceSynchronize();
        if (error != cudaSuccess)
            goto cuda_failure;
        error = cudaMemcpy(output.data(), deviceOutput, voltages.size() * sizeof(double), cudaMemcpyDeviceToHost);
        if (error != cudaSuccess)
            goto cuda_failure;

        for (int i = 0; i < count; ++i)
        {
            const double expected = use6
                ? evaluateDiodeIV6(voltages[i], params, temperature)
                : evaluateDiodeIV4(voltages[i], params, temperature);
            const double absError = std::abs(output[i] - expected);
            const double relError = relativeError(output[i], expected);
            result.max_abs_error = std::max(result.max_abs_error, absError);
            result.max_rel_error = std::max(result.max_rel_error, relError);
            result.passed = result.passed && absError < 1e-10 && relError < 1e-9;
            ++result.cases_checked;
        }
    }

    {
        std::ostringstream stream;
        stream << (result.passed ? "GPU diode current validation PASS" : "GPU diode current validation FAIL")
               << ": cases=" << result.cases_checked
               << ", max_abs_error=" << result.max_abs_error
               << ", max_rel_error=" << result.max_rel_error;
        result.message = stream.str();
    }
    freeDevice(deviceVoltages);
    freeDevice(deviceParams);
    freeDevice(deviceOutput);
    return result;

cuda_failure:
    result.message = "CUDA diode current validation failed: " + cudaErrorMessage(error);
    freeDevice(deviceVoltages);
    freeDevice(deviceParams);
    freeDevice(deviceOutput);
    return result;
}

GpuNumericValidationResult gpuValidateNoise()
{
    GpuNumericValidationResult result;
    const GpuDeviceStatus device = gpuQueryDevice();
    result.cuda_available = device.available;
    if (!device.available)
    {
        result.message = device.message;
        return result;
    }

    const std::vector<double> input = {-1e-9, -2e-6, 0.0, 5e-5, 1e-3, 2e-2, 1.0};
    constexpr double noisePct = 7.5;
    constexpr uint64_t seed = 42;
    const int count = static_cast<int>(input.size());
    std::vector<double> expected(count);
    std::vector<double> output(count);
    for (int i = 0; i < count; ++i)
        expected[i] = input[i] + deterministicNormal(seed, i) * (input[i] * noisePct / 100.0);

    double* deviceInput = nullptr;
    double* deviceOutput = nullptr;
    cudaError_t error = cudaMalloc(&deviceInput, input.size() * sizeof(double));
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMalloc(&deviceOutput, input.size() * sizeof(double));
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMemcpy(deviceInput, input.data(), input.size() * sizeof(double), cudaMemcpyHostToDevice);
    if (error != cudaSuccess)
        goto cuda_failure;

    addPercentNoiseKernel<<<1, 32>>>(deviceInput, deviceOutput, noisePct, seed, count);
    error = cudaGetLastError();
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaDeviceSynchronize();
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMemcpy(output.data(), deviceOutput, input.size() * sizeof(double), cudaMemcpyDeviceToHost);
    if (error != cudaSuccess)
        goto cuda_failure;

    result.passed = true;
    result.cases_checked = count;
    for (int i = 0; i < count; ++i)
    {
        const double absError = std::abs(output[i] - expected[i]);
        const double relError = relativeError(output[i], expected[i]);
        result.max_abs_error = std::max(result.max_abs_error, absError);
        result.max_rel_error = std::max(result.max_rel_error, relError);
        result.passed = result.passed && absError < 1e-14 && relError < 1e-12;
    }

    {
        std::ostringstream stream;
        stream << (result.passed ? "GPU noise validation PASS" : "GPU noise validation FAIL")
               << ": cases=" << result.cases_checked
               << ", max_abs_error=" << result.max_abs_error
               << ", max_rel_error=" << result.max_rel_error;
        result.message = stream.str();
    }
    freeDevice(deviceInput);
    freeDevice(deviceOutput);
    return result;

cuda_failure:
    result.message = "CUDA noise validation failed: " + cudaErrorMessage(error);
    freeDevice(deviceInput);
    freeDevice(deviceOutput);
    return result;
}

GpuNumericValidationResult gpuValidateSimplexOneStep()
{
    GpuNumericValidationResult result;
    const GpuDeviceStatus device = gpuQueryDevice();
    result.cuda_available = device.available;
    if (!device.available)
    {
        result.message = device.message;
        return result;
    }

    constexpr int pointCount = 24;
    std::vector<double> voltages(pointCount);
    std::vector<double> measured(pointCount);
    std::vector<double> sigma(pointCount, 1e-2);
    const double trueParams[] = {1.0e-10, 1.5, 0.1, 1000.0};
    double startParams[] = {1.2e-10, 1.45, 0.2, 900.0};
    const double minBounds[] = {1e-15, 0.5, 0.0, 10.0};
    const double maxBounds[] = {1e-5, 3.0, 10.0, 1e6};
    for (int i = 0; i < pointCount; ++i)
    {
        voltages[i] = -0.3 + 0.9 * static_cast<double>(i) / static_cast<double>(pointCount - 1);
        measured[i] = evaluateDiodeIV4(voltages[i], trueParams, 300.0);
    }

    std::vector<FitParam> fitParams = {
        FitParam("I0", startParams[0], minBounds[0], maxBounds[0], true),
        FitParam("A", startParams[1], minBounds[1], maxBounds[1], true),
        FitParam("Rs", startParams[2], minBounds[2], maxBounds[2], true),
        FitParam("Rsh", startParams[3], minBounds[3], maxBounds[3], true),
    };
    auto objective = makeDiodeIVObjective(
        fitParams,
        [](double voltage, const double* params) { return evaluateDiodeIV4(voltage, params, 300.0); },
        voltages,
        measured,
        sigma);
    SANelderMead cpuSolver(fitParams, objective, 1.0, 2.0, 0.5, 0.5, 1e-12, false, SAConfig{}, 0, false, std::max(1, pointCount - 4));
    cpuSolver.initSimplex();
    cpuSolver.step();
    const SimplexState& cpuState = cpuSolver.state();

    GpuParamLayout layout = makeFourParamLayout(startParams, minBounds, maxBounds, pointCount);
    GpuNmConfig config;
    double* deviceVoltages = nullptr;
    double* deviceMeasured = nullptr;
    double* deviceSigma = nullptr;
    GpuSimplexStepResult* deviceResult = nullptr;
    GpuSimplexStepResult gpuResult;
    cudaError_t error = cudaMalloc(&deviceVoltages, voltages.size() * sizeof(double));
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMalloc(&deviceMeasured, measured.size() * sizeof(double));
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMalloc(&deviceSigma, sigma.size() * sizeof(double));
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMalloc(&deviceResult, sizeof(GpuSimplexStepResult));
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMemcpy(deviceVoltages, voltages.data(), voltages.size() * sizeof(double), cudaMemcpyHostToDevice);
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMemcpy(deviceMeasured, measured.data(), measured.size() * sizeof(double), cudaMemcpyHostToDevice);
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMemcpy(deviceSigma, sigma.data(), sigma.size() * sizeof(double), cudaMemcpyHostToDevice);
    if (error != cudaSuccess)
        goto cuda_failure;

    simplexOneStepKernel<<<1, 32>>>(GpuModelType::Diode4P, layout, config, deviceVoltages, deviceMeasured, deviceSigma, pointCount, deviceResult);
    error = cudaGetLastError();
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaDeviceSynchronize();
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMemcpy(&gpuResult, deviceResult, sizeof(GpuSimplexStepResult), cudaMemcpyDeviceToHost);
    if (error != cudaSuccess)
        goto cuda_failure;

    result.passed = true;
    result.cases_checked = gpuResult.n_vertices * (gpuResult.n_free + 1);
    result.passed = result.passed && gpuResult.n_vertices == static_cast<int>(cpuState.vertices.size());
    result.passed = result.passed && gpuResult.n_free == static_cast<int>(cpuState.vertices.front().size());
    result.passed = result.passed && gpuResult.best_idx == cpuState.best_idx;
    result.passed = result.passed && gpuResult.worst_idx == cpuState.worst_idx;
    result.passed = result.passed && gpuResult.iteration == cpuState.iteration;
    for (int vertex = 0; vertex < gpuResult.n_vertices; ++vertex)
    {
        const double chiAbsError = std::abs(gpuResult.chi2[vertex] - cpuState.chi2_values[vertex]);
        const double chiRelError = relativeError(gpuResult.chi2[vertex], cpuState.chi2_values[vertex]);
        result.max_abs_error = std::max(result.max_abs_error, chiAbsError);
        result.max_rel_error = std::max(result.max_rel_error, chiRelError);
        result.passed = result.passed && chiRelError < 1e-7;
        for (int j = 0; j < gpuResult.n_free; ++j)
        {
            const double absError = std::abs(gpuResult.vertices[vertex][j] - cpuState.vertices[vertex][j]);
            const double relError = relativeError(gpuResult.vertices[vertex][j], cpuState.vertices[vertex][j]);
            result.max_abs_error = std::max(result.max_abs_error, absError);
            result.max_rel_error = std::max(result.max_rel_error, relError);
            result.passed = result.passed && absError < 1e-12;
        }
    }

    {
        std::ostringstream stream;
        stream << (result.passed ? "GPU simplex one-step validation PASS" : "GPU simplex one-step validation FAIL")
               << ": cases=" << result.cases_checked
               << ", max_abs_error=" << result.max_abs_error
               << ", max_rel_error=" << result.max_rel_error
               << ", step_type=" << gpuResult.step_type;
        result.message = stream.str();
    }
    freeDevice(deviceVoltages);
    freeDevice(deviceMeasured);
    freeDevice(deviceSigma);
    freeDevice(deviceResult);
    return result;

cuda_failure:
    result.message = "CUDA simplex one-step validation failed: " + cudaErrorMessage(error);
    freeDevice(deviceVoltages);
    freeDevice(deviceMeasured);
    freeDevice(deviceSigma);
    freeDevice(deviceResult);
    return result;
}

GpuNumericValidationResult gpuValidateSimplexFull()
{
    GpuNumericValidationResult result;
    const GpuDeviceStatus device = gpuQueryDevice();
    result.cuda_available = device.available;
    if (!device.available)
    {
        result.message = device.message;
        return result;
    }

    constexpr int pointCount = 24;
    constexpr int maxIter = 24;
    std::vector<double> voltages(pointCount);
    std::vector<double> measured(pointCount);
    std::vector<double> sigma(pointCount, 1e-2);
    const double trueParams[] = {1.0e-10, 1.5, 0.1, 1000.0};
    double startParams[] = {1.2e-10, 1.45, 0.2, 900.0};
    const double minBounds[] = {1e-15, 0.5, 0.0, 10.0};
    const double maxBounds[] = {1e-5, 3.0, 10.0, 1e6};
    for (int i = 0; i < pointCount; ++i)
    {
        voltages[i] = -0.3 + 0.9 * static_cast<double>(i) / static_cast<double>(pointCount - 1);
        measured[i] = evaluateDiodeIV4(voltages[i], trueParams, 300.0);
    }

    std::vector<FitParam> fitParams = {
        FitParam("I0", startParams[0], minBounds[0], maxBounds[0], true),
        FitParam("A", startParams[1], minBounds[1], maxBounds[1], true),
        FitParam("Rs", startParams[2], minBounds[2], maxBounds[2], true),
        FitParam("Rsh", startParams[3], minBounds[3], maxBounds[3], true),
    };
    auto objective = makeDiodeIVObjective(
        fitParams,
        [](double voltage, const double* params) { return evaluateDiodeIV4(voltage, params, 300.0); },
        voltages,
        measured,
        sigma);
    SANelderMead cpuSolver(fitParams, objective, 1.0, 2.0, 0.5, 0.5, 1e-12, false, SAConfig{}, 0, false, std::max(1, pointCount - 4));
    cpuSolver.initSimplex();
    const FitResult cpuResult = cpuSolver.runUntilConvergence(maxIter, 1e-300);

    GpuParamLayout layout = makeFourParamLayout(startParams, minBounds, maxBounds, pointCount);
    GpuNmConfig config;
    config.max_iter = maxIter;
    config.reduced_chi2_tol = 1e-300;

    double* deviceVoltages = nullptr;
    double* deviceMeasured = nullptr;
    double* deviceSigma = nullptr;
    GpuMcResult* deviceResult = nullptr;
    GpuMcResult gpuResult;
    cudaError_t error = cudaMalloc(&deviceVoltages, voltages.size() * sizeof(double));
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMalloc(&deviceMeasured, measured.size() * sizeof(double));
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMalloc(&deviceSigma, sigma.size() * sizeof(double));
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMalloc(&deviceResult, sizeof(GpuMcResult));
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMemcpy(deviceVoltages, voltages.data(), voltages.size() * sizeof(double), cudaMemcpyHostToDevice);
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMemcpy(deviceMeasured, measured.data(), measured.size() * sizeof(double), cudaMemcpyHostToDevice);
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMemcpy(deviceSigma, sigma.data(), sigma.size() * sizeof(double), cudaMemcpyHostToDevice);
    if (error != cudaSuccess)
        goto cuda_failure;

    simplexFullKernel<<<1, 32>>>(GpuModelType::Diode4P, layout, config, deviceVoltages, deviceMeasured, deviceSigma, pointCount, deviceResult);
    error = cudaGetLastError();
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaDeviceSynchronize();
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMemcpy(&gpuResult, deviceResult, sizeof(GpuMcResult), cudaMemcpyDeviceToHost);
    if (error != cudaSuccess)
        goto cuda_failure;

    result.passed = true;
    result.cases_checked = static_cast<int>(cpuResult.best_params.size()) + 3;
    result.passed = result.passed && gpuResult.n_free == static_cast<int>(cpuResult.best_params.size());
    result.passed = result.passed && gpuResult.iterations == cpuResult.iterations;
    {
        const double chiAbsError = std::abs(gpuResult.chi2_min - cpuResult.chi2_min);
        const double chiRelError = relativeError(gpuResult.chi2_min, cpuResult.chi2_min);
        result.max_abs_error = std::max(result.max_abs_error, chiAbsError);
        result.max_rel_error = std::max(result.max_rel_error, chiRelError);
        result.passed = result.passed && chiRelError < 1e-7;
    }
    {
        const double rchiAbsError = std::abs(gpuResult.reduced_chi2_min - cpuResult.reduced_chi2_min);
        const double rchiRelError = relativeError(gpuResult.reduced_chi2_min, cpuResult.reduced_chi2_min);
        result.max_abs_error = std::max(result.max_abs_error, rchiAbsError);
        result.max_rel_error = std::max(result.max_rel_error, rchiRelError);
        result.passed = result.passed && rchiRelError < 1e-7;
    }
    for (int j = 0; j < gpuResult.n_free; ++j)
    {
        const double absError = std::abs(gpuResult.best_free_params[j] - cpuResult.best_params[static_cast<size_t>(j)]);
        const double relError = relativeError(gpuResult.best_free_params[j], cpuResult.best_params[static_cast<size_t>(j)]);
        result.max_abs_error = std::max(result.max_abs_error, absError);
        result.max_rel_error = std::max(result.max_rel_error, relError);
        result.passed = result.passed && relError < 1e-7;
    }

    {
        std::ostringstream stream;
        stream << (result.passed ? "GPU full simplex validation PASS" : "GPU full simplex validation FAIL")
               << ": iterations=" << gpuResult.iterations
               << ", chi2_gpu=" << gpuResult.chi2_min
               << ", chi2_cpu=" << cpuResult.chi2_min
               << ", max_abs_error=" << result.max_abs_error
               << ", max_rel_error=" << result.max_rel_error;
        result.message = stream.str();
    }
    freeDevice(deviceVoltages);
    freeDevice(deviceMeasured);
    freeDevice(deviceSigma);
    freeDevice(deviceResult);
    return result;

cuda_failure:
    result.message = "CUDA full simplex validation failed: " + cudaErrorMessage(error);
    freeDevice(deviceVoltages);
    freeDevice(deviceMeasured);
    freeDevice(deviceSigma);
    freeDevice(deviceResult);
    return result;
}

GpuMonteCarloOutput gpuRunMonteCarlo(const GpuMonteCarloRequest& request)
{
    GpuMonteCarloOutput output;
    const double totalStartMs = hostTimeMs();
    const GpuDeviceStatus device = gpuQueryDevice();
    if (!device.available)
    {
        output.message = device.message;
        return output;
    }
    if (request.n_samples <= 0)
    {
        output.message = "GPU MC: n_samples must be > 0";
        return output;
    }
    if (request.voltages.empty() || request.voltages.size() != request.true_current.size())
    {
        output.message = "GPU MC: voltages and true_current size mismatch";
        return output;
    }
    if (request.layout.n_free <= 0 || request.layout.n_free >= GpuSimplexStepResult::MAX_VERTICES)
    {
        output.message = "GPU MC: invalid free parameter count";
        return output;
    }

    const int pointCount = static_cast<int>(request.voltages.size());
    const int sampleCount = request.n_samples;
    const size_t pointBytes = request.voltages.size() * sizeof(double);
    const size_t noisyBytes = static_cast<size_t>(pointCount) * static_cast<size_t>(sampleCount) * sizeof(double);
    std::vector<double> sigma(request.true_current.size());
    const double noiseFactor = std::clamp(request.noise_pct, 0.0, 100.0) / 100.0;
    for (size_t i = 0; i < request.true_current.size(); ++i)
        sigma[i] = std::max(std::abs(request.true_current[i]) * noiseFactor, 1e-30);

    double* deviceVoltages = nullptr;
    double* deviceTrueCurrent = nullptr;
    double* deviceSigma = nullptr;
    double* deviceNoisy = nullptr;
    GpuMcResult* deviceResults = nullptr;
    double stageStartMs = hostTimeMs();
    cudaError_t error = cudaMalloc(&deviceVoltages, pointBytes);
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMalloc(&deviceTrueCurrent, pointBytes);
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMalloc(&deviceSigma, pointBytes);
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMalloc(&deviceNoisy, noisyBytes);
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMalloc(&deviceResults, static_cast<size_t>(sampleCount) * sizeof(GpuMcResult));
    if (error != cudaSuccess)
        goto cuda_failure;

    error = cudaMemcpy(deviceVoltages, request.voltages.data(), pointBytes, cudaMemcpyHostToDevice);
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMemcpy(deviceTrueCurrent, request.true_current.data(), pointBytes, cudaMemcpyHostToDevice);
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaMemcpy(deviceSigma, sigma.data(), pointBytes, cudaMemcpyHostToDevice);
    if (error != cudaSuccess)
        goto cuda_failure;
    output.upload_ms = hostTimeMs() - stageStartMs;

    {
        stageStartMs = hostTimeMs();
        const int blockSize = 256;
        const int total = pointCount * sampleCount;
        const int gridSize = (total + blockSize - 1) / blockSize;
        addPercentNoiseMcKernel<<<gridSize, blockSize>>>(
            deviceTrueCurrent,
            deviceSigma,
            deviceNoisy,
            static_cast<uint64_t>(request.noise_seed),
            pointCount,
            sampleCount);
        error = cudaGetLastError();
        if (error != cudaSuccess)
            goto cuda_failure;
        error = cudaDeviceSynchronize();
        if (error != cudaSuccess)
            goto cuda_failure;
        output.noise_ms = hostTimeMs() - stageStartMs;
    }

    stageStartMs = hostTimeMs();
    simplexFullKernel<<<sampleCount, 32>>>(
        request.model_type,
        request.layout,
        request.nm_config,
        deviceVoltages,
        deviceNoisy,
        deviceSigma,
        pointCount,
        deviceResults);
    error = cudaGetLastError();
    if (error != cudaSuccess)
        goto cuda_failure;
    error = cudaDeviceSynchronize();
    if (error != cudaSuccess)
        goto cuda_failure;
    output.simplex_ms = hostTimeMs() - stageStartMs;

    stageStartMs = hostTimeMs();
    output.results.resize(static_cast<size_t>(sampleCount));
    error = cudaMemcpy(output.results.data(), deviceResults, static_cast<size_t>(sampleCount) * sizeof(GpuMcResult), cudaMemcpyDeviceToHost);
    if (error != cudaSuccess)
        goto cuda_failure;
    output.download_ms = hostTimeMs() - stageStartMs;

    output.success = true;
    output.total_ms = hostTimeMs() - totalStartMs;
    output.message = "GPU MC finished: samples=" + std::to_string(sampleCount);
    freeDevice(deviceVoltages);
    freeDevice(deviceTrueCurrent);
    freeDevice(deviceSigma);
    freeDevice(deviceNoisy);
    freeDevice(deviceResults);
    return output;

cuda_failure:
    output.success = false;
    output.total_ms = hostTimeMs() - totalStartMs;
    output.message = "GPU MC failed: " + cudaErrorMessage(error);
    freeDevice(deviceVoltages);
    freeDevice(deviceTrueCurrent);
    freeDevice(deviceSigma);
    freeDevice(deviceNoisy);
    freeDevice(deviceResults);
    return output;
}
