// cuda_interface.h
// Ten plik nie może zawierać żadnych nagłówków CUDA.
// Musi kompilować się przez MSVC bez CUDA Toolkit.
#pragma once
#include <cstddef>

// ---------------------------------------------------------------------------
// Informacje o urządzeniu CUDA
// ---------------------------------------------------------------------------
struct CudaDeviceInfo {
    char   name[256];
    size_t totalMemoryBytes;
    int    computeCapabilityMajor;
    int    computeCapabilityMinor;
    bool   supportsDouble;
};

// ---------------------------------------------------------------------------
// Wynik pojedynczego uruchomienia splotu
// ---------------------------------------------------------------------------
struct ConvolutionResult {
    bool   success;
    char   errorMessage[512]; // wypełniane tylko gdy success == false

    // Czasy GPU (mierzone cudaEvent_t, jednostka: ms)
    float  transferToGpuMs;
    float  kernelMs;
    float  transferFromGpuMs;

    // Czas CPU reference (mierzony std::chrono, jednostka: ms)
    double cpuReferenceMs;

    // Walidacja
    double maxAbsError;       // max|C_gpu[i] - C_cpu[i]|
    bool   validationPassed;  // maxAbsError < 1e-9
};

// ---------------------------------------------------------------------------
// API — implementacja w cuda/cuda_impl.cu
// ---------------------------------------------------------------------------

// Zwraca true jeśli GPU jest dostępne i wypełnia info.
// Zwraca false jeśli brak GPU lub błąd sterownika (nie crash).
bool queryCudaDevice(CudaDeviceInfo& info);

// Liczy splot C[n] = sum_{k=0}^{N-1} A[k] * B[n-k]
// inputA, inputB: tablice CPU, rozmiar N
// outputC:        tablica CPU, rozmiar N, wynik z GPU
// Funkcja jest synchroniczna — blokuje do zakończenia.
// Zwraca false i wypełnia result.errorMessage przy błędzie CUDA.
bool runConvolution(
    const double* inputA,
    const double* inputB,
    double*       outputC,
    int           N,
    double        dt,
    ConvolutionResult& result
);
