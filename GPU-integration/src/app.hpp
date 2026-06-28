// app.h
#pragma once
#include "kernel_manager.hpp"   // NOWE — includuje cuda.h + nvrtc.h
#include "cuda_interface.hpp"
#include "gpu_worker.hpp"
#include "signal_graph.hpp" // NOWE
#include "code_gen.hpp"     // NOWE
#include <future>
#include <vector>
#include <string>

struct ImNodesEditorContext;

// ---------------------------------------------------------------------------
// Stałe konfiguracyjne
// ---------------------------------------------------------------------------
constexpr int    APP_DEFAULT_N      = 4096;
constexpr double APP_SIGNAL_T       = 1.0;   // czas trwania sygnału [s]
constexpr double APP_VALIDATION_TOL = 1e-9;  // tolerancja absolutna

struct GaussianSignalParams {
    double amplitude = 1.0;
    double center = 0.30;
    double sigma = 0.05;
};

struct RectangleSignalParams {
    double amplitude = 1.0;
    double start = 0.60;
    double end = 0.80;
};

// ---------------------------------------------------------------------------
// Stan aplikacji — jeden egzemplarz, przekazywany przez referencję
// ---------------------------------------------------------------------------
struct AppState {
    // Urządzenie CUDA
    CudaDeviceInfo deviceInfo  = {};
    bool           cudaAvail   = false;

    // Sygnały (CPU)
    int    N  = APP_DEFAULT_N;
    int    cpuSteps = APP_DEFAULT_N;
    double T  = APP_SIGNAL_T;
    std::vector<double> timeAxis;   // t[i] = i * dt
    std::vector<double> signalA;    // sygnał A (Gaussowski)
    std::vector<double> signalB;    // sygnał B (prostokąt)
    std::vector<double> signalC;    // wynik GPU
    std::vector<double> cpuTimeAxis;
    std::vector<double> cpuSignalA;
    std::vector<double> cpuSignalB;
    std::vector<double> signalCref; // wynik CPU (referencja)

    GaussianSignalParams signalAParams = {};
    RectangleSignalParams signalBParams = {};

    // Wynik ostatniego uruchomienia
    ConvolutionResult lastResult = {};
    bool              hasResult  = false;
    bool              validationAvailable = false;
    bool              dataDirty = false;
    bool              autoRecomputeDirty = true;

    // Dane zdecymowane do wykresu (aktualizowane po każdym compute)
    std::vector<double> plotT;
    std::vector<double> plotCpuT;
    std::vector<double> plotA;
    std::vector<double> plotB;
    std::vector<double> plotC;
    std::vector<double> plotCref;

    // --- NOWE pola Fazy 2 ---
    KernelManager     kernelMgr;            // obiekt zarządzający kompilacją NVRTC
    std::string       kernelSource;         // bieżący kod źródłowy kernela (jako tekst)
    std::string       kernelFilePath;       // ścieżka do pliku kernela na dysku
    NvrtcCompileResult lastCompile;         // wynik ostatniej kompilacji NVRTC
    bool              kernelAutoRerun = false;  // trigger po przeładowaniu kernela

    // --- NOWE pola Fazy 3 ---
    GpuWorkerThread gpuWorker;
    std::future<AsyncConvResult> gpuFuture;
    bool computing = false;

    // Faza 4 : node grafe + generacja kodu
    NodeGraph graphA;                             // NOWE: graf sygnału A
    NodeGraph graphB;                             // NOWE: graf sygnału B (pusty w Fazie 4)
    ImNodesEditorContext *graphAEditorCtx = nullptr;
    ImNodesEditorContext *graphBEditorCtx = nullptr;
    KernelManager signalKernelMgr;                // NOWE: manager kerneli sygnałów (genA+genB)
    std::string generatedCode;                    // NOWE: ostatnio wygenerowany string CUDA
    NvrtcCompileResult lastSignalCompile;         // NOWE
    bool codeDirty = true;                        // NOWE: czy kod wymaga regeneracji

    // Wyniki (readback z GPU)
    std::vector<double> convOutput;

    // Aktualne N i dt
    double dt = 1.0 / (N - 1);
};

// ---------------------------------------------------------------------------
// Funkcje
// ---------------------------------------------------------------------------

// Inicjalizacja: query GPU, generowanie sygnałów
void appInit(AppState& appState);

// Regeneracja sygnałów (np. po zmianie N)
void appGenerateSignals(AppState& appState);

// Uruchomienie splotu: CPU reference + GPU + walidacja + aktualizacja wykresu
// Zwraca false gdy CUDA niedostępna lub błąd (szczegóły w s.lastResult)
bool appRunComputation(AppState& appState);

void appMarkDirty(AppState& appState);

void appRecomputeIfDirty(AppState& appState);

void appPollAndSubmit(AppState& appState);

// Aktualizacja zdecymowanych tablic do wykresu
// Wywołać po appGenerateSignals() lub appRunComputation()
void appUpdatePlotData(AppState& appState);
