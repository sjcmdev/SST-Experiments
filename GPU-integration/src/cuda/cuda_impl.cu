// cuda_impl.cu
// Jedyny plik CUDA w Fazie 1.
// Zawiera: kernel splotu + implementację interfejsu.
//
// WAŻNE: ten plik jest kompilowany przez nvcc, nie przez MSVC.
// Nie używać: wyjątków C++ (#define _HAS_EXCEPTIONS 0 nie jest potrzebne,
// ale nie rzucać wyjątków — nvcc może je ignorować po stronie device).

#include "cuda_interface.hpp" // szuka w src/ — dodane do nvcc -I"src"
#include <cuda_runtime.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cfloat>

// ===========================================================================
// Makro obsługi błędów CUDA z goto cleanup
//
// Użycie: CUDA_CHECK(cuda_call, result_struct);
// Przy błędzie: wypełnia errorMessage, ustawia success=false, goto cuda_cleanup.
// Wszystkie zmienne CUDA muszą być zadeklarowane PRZED pierwszym CUDA_CHECK.
// ===========================================================================
#define CUDA_CHECK(call, res)                                        \
    do                                                               \
    {                                                                \
        cudaError_t _e = (call);                                     \
        if (_e != cudaSuccess)                                       \
        {                                                            \
            snprintf((res).errorMessage, sizeof((res).errorMessage), \
                     "CUDA error at %s:%d  →  %s",                   \
                     __FILE__, __LINE__, cudaGetErrorString(_e));    \
            (res).success = false;                                   \
            goto cuda_cleanup;                                       \
        }                                                            \
    } while (0)

// ===========================================================================
// Kernel splotu liniowego (element-wise, jedno wątki na punkt wyjściowy)
//
// C[n] = sum_{k=0}^{N-1}  A[k] * B[n-k]   (boundary: tylko k gdy n-k ∈ [0,N))
//
// Wymagania:
//   - blockDim.x = 256
//   - gridDim.x  = (N + 255) / 256
//   - Wątki n >= N są pomijane (guard w pierwszej linii)
// ===========================================================================
__global__ void convolutionKernel(
    const double *__restrict__ A,
    const double *__restrict__ B,
    double *__restrict__ C,
    int N,
    double dt)
{
    const int n = blockIdx.x * blockDim.x + threadIdx.x;
    if (n >= N)
        return;

    double sum = 0.0;

    for (int k = 0; k < N; ++k)
    {
        const int bIdx = n - k;

        if (bIdx >= 0 && bIdx < N)
        {
            sum += A[k] * B[bIdx];
        }
    }

    C[n] = sum * dt;
}

// ===========================================================================
// queryCudaDevice
// ===========================================================================
bool queryCudaDevice(CudaDeviceInfo &info)
{
    memset(&info, 0, sizeof(info));

    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess || count == 0)
    {
        return false;
    }

    cudaDeviceProp props;
    if (cudaGetDeviceProperties(&props, 0) != cudaSuccess)
    {
        return false;
    }

    strncpy(info.name, props.name, sizeof(info.name) - 1);
    info.totalMemoryBytes = props.totalGlobalMem;
    info.computeCapabilityMajor = props.major;
    info.computeCapabilityMinor = props.minor;
    // Compute capability >= 1.3 for double; ale praktycznie >= 2.0 na każdej
    // sensownej karcie. Zostawiamy prostą flagę.
    info.supportsDouble = (props.major >= 2);

    return true;
}

// ===========================================================================
// runConvolution
// ===========================================================================
bool runConvolution(
    const double *inputA,
    const double *inputB,
    double *outputC,
    int N,
    double dt,
    ConvolutionResult &result)
{
    // --- Inicjalizacja result ---
    memset(&result, 0, sizeof(result));
    result.success = true;

    // --- Deklaracje WSZYSTKICH uchwytów CUDA przed pierwszym CUDA_CHECK ---
    // (wymagane przez goto: nie można przeskakiwać inicjalizacji zmiennych)
    double *d_A = nullptr;
    double *d_B = nullptr;
    double *d_C = nullptr;

    cudaEvent_t evH2D_start = nullptr;
    cudaEvent_t evH2D_stop = nullptr;
    cudaEvent_t evK_start = nullptr;
    cudaEvent_t evK_stop = nullptr;
    cudaEvent_t evD2H_start = nullptr;
    cudaEvent_t evD2H_stop = nullptr;

    float elapsed = 0.f;
    const size_t bytes = static_cast<size_t>(N) * sizeof(double);
    const int blockSize = 256;
    const int gridSize = (N + blockSize - 1) / blockSize;

    // --- Alokacja pamięci GPU ---
    CUDA_CHECK(cudaMalloc(&d_A, bytes), result);
    CUDA_CHECK(cudaMalloc(&d_B, bytes), result);
    CUDA_CHECK(cudaMalloc(&d_C, bytes), result);

    // --- Tworzenie eventów do pomiaru czasu ---
    CUDA_CHECK(cudaEventCreate(&evH2D_start), result);
    CUDA_CHECK(cudaEventCreate(&evH2D_stop), result);
    CUDA_CHECK(cudaEventCreate(&evK_start), result);
    CUDA_CHECK(cudaEventCreate(&evK_stop), result);
    CUDA_CHECK(cudaEventCreate(&evD2H_start), result);
    CUDA_CHECK(cudaEventCreate(&evD2H_stop), result);

    // --- Transfer Host → Device ---
    CUDA_CHECK(cudaEventRecord(evH2D_start, 0), result);
    CUDA_CHECK(cudaMemcpy(d_A, inputA, bytes, cudaMemcpyHostToDevice), result);
    CUDA_CHECK(cudaMemcpy(d_B, inputB, bytes, cudaMemcpyHostToDevice), result);
    CUDA_CHECK(cudaEventRecord(evH2D_stop, 0), result);
    CUDA_CHECK(cudaEventSynchronize(evH2D_stop), result);
    CUDA_CHECK(cudaEventElapsedTime(&elapsed, evH2D_start, evH2D_stop), result);
    result.transferToGpuMs = elapsed;

    // --- Launch kernela ---
    CUDA_CHECK(cudaEventRecord(evK_start, 0), result);
    convolutionKernel<<<gridSize, blockSize>>>(d_A, d_B, d_C, N, dt);
    // Sprawdzenie błędu launch (<<<>>> nie zwraca błędu bezpośrednio)
    CUDA_CHECK(cudaGetLastError(), result);
    CUDA_CHECK(cudaEventRecord(evK_stop, 0), result);
    CUDA_CHECK(cudaEventSynchronize(evK_stop), result);
    CUDA_CHECK(cudaEventElapsedTime(&elapsed, evK_start, evK_stop), result);
    result.kernelMs = elapsed;

    // --- Transfer Device → Host ---
    CUDA_CHECK(cudaEventRecord(evD2H_start, 0), result);
    CUDA_CHECK(cudaMemcpy(outputC, d_C, bytes, cudaMemcpyDeviceToHost), result);
    CUDA_CHECK(cudaEventRecord(evD2H_stop, 0), result);
    CUDA_CHECK(cudaEventSynchronize(evD2H_stop), result);
    CUDA_CHECK(cudaEventElapsedTime(&elapsed, evD2H_start, evD2H_stop), result);
    result.transferFromGpuMs = elapsed;

// --- Sprzątanie (zawsze wykonywane, nawet po goto z CUDA_CHECK) ---
cuda_cleanup:
    if (evH2D_start)
        cudaEventDestroy(evH2D_start);
    if (evH2D_stop)
        cudaEventDestroy(evH2D_stop);
    if (evK_start)
        cudaEventDestroy(evK_start);
    if (evK_stop)
        cudaEventDestroy(evK_stop);
    if (evD2H_start)
        cudaEventDestroy(evD2H_start);
    if (evD2H_stop)
        cudaEventDestroy(evD2H_stop);
    if (d_A)
        cudaFree(d_A);
    if (d_B)
        cudaFree(d_B);
    if (d_C)
        cudaFree(d_C);

    return result.success;
}
