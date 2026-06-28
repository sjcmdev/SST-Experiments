#pragma once

#include <cstddef>
#include <string>
#include <vector>

using KernelHandle = void*;
struct PipelineResult
{
    bool success = false;
    std::string errorMessage;

    // Timing fazy generate (ms)
    float genAMs = 0.0f;
    float genBMs = 0.0f;
    float convMs = 0.0f;
    // Readback
    float readbackMs = 0.0f;
    // Walidacja
    float maxAbsError = 0.0f;
};
struct NvrtcCompileResult {
    bool success = false;
    std::string log;
    float compileTimeMs = 0.0f;
};

struct CudaDeviceInfo {
    char name[256];
    size_t totalMemoryBytes;
    int computeCapabilityMajor;
    int computeCapabilityMinor;
    bool supportsDouble;
};

struct ConvolutionResult {
    bool success;
    char errorMessage[512];

    float transferToGpuMs;
    float kernelMs;
    float transferFromGpuMs;

    double cpuReferenceMs;

    double maxAbsError;
    bool validationPassed;
};

struct AsyncConvResult
{
    PipelineResult info;         // timing, maxAbsError, success
    std::vector<double> signalA; // odczyt GPU Signal A (dla ImPlot)
    std::vector<double> signalB; // odczyt GPU Signal B (dla ImPlot)
    std::vector<double> convOut; // wynik splotu GPU (dla ImPlot)
    std::vector<double> cpuSignalA;
    std::vector<double> cpuSignalB;
    std::vector<double> cpuConvOut;
    double cpuReferenceMs = 0.0;
    bool validationAvailable = false;
};

bool queryCudaDevice(CudaDeviceInfo& info);

bool initCudaDriver();

void runConvolution(
    const double* inputA,
    const double* inputB,
    double* outputC,
    int N,
    double dt,
    KernelHandle kernelFunc,
    ConvolutionResult& result
);

void runPipeline(
    KernelHandle genAFunc,
    KernelHandle genBFunc,
    KernelHandle convFunc,
    int N,
    double dt,
    double *signalA_out, // CPU buffer (N elementy)
    double *signalB_out, // CPU buffer (N elementy)
    double *convOut,     // CPU buffer (N elementy)
    PipelineResult &result);
