#include "cuda_interface.hpp"

#include <cuda.h>
#include <cuda_runtime.h>

#include <cstdio>
#include <cstring>

static CUdevice g_cudaDevice = 0;
static CUcontext g_cudaPrimaryContext = nullptr;

#define CUDA_CHECK(call)                                                     \
    do {                                                                     \
        cudaError_t error = (call);                                          \
        if (error != cudaSuccess) {                                          \
            snprintf(result.errorMessage, sizeof(result.errorMessage),       \
                     "CUDA error at %s:%d -> %s",                           \
                     __FILE__, __LINE__, cudaGetErrorString(error));         \
            result.success = false;                                         \
            goto cuda_cleanup;                                              \
        }                                                                    \
    } while (0)

#define CU_CHECK(call)                                                       \
    do {                                                                     \
        CUresult error = (call);                                             \
        if (error != CUDA_SUCCESS) {                                         \
            const char* message = "unknown";                                \
            cuGetErrorString(error, &message);                               \
            snprintf(result.errorMessage, sizeof(result.errorMessage),       \
                     "CUDA Driver error at %s:%d -> %s",                    \
                     __FILE__, __LINE__, message);                           \
            result.success = false;                                         \
            goto cuda_cleanup;                                              \
        }                                                                    \
    } while (0)

bool queryCudaDevice(CudaDeviceInfo& info)
{
    memset(&info, 0, sizeof(info));

    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess || count == 0) {
        return false;
    }

    cudaDeviceProp props;
    if (cudaGetDeviceProperties(&props, 0) != cudaSuccess) {
        return false;
    }

    strncpy(info.name, props.name, sizeof(info.name) - 1);
    info.totalMemoryBytes = props.totalGlobalMem;
    info.computeCapabilityMajor = props.major;
    info.computeCapabilityMinor = props.minor;
    info.supportsDouble = (props.major >= 2);

    return true;
}

bool initCudaDriver()
{
    CUresult driverResult = cuInit(0);
    if (driverResult != CUDA_SUCCESS) {
        const char* message = "unknown";
        cuGetErrorString(driverResult, &message);
        fprintf(stderr, "[CU] cuInit(0) failed: %s\n", message);
        return false;
    }

    driverResult = cuDeviceGet(&g_cudaDevice, 0);
    if (driverResult != CUDA_SUCCESS) {
        const char* message = "unknown";
        cuGetErrorString(driverResult, &message);
        fprintf(stderr, "[CU] cuDeviceGet(0) failed: %s\n", message);
        return false;
    }

    if (!g_cudaPrimaryContext) {
        driverResult = cuDevicePrimaryCtxRetain(&g_cudaPrimaryContext, g_cudaDevice);
        if (driverResult != CUDA_SUCCESS) {
            const char* message = "unknown";
            cuGetErrorString(driverResult, &message);
            fprintf(stderr, "[CU] cuDevicePrimaryCtxRetain failed: %s\n", message);
            return false;
        }
    }

    driverResult = cuCtxSetCurrent(g_cudaPrimaryContext);
    if (driverResult != CUDA_SUCCESS) {
        const char* message = "unknown";
        cuGetErrorString(driverResult, &message);
        fprintf(stderr, "[CU] cuCtxSetCurrent failed: %s\n", message);
        return false;
    }

    return true;
}

void runConvolution(
    const double* inputA,
    const double* inputB,
    double* outputC,
    int N,
    double dt,
    KernelHandle kernelFunc,
    ConvolutionResult& result)
{
    memset(&result, 0, sizeof(result));
    result.success = true;

    if (!kernelFunc) {
        result.success = false;
        snprintf(result.errorMessage, sizeof(result.errorMessage),
                 "Kernel not ready. Compile first.");
        return;
    }

    double* d_A = nullptr;
    double* d_B = nullptr;
    double* d_C = nullptr;

    cudaEvent_t evH2DStart = nullptr;
    cudaEvent_t evH2DStop = nullptr;
    cudaEvent_t evKernelStart = nullptr;
    cudaEvent_t evKernelStop = nullptr;
    cudaEvent_t evD2HStart = nullptr;
    cudaEvent_t evD2HStop = nullptr;

    const size_t bytes = static_cast<size_t>(N) * sizeof(double);
    const unsigned int blockSize = 256;
    const unsigned int gridSize = (static_cast<unsigned int>(N) + blockSize - 1) / blockSize;
    CUfunction function = reinterpret_cast<CUfunction>(kernelFunc);
    void* args[] = { &d_A, &d_B, &d_C, &N, &dt };

    CU_CHECK(cuCtxSetCurrent(g_cudaPrimaryContext));

    CUDA_CHECK(cudaMalloc(&d_A, bytes));
    CUDA_CHECK(cudaMalloc(&d_B, bytes));
    CUDA_CHECK(cudaMalloc(&d_C, bytes));

    CUDA_CHECK(cudaEventCreate(&evH2DStart));
    CUDA_CHECK(cudaEventCreate(&evH2DStop));
    CUDA_CHECK(cudaEventCreate(&evKernelStart));
    CUDA_CHECK(cudaEventCreate(&evKernelStop));
    CUDA_CHECK(cudaEventCreate(&evD2HStart));
    CUDA_CHECK(cudaEventCreate(&evD2HStop));

    CUDA_CHECK(cudaEventRecord(evH2DStart, 0));
    CUDA_CHECK(cudaMemcpy(d_A, inputA, bytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_B, inputB, bytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaEventRecord(evH2DStop, 0));
    CUDA_CHECK(cudaEventSynchronize(evH2DStop));
    CUDA_CHECK(cudaEventElapsedTime(&result.transferToGpuMs, evH2DStart, evH2DStop));

    CUDA_CHECK(cudaEventRecord(evKernelStart, 0));
    CU_CHECK(cuLaunchKernel(
        function,
        gridSize, 1, 1,
        blockSize, 1, 1,
        0,
        0,
        args,
        nullptr));
    CUDA_CHECK(cudaEventRecord(evKernelStop, 0));
    CUDA_CHECK(cudaEventSynchronize(evKernelStop));
    CUDA_CHECK(cudaEventElapsedTime(&result.kernelMs, evKernelStart, evKernelStop));

    CUDA_CHECK(cudaEventRecord(evD2HStart, 0));
    CUDA_CHECK(cudaMemcpy(outputC, d_C, bytes, cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaEventRecord(evD2HStop, 0));
    CUDA_CHECK(cudaEventSynchronize(evD2HStop));
    CUDA_CHECK(cudaEventElapsedTime(&result.transferFromGpuMs, evD2HStart, evD2HStop));

cuda_cleanup:
    if (evH2DStart) cudaEventDestroy(evH2DStart);
    if (evH2DStop) cudaEventDestroy(evH2DStop);
    if (evKernelStart) cudaEventDestroy(evKernelStart);
    if (evKernelStop) cudaEventDestroy(evKernelStop);
    if (evD2HStart) cudaEventDestroy(evD2HStart);
    if (evD2HStop) cudaEventDestroy(evD2HStop);
    if (d_A) cudaFree(d_A);
    if (d_B) cudaFree(d_B);
    if (d_C) cudaFree(d_C);
}
