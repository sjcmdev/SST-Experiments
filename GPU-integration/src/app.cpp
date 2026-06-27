#include "app.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

static constexpr int MAX_PLOT_POINTS = 2048;
static constexpr int MIN_STEPS = 2;
static constexpr int MAX_STEPS = 65536;

static int clampStepCount(int steps)
{
    return std::clamp(steps, MIN_STEPS, MAX_STEPS);
}

static double sampleGaussian(const GaussianSignalParams &params, double t)
{
    const double sigma = std::max(params.sigma, 1e-6);
    const double x = t - params.center;
    return params.amplitude * std::exp(-(x * x) / (2.0 * sigma * sigma));
}

static double sampleRectangle(const RectangleSignalParams &params, double t)
{
    const double start = std::min(params.start, params.end);
    const double end = std::max(params.start, params.end);
    return (t >= start && t <= end) ? params.amplitude : 0.0;
}

static void generateSignalBuffers(
    const AppState &appState,
    int steps,
    std::vector<double> &timeAxis,
    std::vector<double> &signalA,
    std::vector<double> &signalB)
{
    const double dt = appState.T / static_cast<double>(steps - 1);

    timeAxis.assign(steps, 0.0);
    signalA.assign(steps, 0.0);
    signalB.assign(steps, 0.0);

    for (int i = 0; i < steps; ++i)
    {
        const double t = i * dt;
        timeAxis[i] = t;
        signalA[i] = sampleGaussian(appState.signalAParams, t);
        signalB[i] = sampleRectangle(appState.signalBParams, t);
    }
}

void appInit(AppState &appState)
{
    appState.cudaAvail = queryCudaDevice(appState.deviceInfo);
    appGenerateSignals(appState);
    appState.dataDirty = false;
}

void appGenerateSignals(AppState &appState)
{
    appState.N = clampStepCount(appState.N);
    appState.cpuSteps = clampStepCount(appState.cpuSteps);

    generateSignalBuffers(
        appState,
        appState.N,
        appState.timeAxis,
        appState.signalA,
        appState.signalB);

    generateSignalBuffers(
        appState,
        appState.cpuSteps,
        appState.cpuTimeAxis,
        appState.cpuSignalA,
        appState.cpuSignalB);

    appState.signalC.assign(appState.N, 0.0);
    appState.signalCref.assign(appState.cpuSteps, 0.0);
    appUpdatePlotData(appState);
}

bool appRunComputation(AppState &appState)
{
    if (appState.dataDirty)
    {
        appGenerateSignals(appState);
    }

    appState.lastResult = {};
    appState.lastResult.success = true;
    appState.validationAvailable = false;

    const int cpuSteps = appState.cpuSteps;
    const double cpuDt =
        appState.T / static_cast<double>(cpuSteps - 1);
    auto cpuStart = std::chrono::steady_clock::now();
    for (int n = 0; n < cpuSteps; ++n)
    {
        double sum = 0.0;

        for (int k = 0; k < cpuSteps; ++k)
        {
            const int bIdx = n - k;

            if (bIdx >= 0 && bIdx < cpuSteps)
            {
                sum += appState.cpuSignalA[k] * appState.cpuSignalB[bIdx];
            }
        }

        appState.signalCref[n] = sum * cpuDt;
    }
    auto cpuEnd = std::chrono::steady_clock::now();
    appState.lastResult.cpuReferenceMs = std::chrono::duration<double, std::milli>(
                                             cpuEnd - cpuStart)
                                             .count();

    if (!appState.cudaAvail)
    {
        appState.lastResult.success = false;
        strncpy(appState.lastResult.errorMessage,
                "No CUDA device available.",
                sizeof(appState.lastResult.errorMessage) - 1);
        appState.hasResult = true;
        appState.dataDirty = false;
        appUpdatePlotData(appState);
        return false;
    }

    const bool ok = runConvolution(
        appState.signalA.data(),
        appState.signalB.data(),
        appState.signalC.data(),
        appState.N,
        appState.T / static_cast<double>(appState.N - 1),
        appState.lastResult);
    appState.lastResult.cpuReferenceMs = std::chrono::duration<double, std::milli>(
                                             cpuEnd - cpuStart)
                                             .count();

    if (ok && appState.cpuSteps == appState.N)
    {
        double maxErr = 0.0;
        for (int i = 0; i < appState.N; ++i)
        {
            maxErr = std::max(maxErr, std::abs(appState.signalC[i] - appState.signalCref[i]));
        }
        appState.validationAvailable = true;
        appState.lastResult.maxAbsError = maxErr;
        appState.lastResult.validationPassed = (maxErr < APP_VALIDATION_TOL);
    }

    appState.hasResult = true;
    appState.dataDirty = false;
    appUpdatePlotData(appState);
    return ok;
}

void appMarkDirty(AppState &appState)
{
    appState.dataDirty = true;
    appState.hasResult = false;
    appState.validationAvailable = false;
    appGenerateSignals(appState);
}

void appRecomputeIfDirty(AppState &appState)
{
    if (appState.autoRecomputeDirty && appState.dataDirty)
    {
        appRunComputation(appState);
    }
}

void appUpdatePlotData(AppState &appState)
{
    const int gpuStep = std::max(1, appState.N / MAX_PLOT_POINTS);
    const int gpuCount = (appState.N + gpuStep - 1) / gpuStep;

    appState.plotT.resize(gpuCount);
    appState.plotA.resize(gpuCount);
    appState.plotB.resize(gpuCount);
    appState.plotC.resize(gpuCount);

    for (int i = 0; i < gpuCount; ++i)
    {
        const int idx = std::min(i * gpuStep, appState.N - 1);
        appState.plotT[i] = appState.timeAxis[idx];
        appState.plotA[i] = appState.signalA[idx];
        appState.plotB[i] = appState.signalB[idx];
        appState.plotC[i] = appState.signalC[idx];
    }

    const int cpuStep = std::max(1, appState.cpuSteps / MAX_PLOT_POINTS);
    const int cpuCount = (appState.cpuSteps + cpuStep - 1) / cpuStep;

    appState.plotCpuT.resize(cpuCount);
    appState.plotCref.resize(cpuCount);

    for (int i = 0; i < cpuCount; ++i)
    {
        const int idx = std::min(i * cpuStep, appState.cpuSteps - 1);
        appState.plotCpuT[i] = appState.cpuTimeAxis[idx];
        appState.plotCref[i] = appState.signalCref[idx];
    }
}
