// kernel_manager.h — zmodyfikowany (wersja Phase 4)
#pragma once
#include <string>
#include <unordered_map> // NOWE
#include <vector>
#include "cuda_interface.hpp"
#include <cuda.h>
#include <nvrtc.h>

class KernelManager
{
public:
    KernelManager();
    ~KernelManager();

    // Skompiluj source (może zawierać WIELE __global__ funkcji).
    // expectedFunctions: lista nazw funkcji do pobrania z modułu po kompilacji.
    NvrtcCompileResult compile(
        const std::string &source,
        int capMajor,
        int capMinor,
        const std::vector<std::string> &expectedFunctions // NOWE
    );

    bool isReady() const;

    // Zwraca uchwyt do nazwanej funkcji. nullptr jeśli nie znaleziono.
    KernelHandle getFunction(const std::string &name) const; // ZMIENIONE

    // Ile funkcji zostało załadowanych?
    int functionCount() const;

private:
    void unloadModule();

    CUmodule m_module = nullptr;
    std::unordered_map<std::string, CUfunction> m_functions; // NOWE
    bool m_ready = false;
};

std::string loadKernelSourceFromFile(const std::string &path);
