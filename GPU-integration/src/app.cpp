#include "app.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

static constexpr int MAX_PLOT_POINTS = 2048;
static constexpr int MIN_STEPS = 2;
static constexpr int MAX_STEPS = 65536;

static int clampStepCount(int steps)
{
    return std::clamp(steps, MIN_STEPS, MAX_STEPS);
}

static double sampleGaussian(const GaussianSignalParams& params, double t)
{
    const double sigma = std::max(params.sigma, 1e-6);
    const double x = t - params.center;
    return params.amplitude * std::exp(-(x * x) / (2.0 * sigma * sigma));
}

static double sampleRectangle(const RectangleSignalParams& params, double t)
{
    const double start = std::min(params.start, params.end);
    const double end = std::max(params.start, params.end);
    return (t >= start && t <= end) ? params.amplitude : 0.0;
}

static void generateSignalBuffers(
    const AppState& appState,
    int steps,
    std::vector<double>& timeAxis,
    std::vector<double>& signalA,
    std::vector<double>& signalB)
{
    const double dt = appState.T / static_cast<double>(steps - 1);

    timeAxis.assign(steps, 0.0);
    signalA.assign(steps, 0.0);
    signalB.assign(steps, 0.0);

    for (int i = 0; i < steps; ++i) {
        const double t = i * dt;
        timeAxis[i] = t;
        signalA[i] = sampleGaussian(appState.signalAParams, t);
        signalB[i] = sampleRectangle(appState.signalBParams, t);
    }
}

void appInit(AppState& appState)
{
    appState.cudaAvail = queryCudaDevice(appState.deviceInfo);
    appGenerateSignals(appState);
    appState.dataDirty = true;

    if (appState.cudaAvail && !initCudaDriver()) {
        fprintf(stderr, "[App] initCudaDriver() failed - NVRTC unavailable\n");
        appState.cudaAvail = false;
    }

    if (!appState.cudaAvail) {
        return;
    }

    appState.kernelFilePath = "kernels/convolution.cu";
    appState.kernelSource = loadKernelSourceFromFile(appState.kernelFilePath);

    if (appState.kernelSource.empty()) {
        appState.lastCompile.success = false;
        appState.lastCompile.log = "Cannot read kernel file: " + appState.kernelFilePath;
        fprintf(stderr, "[App] Missing kernel file: %s\n", appState.kernelFilePath.c_str());
        return;
    }

    fprintf(stdout, "[App] Compiling startup kernel...\n");
    appState.lastCompile = appState.kernelMgr.compile(
        appState.kernelSource,
        appState.deviceInfo.computeCapabilityMajor,
        appState.deviceInfo.computeCapabilityMinor);

    if (appState.lastCompile.success) {
        fprintf(stdout, "[App] Kernel OK (%.1f ms)\n", appState.lastCompile.compileTimeMs);
    } else {
        fprintf(stderr, "[App] Kernel compile failed:\n%s\n", appState.lastCompile.log.c_str());
    }
}

void appGenerateSignals(AppState& appState)
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

    if (static_cast<int>(appState.signalC.size()) != appState.N) {
        appState.signalC.assign(appState.N, 0.0);
    }
    if (static_cast<int>(appState.signalCref.size()) != appState.cpuSteps) {
        appState.signalCref.assign(appState.cpuSteps, 0.0);
    }
    appUpdatePlotData(appState);
}

static bool appSubmitComputation(AppState& appState)
{
    if (!appState.cudaAvail || !appState.kernelMgr.isReady()) {
        appState.lastResult.success = false;
        const char* message = !appState.cudaAvail
            ? "No CUDA device available."
            : "Kernel not ready. Reload kernel first.";
        strncpy(appState.lastResult.errorMessage, message,
                sizeof(appState.lastResult.errorMessage) - 1);
        appState.hasResult = true;
        appUpdatePlotData(appState);
        return false;
    }

    if (appState.computing) {
        return false;
    }

    appState.gpuFuture = appState.gpuWorker.submit(
        appState.signalA,
        appState.signalB,
        appState.cpuSignalA,
        appState.cpuSignalB,
        appState.N,
        appState.cpuSteps,
        appState.T,
        appState.kernelMgr.getFunction());

    if (!appState.gpuFuture.valid()) {
        appState.lastResult = {};
        appState.lastResult.success = false;
        strncpy(appState.lastResult.errorMessage, "GPU worker is busy.",
                sizeof(appState.lastResult.errorMessage) - 1);
        appState.hasResult = true;
        return false;
    }

    appState.computing = true;
    appState.dataDirty = false;
    return true;
}

bool appRunComputation(AppState& appState)
{
    if (appState.dataDirty) {
        appGenerateSignals(appState);
    }

    appState.dataDirty = true;
    return appSubmitComputation(appState);
}

void appMarkDirty(AppState& appState)
{
    const bool outputSizeChanged =
        static_cast<int>(appState.signalC.size()) != appState.N ||
        static_cast<int>(appState.signalCref.size()) != appState.cpuSteps;

    appState.dataDirty = true;
    if (outputSizeChanged) {
        appState.hasResult = false;
        appState.validationAvailable = false;
    }
    appGenerateSignals(appState);
}

void appRecomputeIfDirty(AppState& appState)
{
    appPollAndSubmit(appState);
}

void appPollAndSubmit(AppState& appState)
{
    if (appState.computing && appState.gpuFuture.valid()) {
        using namespace std::chrono;
        if (appState.gpuFuture.wait_for(milliseconds(0)) == std::future_status::ready) {
            AsyncConvResult asyncResult = appState.gpuFuture.get();

            appState.signalC = std::move(asyncResult.gpuOutput);
            appState.signalCref = std::move(asyncResult.cpuOutput);
            appState.lastResult = asyncResult.info;
            appState.validationAvailable = asyncResult.validationAvailable;
            appState.hasResult = true;
            appState.computing = false;
            appUpdatePlotData(appState);
        }
    }

    if (appState.autoRecomputeDirty &&
        appState.dataDirty &&
        !appState.computing &&
        appState.cudaAvail &&
        appState.kernelMgr.isReady()) {
        appSubmitComputation(appState);
    }
}

void appUpdatePlotData(AppState& appState)
{
    const int gpuStep = std::max(1, appState.N / MAX_PLOT_POINTS);
    const int gpuCount = (appState.N + gpuStep - 1) / gpuStep;

    appState.plotT.resize(gpuCount);
    appState.plotA.resize(gpuCount);
    appState.plotB.resize(gpuCount);
    appState.plotC.resize(gpuCount);

    for (int i = 0; i < gpuCount; ++i) {
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

    for (int i = 0; i < cpuCount; ++i) {
        const int idx = std::min(i * cpuStep, appState.cpuSteps - 1);
        appState.plotCpuT[i] = appState.cpuTimeAxis[idx];
        appState.plotCref[i] = appState.signalCref[idx];
    }
}
