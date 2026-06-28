#pragma once

#include <cstddef>
#include <string>
#include <vector>

using KernelHandle = void*;

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

struct AsyncConvResult {
    ConvolutionResult info = {};
    std::vector<double> gpuOutput;
    std::vector<double> cpuOutput;
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
