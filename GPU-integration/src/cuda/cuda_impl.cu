#include "cuda_interface.hpp"

#include <cuda.h>
#include <cuda_runtime.h>

#include <cstdio>
#include <cstring>

static CUdevice g_cudaDevice = 0;
static CUcontext g_cudaPrimaryContext = nullptr;
// Makro dla Driver API (CUresult) — NOWE
#define CU_CHECK_LAUNCH(call, resultRef) do {                              \
    CUresult _r = (call);                                                   \
    if (_r != CUDA_SUCCESS) {                                               \
        const char* _s = "?";                                               \
        cuGetErrorString(_r, &_s);                                          \
        fprintf(stderr, "[CU] %s:%d %s → %s\n",                           \
            __FILE__, __LINE__, #call, _s);                                 \
        (resultRef).success = false;                                        \
        (resultRef).errorMessage = std::string(#call) + ": " + _s;        \
        goto cuda_error;                                                    \
    }                                                                       \
} while(0)
#define PIPELINE_CUDA_CHECK(call, resultRef)                         \
    do                                                               \
    {                                                                \
        cudaError_t error = (call);                                  \
        if (error != cudaSuccess)                                    \
        {                                                            \
            char message[512];                                       \
            snprintf(message, sizeof(message),                       \
                     "CUDA Runtime error at %s:%d -> %s",            \
                     __FILE__, __LINE__, cudaGetErrorString(error)); \
            (resultRef).success = false;                             \
            (resultRef).errorMessage = message;                      \
            goto pipeline_cleanup;                                   \
        }                                                            \
    } while (0)

#define PIPELINE_CU_CHECK(call, resultRef)               \
    do                                                   \
    {                                                    \
        CUresult error = (call);                         \
        if (error != CUDA_SUCCESS)                       \
        {                                                \
            const char *driverMessage = "unknown";       \
            cuGetErrorString(error, &driverMessage);     \
            char message[512];                           \
            snprintf(message, sizeof(message),           \
                     "CUDA Driver error at %s:%d -> %s", \
                     __FILE__, __LINE__, driverMessage); \
            (resultRef).success = false;                 \
            (resultRef).errorMessage = message;          \
            goto pipeline_cleanup;                       \
        }                                                \
    } while (0)

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

void runPipeline(
    KernelHandle genAFunc,
    KernelHandle genBFunc,
    KernelHandle convFunc,
    int N,
    double dt,
    double *signalA_out,
    double *signalB_out,
    double *convOut,
    PipelineResult &result)
{
    result.success = false;
    result.errorMessage.clear();
    result.genAMs = 0.0f;
    result.genBMs = 0.0f;
    result.convMs = 0.0f;
    result.readbackMs = 0.0f;

    if (!genAFunc || !genBFunc || !convFunc)
    {
        result.errorMessage = "Jeden lub więcej kerneli nie jest załadowany.";
        return;
    }

    if (!g_cudaPrimaryContext)
    {
        result.errorMessage = "CUDA Driver nie został zainicjalizowany. Wywołaj initCudaDriver().";
        return;
    }

    CUfunction fGenA = reinterpret_cast<CUfunction>(genAFunc);
    CUfunction fGenB = reinterpret_cast<CUfunction>(genBFunc);
    CUfunction fConv = reinterpret_cast<CUfunction>(convFunc);

    const size_t bytes = static_cast<size_t>(N) * sizeof(double);
    const unsigned int block = 256u;
    const unsigned int grid = (static_cast<unsigned int>(N) + block - 1u) / block;

    double *d_A = nullptr;
    double *d_B = nullptr;
    double *d_C = nullptr;

    cudaEvent_t evA0 = nullptr;
    cudaEvent_t evA1 = nullptr;
    cudaEvent_t evB0 = nullptr;
    cudaEvent_t evB1 = nullptr;
    cudaEvent_t evC0 = nullptr;
    cudaEvent_t evC1 = nullptr;
    cudaEvent_t evR0 = nullptr;
    cudaEvent_t evR1 = nullptr;

    PIPELINE_CU_CHECK(cuCtxSetCurrent(g_cudaPrimaryContext), result);

    PIPELINE_CUDA_CHECK(cudaEventCreate(&evA0), result);
    PIPELINE_CUDA_CHECK(cudaEventCreate(&evA1), result);
    PIPELINE_CUDA_CHECK(cudaEventCreate(&evB0), result);
    PIPELINE_CUDA_CHECK(cudaEventCreate(&evB1), result);
    PIPELINE_CUDA_CHECK(cudaEventCreate(&evC0), result);
    PIPELINE_CUDA_CHECK(cudaEventCreate(&evC1), result);
    PIPELINE_CUDA_CHECK(cudaEventCreate(&evR0), result);
    PIPELINE_CUDA_CHECK(cudaEventCreate(&evR1), result);

    PIPELINE_CUDA_CHECK(cudaMalloc(&d_A, bytes), result);
    PIPELINE_CUDA_CHECK(cudaMalloc(&d_B, bytes), result);
    PIPELINE_CUDA_CHECK(cudaMalloc(&d_C, bytes), result);

    // --- Generate Signal A ---
    {
        void *args[] = {&d_A, &N, &dt};
        PIPELINE_CUDA_CHECK(cudaEventRecord(evA0, 0), result);
        PIPELINE_CU_CHECK(
            cuLaunchKernel(fGenA, grid, 1, 1, block, 1, 1, 0, 0, args, nullptr),
            result);
        PIPELINE_CUDA_CHECK(cudaEventRecord(evA1, 0), result);
        PIPELINE_CUDA_CHECK(cudaEventSynchronize(evA1), result);
        PIPELINE_CUDA_CHECK(cudaEventElapsedTime(&result.genAMs, evA0, evA1), result);
    }

    // --- Generate Signal B ---
    {
        void *args[] = {&d_B, &N, &dt};
        PIPELINE_CUDA_CHECK(cudaEventRecord(evB0, 0), result);
        PIPELINE_CU_CHECK(
            cuLaunchKernel(fGenB, grid, 1, 1, block, 1, 1, 0, 0, args, nullptr),
            result);
        PIPELINE_CUDA_CHECK(cudaEventRecord(evB1, 0), result);
        PIPELINE_CUDA_CHECK(cudaEventSynchronize(evB1), result);
        PIPELINE_CUDA_CHECK(cudaEventElapsedTime(&result.genBMs, evB0, evB1), result);
    }

    // --- Convolution ---
    {
        // runConvolution() przekazuje do tego kernela również dt, więc tutaj
        // argumenty muszą mieć tę samą kolejność i liczbę.
        void *args[] = {&d_A, &d_B, &d_C, &N, &dt};
        PIPELINE_CUDA_CHECK(cudaEventRecord(evC0, 0), result);
        PIPELINE_CU_CHECK(
            cuLaunchKernel(fConv, grid, 1, 1, block, 1, 1, 0, 0, args, nullptr),
            result);
        PIPELINE_CUDA_CHECK(cudaEventRecord(evC1, 0), result);
        PIPELINE_CUDA_CHECK(cudaEventSynchronize(evC1), result);
        PIPELINE_CUDA_CHECK(cudaEventElapsedTime(&result.convMs, evC0, evC1), result);
    }

    // --- Readback (GPU -> CPU) ---
    PIPELINE_CUDA_CHECK(cudaEventRecord(evR0, 0), result);
    PIPELINE_CUDA_CHECK(cudaMemcpy(signalA_out, d_A, bytes, cudaMemcpyDeviceToHost), result);
    PIPELINE_CUDA_CHECK(cudaMemcpy(signalB_out, d_B, bytes, cudaMemcpyDeviceToHost), result);
    PIPELINE_CUDA_CHECK(cudaMemcpy(convOut, d_C, bytes, cudaMemcpyDeviceToHost), result);
    PIPELINE_CUDA_CHECK(cudaEventRecord(evR1, 0), result);
    PIPELINE_CUDA_CHECK(cudaEventSynchronize(evR1), result);
    PIPELINE_CUDA_CHECK(cudaEventElapsedTime(&result.readbackMs, evR0, evR1), result);

    result.success = true;

pipeline_cleanup:
    if (d_A)
        cudaFree(d_A);
    if (d_B)
        cudaFree(d_B);
    if (d_C)
        cudaFree(d_C);

    if (evA0)
        cudaEventDestroy(evA0);
    if (evA1)
        cudaEventDestroy(evA1);
    if (evB0)
        cudaEventDestroy(evB0);
    if (evB1)
        cudaEventDestroy(evB1);
    if (evC0)
        cudaEventDestroy(evC0);
    if (evC1)
        cudaEventDestroy(evC1);
    if (evR0)
        cudaEventDestroy(evR0);
    if (evR1)
        cudaEventDestroy(evR1);
}
