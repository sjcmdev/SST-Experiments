#include "app.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

static constexpr int MAX_PLOT_POINTS = 2048;
static constexpr int MIN_STEPS = 2;
static constexpr int MAX_STEPS = 65536;

static int clampStepCount(int steps)
{
    return std::clamp(steps, MIN_STEPS, MAX_STEPS);
}

static void resizePlotSource(AppState& appState)
{
    appState.timeAxis.resize(appState.N);
    const double dt = appState.T / static_cast<double>(appState.N - 1);
    for (int i = 0; i < appState.N; ++i) {
        appState.timeAxis[i] = static_cast<double>(i) * dt;
    }
}

static void resizeCpuPlotSource(AppState& appState)
{
    appState.cpuTimeAxis.resize(appState.cpuSteps);
    const double cpuDt = appState.T / static_cast<double>(appState.cpuSteps - 1);
    for (int i = 0; i < appState.cpuSteps; ++i) {
        appState.cpuTimeAxis[i] = static_cast<double>(i) * cpuDt;
    }
}

static bool compileConvolutionKernel(AppState& appState)
{
    appState.kernelFilePath = "kernels/convolution.cu";
    appState.kernelSource = loadKernelSourceFromFile(appState.kernelFilePath);

    if (appState.kernelSource.empty()) {
        appState.lastCompile.success = false;
        appState.lastCompile.log = "Cannot read kernel file: " + appState.kernelFilePath;
        return false;
    }

    appState.lastCompile = appState.kernelMgr.compile(
        appState.kernelSource,
        appState.deviceInfo.computeCapabilityMajor,
        appState.deviceInfo.computeCapabilityMinor,
        {"convolution"});

    return appState.lastCompile.success;
}

static bool compileSignalKernels(AppState& appState)
{
    appState.generatedCode = buildSignalModule(appState.graphA, appState.graphB);
    appState.lastSignalCompile = appState.signalKernelMgr.compile(
        appState.generatedCode,
        appState.deviceInfo.computeCapabilityMajor,
        appState.deviceInfo.computeCapabilityMinor,
        {"generateSignalA", "generateSignalB"});

    appState.codeDirty = !appState.lastSignalCompile.success;
    return appState.lastSignalCompile.success;
}

void appInit(AppState& appState)
{
    appState.cudaAvail = queryCudaDevice(appState.deviceInfo);
    appState.N = clampStepCount(appState.N);
    appState.cpuSteps = appState.N;
    appState.dt = appState.T / static_cast<double>(appState.N - 1);

    {
        float params[] = {0.30f, 0.05f};
        appState.graphA.initDefault(NodeType::Gaussian, params);
    }
    {
        float params[] = {0.60f, 0.80f};
        appState.graphB.initDefault(NodeType::Rectangle, params);
    }

    appGenerateSignals(appState);

    if (appState.cudaAvail && !initCudaDriver()) {
        fprintf(stderr, "[App] initCudaDriver() failed - NVRTC unavailable\n");
        appState.cudaAvail = false;
    }

    if (appState.cudaAvail) {
        compileConvolutionKernel(appState);
        compileSignalKernels(appState);
    }

    appState.dataDirty = true;
}

void appGenerateSignals(AppState& appState)
{
    appState.N = clampStepCount(appState.N);
    appState.cpuSteps = clampStepCount(appState.cpuSteps);
    appState.dt = appState.T / static_cast<double>(appState.N - 1);

    resizePlotSource(appState);
    resizeCpuPlotSource(appState);
    appState.signalA.assign(appState.N, 0.0);
    appState.signalB.assign(appState.N, 0.0);
    appState.signalC.assign(appState.N, 0.0);
    appState.convOutput.assign(appState.N, 0.0);
    appState.cpuSignalA.assign(appState.cpuSteps, 0.0);
    appState.cpuSignalB.assign(appState.cpuSteps, 0.0);
    appState.signalCref.assign(appState.cpuSteps, 0.0);
    appUpdatePlotData(appState);
}

static bool submitPipelineTask(AppState& appState)
{
    if (!appState.cudaAvail || !appState.kernelMgr.isReady() || !appState.signalKernelMgr.isReady()) {
        appState.lastResult = {};
        appState.lastResult.success = false;
        const char* message = !appState.cudaAvail
            ? "No CUDA device available."
            : "Kernel not ready. Compile/reload kernels first.";
        strncpy(appState.lastResult.errorMessage, message,
                sizeof(appState.lastResult.errorMessage) - 1);
        appState.hasResult = true;
        return false;
    }

    if (appState.computing) {
        return false;
    }

    GpuTask task;
    task.N = appState.N;
    task.dt = appState.dt;
    task.cpuSteps = appState.cpuSteps;
    task.cpuDt = appState.T / static_cast<double>(appState.cpuSteps - 1);
    task.genAFunc = appState.signalKernelMgr.getFunction("generateSignalA");
    task.genBFunc = appState.signalKernelMgr.getFunction("generateSignalB");
    task.convFunc = appState.kernelMgr.getFunction("convolution");
    task.graphA = appState.graphA;
    task.graphB = appState.graphB;

    appState.gpuFuture = appState.gpuWorker.submitTask(std::move(task));
    appState.computing = appState.gpuFuture.valid();
    if (appState.computing) {
        appState.dataDirty = false;
    }

    return appState.computing;
}

bool appRunComputation(AppState& appState)
{
    if (appState.codeDirty && appState.cudaAvail && !appState.computing) {
        if (!compileSignalKernels(appState)) {
            return false;
        }
    }

    appState.dataDirty = true;
    return submitPipelineTask(appState);
}

void appMarkDirty(AppState& appState)
{
    appState.dataDirty = true;
    appState.codeDirty = true;
    appState.hasResult = false;
    appState.validationAvailable = false;
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
            AsyncConvResult result = appState.gpuFuture.get();

            appState.signalA = std::move(result.signalA);
            appState.signalB = std::move(result.signalB);
            appState.signalC = std::move(result.convOut);
            appState.convOutput = appState.signalC;
            appState.cpuSignalA = std::move(result.cpuSignalA);
            appState.cpuSignalB = std::move(result.cpuSignalB);
            appState.signalCref = std::move(result.cpuConvOut);

            appState.lastResult = {};
            appState.lastResult.success = result.info.success;
            if (!result.info.success) {
                strncpy(appState.lastResult.errorMessage, result.info.errorMessage.c_str(),
                        sizeof(appState.lastResult.errorMessage) - 1);
            }
            appState.lastResult.transferToGpuMs = result.info.genAMs + result.info.genBMs;
            appState.lastResult.kernelMs = result.info.convMs;
            appState.lastResult.transferFromGpuMs = result.info.readbackMs;
            appState.lastResult.cpuReferenceMs = result.cpuReferenceMs;
            appState.lastResult.maxAbsError = result.info.maxAbsError;
            appState.lastResult.validationPassed =
                result.validationAvailable && (result.info.maxAbsError < APP_VALIDATION_TOL);

            appState.validationAvailable = result.info.success && result.validationAvailable;
            appState.hasResult = true;
            appState.computing = false;
            appUpdatePlotData(appState);
        }
    }

    if (appState.dataDirty &&
        appState.autoRecomputeDirty &&
        !appState.computing &&
        appState.cudaAvail) {
        if (appState.codeDirty) {
            if (!compileSignalKernels(appState)) {
                return;
            }
        }
        submitPipelineTask(appState);
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
        appState.plotT[i] = appState.timeAxis.empty() ? 0.0 : appState.timeAxis[idx];
        appState.plotA[i] = appState.signalA.empty() ? 0.0 : appState.signalA[idx];
        appState.plotB[i] = appState.signalB.empty() ? 0.0 : appState.signalB[idx];
        appState.plotC[i] = appState.signalC.empty() ? 0.0 : appState.signalC[idx];
    }

    const int cpuStep = std::max(1, appState.cpuSteps / MAX_PLOT_POINTS);
    const int cpuCount = (appState.cpuSteps + cpuStep - 1) / cpuStep;

    appState.plotCpuT.resize(cpuCount);
    appState.plotCref.resize(cpuCount);

    for (int i = 0; i < cpuCount; ++i) {
        const int idx = std::min(i * cpuStep, appState.cpuSteps - 1);
        appState.plotCpuT[i] = appState.cpuTimeAxis.empty() ? 0.0 : appState.cpuTimeAxis[idx];
        appState.plotCref[i] = appState.signalCref.empty() ? 0.0 : appState.signalCref[idx];
    }
}
