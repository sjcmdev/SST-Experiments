// kernel_manager.h
// Zarządzanie kompilacją kernela przez NVRTC i ładowaniem przez Driver API.
//
// Ten plik includuje cuda.h i nvrtc.h — to jest celowe.
// kernel_manager.cpp kompiluje MSVC (cl.exe), nie NVCC — i to jest poprawne,
// bo NVRTC i Driver API to zwykłe biblioteki C++ bez specjalnej składni GPU.
#pragma once
#include <string>
#include "cuda_interface.hpp"  // KernelHandle, NvrtcCompileResult

// Driver API i NVRTC — nagłówki z CUDA Toolkit (includowane przez cl.exe)
#include <cuda.h>
#include <nvrtc.h>

class KernelManager {
public:
    KernelManager();
    ~KernelManager();

    // Skompiluj source przez NVRTC, załaduj jako moduł Driver API.
    // capMajor / capMinor — compute capability z CudaDeviceInfo
    // (np. 7 i 5 dla sm_75).
    NvrtcCompileResult compile(
        const std::string& source,
        int                capMajor,
        int                capMinor
    );

    // Czy kernel jest skompilowany i gotowy do użycia?
    bool isReady() const;

    // Zwraca uchwyt do funkcji kernela (CUfunction rzutowany na void*).
    // Zwraca nullptr jeśli isReady() == false.
    KernelHandle getFunction() const;

private:
    void unloadModule();  // zwolnij bieżący CUmodule jeśli załadowany

    CUmodule   m_module   = nullptr;
    CUfunction m_function = nullptr;
    bool       m_ready    = false;
};

// ---------------------------------------------------------------------------
// Wczytaj plik kernela z dysku. Zwraca pusty string jeśli plik nie istnieje.
// ---------------------------------------------------------------------------
std::string loadKernelSourceFromFile(const std::string& path);
