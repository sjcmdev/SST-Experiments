d# Faza 1 — Specyfikacja implementacyjna
## Splot sygnałów: statyczny kernel, synchroniczne wywołanie

> **Dokument dla Codex/Copilot.** Każda sekcja opisuje jeden plik lub krok.
> Nie pomijaj żadnego kroku. Nie modyfikuj kolejności inicjalizacji.

---

## 0. Wymagania wstępne

Przed generowaniem projektu muszą być zainstalowane:
- Visual Studio 2022 (workload: Desktop development with C++)
- CUDA Toolkit 12.x (instalator ustawia `CUDA_PATH` i `CUDA_PATH_Vx_x`)
- Zmienna środowiskowa `CUDA_PATH` musi być widoczna w sesji terminala

Weryfikacja przed startem:
```
echo %CUDA_PATH%           → np. C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6
nvcc --version             → Cuda compilation tools, release 12.x
cl                         → Microsoft (R) C/C++ Optimizing Compiler
```

---

## 1. Struktura katalogów — utwórz ręcznie

```
gpu-experiment/
├── premake5.lua
├── tools/
│   └── premake5.exe              ← pobierz z premake.io/download (Windows x64)
├── vendor/
│   ├── glfw/
│   │   ├── include/
│   │   │   └── GLFW/
│   │   │       ├── glfw3.h
│   │   │       └── glfw3native.h
│   │   └── lib-vc2022/
│   │       ├── glfw3.lib
│   │       └── glfw3dll.lib      ← opcjonalne, nie używane
│   ├── imgui/
│   │   ├── imgui.h
│   │   ├── imgui_internal.h
│   │   ├── imgui.cpp
│   │   ├── imgui_draw.cpp
│   │   ├── imgui_tables.cpp
│   │   ├── imgui_widgets.cpp
│   │   ├── imconfig.h
│   │   ├── imgui_impl_glfw.h
│   │   ├── imgui_impl_glfw.cpp
│   │   ├── imgui_impl_opengl3.h
│   │   ├── imgui_impl_opengl3.cpp
│   │   └── imgui_impl_opengl3_loader.h
│   ├── implot/
│   │   ├── implot.h
│   │   ├── implot_internal.h
│   │   ├── implot.cpp
│   │   └── implot_items.cpp
│   └── glad/
│       ├── include/
│       │   ├── glad/
│       │   │   └── glad.h
│       │   └── KHR/
│       │       └── khrplatform.h
│       └── src/
│           └── glad.c
└── src/
    ├── main.cpp
    ├── app.h
    ├── app.cpp
    ├── gui.h
    ├── gui.cpp
    ├── cuda_interface.h
    └── cuda/
        └── cuda_impl.cu
```

### Skąd pobrać zależności

**GLFW 3.4** — https://www.glfw.org/download.html → „Windows pre-compiled binaries" →
skopiować `include/GLFW/` i `lib-vc2022/glfw3.lib` do `vendor/glfw/`.

**Dear ImGui** — https://github.com/ocornut/imgui (branch: master, najnowszy tag stabilny) →
skopiować pliki wymienione powyżej (bez podkatalogów, bez `examples/`, bez `backends/` poza impl_glfw i impl_opengl3).

**ImPlot** — https://github.com/epezent/implot (najnowszy tag stabilny) →
skopiować `implot.h`, `implot_internal.h`, `implot.cpp`, `implot_items.cpp`.

**GLAD** — https://glad.dav1d.de/ → ustawienia:
- Language: C/C++
- Specification: OpenGL
- API gl: Version 3.3
- Profile: Core
- Options: Generate a loader ✓

Kliknij GENERATE, pobierz ZIP, skopiuj `include/` i `src/glad.c` do `vendor/glad/`.

---

## 2. `premake5.lua` — kompletny plik

```lua
-- premake5.lua
-- Walidacja CUDA_PATH przed czymkolwiek innym
local cudaPath = os.getenv("CUDA_PATH")
assert(
    cudaPath ~= nil and cudaPath ~= "",
    "\n\nERROR: CUDA_PATH is not set.\n" ..
    "Install CUDA Toolkit and ensure CUDA_PATH environment variable is defined.\n" ..
    "Expected format: C:\\Program Files\\NVIDIA GPU Computing Toolkit\\CUDA\\vX.Y\n"
)

workspace "GpuExperiment"
    configurations { "Debug", "Release" }
    platforms      { "x64" }
    startproject   "GpuExperiment"

project "GpuExperiment"
    kind        "ConsoleApp"
    language    "C++"
    cppdialect  "C++17"
    targetdir   "build/bin/%{cfg.buildcfg}"
    objdir      "build/obj/%{cfg.buildcfg}/%{prj.name}"

    -- Pliki źródłowe
    files {
        "src/**.h",
        "src/**.cpp",
        "src/**.cu",
        "vendor/imgui/imgui.cpp",
        "vendor/imgui/imgui_draw.cpp",
        "vendor/imgui/imgui_tables.cpp",
        "vendor/imgui/imgui_widgets.cpp",
        "vendor/imgui/imgui_impl_glfw.cpp",
        "vendor/imgui/imgui_impl_opengl3.cpp",
        "vendor/implot/implot.cpp",
        "vendor/implot/implot_items.cpp",
        "vendor/glad/src/glad.c",
    }

    -- Katalogi nagłówków
    includedirs {
        "src",
        "vendor/imgui",
        "vendor/implot",
        "vendor/glad/include",
        "vendor/glfw/include",
        cudaPath .. "/include",
    }

    -- Katalogi bibliotek
    libdirs {
        "vendor/glfw/lib-vc2022",
        cudaPath .. "/lib/x64",
    }

    -- Linkowanie
    links {
        "cudart",
        "glfw3",
        "opengl32",
        "gdi32",
        "user32",
        "shell32",
    }

    -- Ustawienia wspólne
    filter "system:windows"
        systemversion "latest"
        defines { "_CRT_SECURE_NO_WARNINGS" }

    filter "configurations:Debug"
        runtime  "Debug"
        symbols  "On"
        optimize "Off"
        defines  { "_DEBUG", "DEBUG" }

    filter "configurations:Release"
        runtime  "Release"
        symbols  "Off"
        optimize "Speed"
        defines  { "NDEBUG" }

    -- =========================================================
    -- CUDA Custom Build Tool — kompilacja plików .cu przez nvcc
    -- WAŻNE: oba filtry (outputs i commands) muszą być osobne
    -- =========================================================

    -- Wspólne: wyjście .obj dla obu konfiguracji
    filter { "files:**.cu" }
        buildmessage "NVCC: %{file.relpath}"
        buildoutputs { "$(IntDir)%{file.basename}.obj" }

    -- Debug
    filter { "files:**.cu", "configurations:Debug" }
        buildcommands {
            '"$(CUDA_PATH)/bin/nvcc"'
            .. ' -c'
            .. ' -G'
            .. ' -g'
            .. ' -O0'
            .. ' -std=c++17'
            .. ' -gencode arch=compute_75,code=sm_75'
            .. ' -Xcompiler "/MDd /Zi /FS"'
            .. ' -I"$(CUDA_PATH)/include"'
            .. ' -I"src"'
            .. ' -o "$(IntDir)%{file.basename}.obj"'
            .. ' "%{file.relpath}"'
        }

    -- Release
    filter { "files:**.cu", "configurations:Release" }
        buildcommands {
            '"$(CUDA_PATH)/bin/nvcc"'
            .. ' -c'
            .. ' -O2'
            .. ' -std=c++17'
            .. ' -gencode arch=compute_75,code=sm_75'
            .. ' -Xcompiler "/MD"'
            .. ' -DNDEBUG'
            .. ' -I"$(CUDA_PATH)/include"'
            .. ' -I"src"'
            .. ' -o "$(IntDir)%{file.basename}.obj"'
            .. ' "%{file.relpath}"'
        }

    filter {}
```

> **Krytyczna uwaga — `$(IntDir)`**: W MSBuild `$(IntDir)` to ścieżka do katalogu
> pośredniego z końcowym separatorem (`\`), np. `build\obj\Debug\GpuExperiment\`.
> Użycie `$(IntDir)%{file.basename}.obj` gwarantuje, że plik `.obj` z nvcc trafia
> dokładnie tam, gdzie MSVC spodziewa się znaleźć obiekty do linkowania.
> VS automatycznie linkuje wszystkie `.obj` ze swojego `$(IntDir)`.

---

## 3. `src/cuda_interface.h` — kompletny plik

```cpp
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
    ConvolutionResult& result
);
```

---

## 4. `src/cuda/cuda_impl.cu` — kompletny plik

```cuda
// cuda_impl.cu
// Jedyny plik CUDA w Fazie 1.
// Zawiera: kernel splotu + implementację interfejsu.
//
// WAŻNE: ten plik jest kompilowany przez nvcc, nie przez MSVC.
// Nie używać: wyjątków C++ (#define _HAS_EXCEPTIONS 0 nie jest potrzebne,
// ale nie rzucać wyjątków — nvcc może je ignorować po stronie device).

#include "cuda_interface.h"  // szuka w src/ — dodane do nvcc -I"src"
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
#define CUDA_CHECK(call, res)                                               \
    do {                                                                     \
        cudaError_t _e = (call);                                            \
        if (_e != cudaSuccess) {                                            \
            snprintf((res).errorMessage, sizeof((res).errorMessage),        \
                     "CUDA error at %s:%d  →  %s",                         \
                     __FILE__, __LINE__, cudaGetErrorString(_e));           \
            (res).success = false;                                          \
            goto cuda_cleanup;                                              \
        }                                                                   \
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
    const double* __restrict__ A,
    const double* __restrict__ B,
    double*       __restrict__ C,
    int N)
{
    int n = blockIdx.x * blockDim.x + threadIdx.x;
    if (n >= N) return;

    double sum = 0.0;
    for (int k = 0; k < N; ++k) {
        int bIdx = n - k;
        if (bIdx >= 0 && bIdx < N) {
            sum += A[k] * B[bIdx];
        }
    }
    C[n] = sum;
}

// ===========================================================================
// queryCudaDevice
// ===========================================================================
bool queryCudaDevice(CudaDeviceInfo& info) {
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
    info.totalMemoryBytes         = props.totalGlobalMem;
    info.computeCapabilityMajor   = props.major;
    info.computeCapabilityMinor   = props.minor;
    // Compute capability >= 1.3 for double; ale praktycznie >= 2.0 na każdej
    // sensownej karcie. Zostawiamy prostą flagę.
    info.supportsDouble = (props.major >= 2);

    return true;
}

// ===========================================================================
// runConvolution
// ===========================================================================
bool runConvolution(
    const double* inputA,
    const double* inputB,
    double*       outputC,
    int           N,
    ConvolutionResult& result)
{
    // --- Inicjalizacja result ---
    memset(&result, 0, sizeof(result));
    result.success = true;

    // --- Deklaracje WSZYSTKICH uchwytów CUDA przed pierwszym CUDA_CHECK ---
    // (wymagane przez goto: nie można przeskakiwać inicjalizacji zmiennych)
    double* d_A = nullptr;
    double* d_B = nullptr;
    double* d_C = nullptr;

    cudaEvent_t evH2D_start  = nullptr;
    cudaEvent_t evH2D_stop   = nullptr;
    cudaEvent_t evK_start    = nullptr;
    cudaEvent_t evK_stop     = nullptr;
    cudaEvent_t evD2H_start  = nullptr;
    cudaEvent_t evD2H_stop   = nullptr;

    float elapsed = 0.f;
    const size_t bytes = static_cast<size_t>(N) * sizeof(double);

    // --- Alokacja pamięci GPU ---
    CUDA_CHECK(cudaMalloc(&d_A, bytes), result);
    CUDA_CHECK(cudaMalloc(&d_B, bytes), result);
    CUDA_CHECK(cudaMalloc(&d_C, bytes), result);

    // --- Tworzenie eventów do pomiaru czasu ---
    CUDA_CHECK(cudaEventCreate(&evH2D_start),  result);
    CUDA_CHECK(cudaEventCreate(&evH2D_stop),   result);
    CUDA_CHECK(cudaEventCreate(&evK_start),    result);
    CUDA_CHECK(cudaEventCreate(&evK_stop),     result);
    CUDA_CHECK(cudaEventCreate(&evD2H_start),  result);
    CUDA_CHECK(cudaEventCreate(&evD2H_stop),   result);

    // --- Transfer Host → Device ---
    CUDA_CHECK(cudaEventRecord(evH2D_start, 0), result);
    CUDA_CHECK(cudaMemcpy(d_A, inputA, bytes, cudaMemcpyHostToDevice), result);
    CUDA_CHECK(cudaMemcpy(d_B, inputB, bytes, cudaMemcpyHostToDevice), result);
    CUDA_CHECK(cudaEventRecord(evH2D_stop, 0),  result);
    CUDA_CHECK(cudaEventSynchronize(evH2D_stop), result);
    CUDA_CHECK(cudaEventElapsedTime(&elapsed, evH2D_start, evH2D_stop), result);
    result.transferToGpuMs = elapsed;

    // --- Launch kernela ---
    const int blockSize = 256;
    const int gridSize  = (N + blockSize - 1) / blockSize;

    CUDA_CHECK(cudaEventRecord(evK_start, 0), result);
    convolutionKernel<<<gridSize, blockSize>>>(d_A, d_B, d_C, N);
    // Sprawdzenie błędu launch (<<<>>> nie zwraca błędu bezpośrednio)
    CUDA_CHECK(cudaGetLastError(), result);
    CUDA_CHECK(cudaEventRecord(evK_stop, 0),      result);
    CUDA_CHECK(cudaEventSynchronize(evK_stop),    result);
    CUDA_CHECK(cudaEventElapsedTime(&elapsed, evK_start, evK_stop), result);
    result.kernelMs = elapsed;

    // --- Transfer Device → Host ---
    CUDA_CHECK(cudaEventRecord(evD2H_start, 0), result);
    CUDA_CHECK(cudaMemcpy(outputC, d_C, bytes, cudaMemcpyDeviceToHost), result);
    CUDA_CHECK(cudaEventRecord(evD2H_stop, 0),  result);
    CUDA_CHECK(cudaEventSynchronize(evD2H_stop), result);
    CUDA_CHECK(cudaEventElapsedTime(&elapsed, evD2H_start, evD2H_stop), result);
    result.transferFromGpuMs = elapsed;

// --- Sprzątanie (zawsze wykonywane, nawet po goto z CUDA_CHECK) ---
cuda_cleanup:
    if (evH2D_start)  cudaEventDestroy(evH2D_start);
    if (evH2D_stop)   cudaEventDestroy(evH2D_stop);
    if (evK_start)    cudaEventDestroy(evK_start);
    if (evK_stop)     cudaEventDestroy(evK_stop);
    if (evD2H_start)  cudaEventDestroy(evD2H_start);
    if (evD2H_stop)   cudaEventDestroy(evD2H_stop);
    if (d_A) cudaFree(d_A);
    if (d_B) cudaFree(d_B);
    if (d_C) cudaFree(d_C);

    return result.success;
}
```

---

## 5. `src/app.h` — kompletny plik

```cpp
// app.h
#pragma once
#include "cuda_interface.h"
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// Stałe konfiguracyjne
// ---------------------------------------------------------------------------
constexpr int    APP_DEFAULT_N      = 4096;
constexpr double APP_SIGNAL_T       = 1.0;   // czas trwania sygnału [s]
constexpr double APP_VALIDATION_TOL = 1e-9;  // tolerancja absolutna

// ---------------------------------------------------------------------------
// Stan aplikacji — jeden egzemplarz, przekazywany przez referencję
// ---------------------------------------------------------------------------
struct AppState {
    // Urządzenie CUDA
    CudaDeviceInfo deviceInfo  = {};
    bool           cudaAvail   = false;

    // Sygnały (CPU)
    int    N  = APP_DEFAULT_N;
    double T  = APP_SIGNAL_T;
    std::vector<double> timeAxis;   // t[i] = i * dt
    std::vector<double> signalA;    // sygnał A (Gaussowski)
    std::vector<double> signalB;    // sygnał B (prostokąt)
    std::vector<double> signalC;    // wynik GPU
    std::vector<double> signalCref; // wynik CPU (referencja)

    // Wynik ostatniego uruchomienia
    ConvolutionResult lastResult = {};
    bool              hasResult  = false;

    // Dane zdecymowane do wykresu (aktualizowane po każdym compute)
    std::vector<double> plotT;
    std::vector<double> plotA;
    std::vector<double> plotB;
    std::vector<double> plotC;
    std::vector<double> plotCref;
};

// ---------------------------------------------------------------------------
// Funkcje
// ---------------------------------------------------------------------------

// Inicjalizacja: query GPU, generowanie sygnałów
void appInit(AppState& s);

// Regeneracja sygnałów (np. po zmianie N)
void appGenerateSignals(AppState& s);

// Uruchomienie splotu: CPU reference + GPU + walidacja + aktualizacja wykresu
// Zwraca false gdy CUDA niedostępna lub błąd (szczegóły w s.lastResult)
bool appRunComputation(AppState& s);

// Aktualizacja zdecymowanych tablic do wykresu
// Wywołać po appGenerateSignals() lub appRunComputation()
void appUpdatePlotData(AppState& s);
```

---

## 6. `src/app.cpp` — kompletny plik

```cpp
// app.cpp
#include "app.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <chrono>

static constexpr double PI = 3.14159265358979323846;
static constexpr int    MAX_PLOT_POINTS = 2048;

// ---------------------------------------------------------------------------
void appInit(AppState& s) {
    s.cudaAvail = queryCudaDevice(s.deviceInfo);
    appGenerateSignals(s);
}

// ---------------------------------------------------------------------------
void appGenerateSignals(AppState& s) {
    const int    N  = s.N;
    const double dt = s.T / static_cast<double>(N - 1);

    s.timeAxis .assign(N, 0.0);
    s.signalA  .assign(N, 0.0);
    s.signalB  .assign(N, 0.0);
    s.signalC  .assign(N, 0.0);
    s.signalCref.assign(N, 0.0);

    for (int i = 0; i < N; ++i) {
        const double t = i * dt;
        s.timeAxis[i] = t;

        // Sygnał A: gaussowski impuls
        //   mu    = 0.30  (środek)
        //   sigma = 0.05  (szerokość)
        //   A_max = 1.0
        const double mu_a    = 0.30;
        const double sigma_a = 0.05;
        s.signalA[i] = std::exp(
            -(t - mu_a) * (t - mu_a) / (2.0 * sigma_a * sigma_a)
        );

        // Sygnał B: prostokąt 1.0 dla t ∈ [0.60, 0.80]
        s.signalB[i] = (t >= 0.60 && t <= 0.80) ? 1.0 : 0.0;
    }

    appUpdatePlotData(s);
}

// ---------------------------------------------------------------------------
bool appRunComputation(AppState& s) {
    if (!s.cudaAvail) {
        s.lastResult = {};
        s.lastResult.success = false;
        strncpy(s.lastResult.errorMessage,
                "No CUDA device available.",
                sizeof(s.lastResult.errorMessage) - 1);
        s.hasResult = true;
        return false;
    }

    const int N = s.N;

    // --- CPU reference (mierzony oddzielnie) ---
    auto cpuStart = std::chrono::steady_clock::now();
    for (int n = 0; n < N; ++n) {
        double sum = 0.0;
        for (int k = 0; k < N; ++k) {
            const int bIdx = n - k;
            if (bIdx >= 0 && bIdx < N) {
                sum += s.signalA[k] * s.signalB[bIdx];
            }
        }
        s.signalCref[n] = sum;
    }
    auto cpuEnd = std::chrono::steady_clock::now();
    const double cpuMs = std::chrono::duration<double, std::milli>(
        cpuEnd - cpuStart
    ).count();

    // --- GPU ---
    bool ok = runConvolution(
        s.signalA.data(),
        s.signalB.data(),
        s.signalC.data(),
        N,
        s.lastResult
    );
    s.lastResult.cpuReferenceMs = cpuMs;

    // --- Walidacja (tylko gdy GPU się powiodło) ---
    if (ok) {
        double maxErr = 0.0;
        for (int i = 0; i < N; ++i) {
            maxErr = std::max(maxErr, std::abs(s.signalC[i] - s.signalCref[i]));
        }
        s.lastResult.maxAbsError     = maxErr;
        s.lastResult.validationPassed = (maxErr < APP_VALIDATION_TOL);
    }

    s.hasResult = true;
    appUpdatePlotData(s); // odśwież wykres po obliczeniach
    return ok;
}

// ---------------------------------------------------------------------------
void appUpdatePlotData(AppState& s) {
    const int N    = s.N;
    const int step = std::max(1, N / MAX_PLOT_POINTS);
    const int cnt  = (N + step - 1) / step;

    s.plotT   .resize(cnt);
    s.plotA   .resize(cnt);
    s.plotB   .resize(cnt);
    s.plotC   .resize(cnt);
    s.plotCref.resize(cnt);

    for (int i = 0; i < cnt; ++i) {
        const int idx = std::min(i * step, N - 1);
        s.plotT   [i] = s.timeAxis [idx];
        s.plotA   [i] = s.signalA  [idx];
        s.plotB   [i] = s.signalB  [idx];
        s.plotC   [i] = s.signalC  [idx];
        s.plotCref[i] = s.signalCref[idx];
    }
}
```

---

## 7. `src/gui.h` — kompletny plik

```cpp
// gui.h
#pragma once
#include "app.h"

// Renderuje wszystkie okna ImGui dla jednej klatki.
// Wywołać między ImGui::NewFrame() a ImGui::Render().
void guiRender(AppState& s);
```

---

## 8. `src/gui.cpp` — kompletny plik

```cpp
// gui.cpp
#include "gui.h"
#include "imgui.h"
#include "implot.h"

// ---------------------------------------------------------------------------
// Okno: informacje o urządzeniu CUDA
// ---------------------------------------------------------------------------
static void guiWindowDevice(const AppState& s) {
    ImGui::Begin("CUDA Device");

    if (!s.cudaAvail) {
        ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f),
                           "No CUDA device found.");
        ImGui::TextDisabled("GPU features are unavailable.");
        ImGui::End();
        return;
    }

    const auto& d = s.deviceInfo;
    ImGui::Text("Name:               %s", d.name);
    ImGui::Text("Compute Capability: %d.%d",
                d.computeCapabilityMajor, d.computeCapabilityMinor);
    ImGui::Text("Total Memory:       %.1f GB",
                static_cast<double>(d.totalMemoryBytes) / (1024.0 * 1024.0 * 1024.0));
    ImGui::Text("Double Precision:   %s",
                d.supportsDouble ? "Yes" : "No");

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Okno: sterowanie obliczeniami i wyniki
// ---------------------------------------------------------------------------
static void guiWindowCompute(AppState& s) {
    ImGui::Begin("Convolution");

    ImGui::Text("Signal length: N = %d", s.N);
    ImGui::Text("Duration:      T = %.2f s", s.T);
    ImGui::Separator();

    // Przycisk aktywny tylko gdy GPU dostępne
    if (!s.cudaAvail) {
        ImGui::BeginDisabled();
    }
    const bool clicked = ImGui::Button("Run Convolution (GPU + CPU ref.)");
    if (!s.cudaAvail) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(no GPU)");
    }

    if (clicked) {
        appRunComputation(s);
    }

    // Wyniki
    if (s.hasResult) {
        ImGui::Separator();
        const auto& r = s.lastResult;

        if (!r.success) {
            ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f),
                               "Error: %s", r.errorMessage);
        } else {
            ImGui::Text("Transfer CPU -> GPU: %8.3f ms", r.transferToGpuMs);
            ImGui::Text("Kernel:              %8.3f ms", r.kernelMs);
            ImGui::Text("Transfer GPU -> CPU: %8.3f ms", r.transferFromGpuMs);
            ImGui::Text("CPU reference:       %8.3f ms", r.cpuReferenceMs);
            ImGui::Separator();
            ImGui::Text("Max absolute error:  %.6e", r.maxAbsError);

            if (r.validationPassed) {
                ImGui::TextColored(ImVec4(0.2f, 1.f, 0.2f, 1.f),
                                   "Status: OK  (tolerance 1e-9)");
            } else {
                ImGui::TextColored(ImVec4(1.f, 0.6f, 0.f, 1.f),
                                   "Status: VALIDATION FAILED");
            }
        }
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Okno: wykres sygnałów
// ---------------------------------------------------------------------------
static void guiWindowPlot(const AppState& s) {
    ImGui::Begin("Signal Plot");

    if (s.plotT.empty()) {
        ImGui::TextDisabled("No data to display.");
        ImGui::End();
        return;
    }

    const int cnt       = static_cast<int>(s.plotT.size());
    const double* tPtr  = s.plotT.data();

    if (ImPlot::BeginPlot("##signals", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Time [s]", "Amplitude");

        ImPlot::SetNextLineStyle(ImVec4(0.3f, 0.7f, 1.0f, 1.f), 1.5f);
        ImPlot::PlotLine("Signal A (Gaussian)",
                         tPtr, s.plotA.data(), cnt);

        ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.7f, 0.2f, 1.f), 1.5f);
        ImPlot::PlotLine("Signal B (Rectangle)",
                         tPtr, s.plotB.data(), cnt);

        if (s.hasResult && s.lastResult.success) {
            ImPlot::SetNextLineStyle(ImVec4(0.2f, 1.0f, 0.4f, 1.f), 2.0f);
            ImPlot::PlotLine("Convolution (GPU)",
                             tPtr, s.plotC.data(), cnt);

            // CPU reference jako cienka przerywana linia dla weryfikacji wizualnej
            ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.3f, 0.3f, 0.6f), 1.0f);
            ImPlot::PlotLine("Convolution (CPU ref.)",
                             tPtr, s.plotCref.data(), cnt);
        }

        ImPlot::EndPlot();
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Punkt wejścia GUI — wywołany z pętli main
// ---------------------------------------------------------------------------
void guiRender(AppState& s) {
    guiWindowDevice(s);
    guiWindowCompute(s);
    guiWindowPlot(s);
}
```

---

## 9. `src/main.cpp` — kompletny plik

```cpp
// main.cpp
// WAŻNA KOLEJNOŚĆ NAGŁÓWKÓW:
//   1. glad.h MUSI być przed glfw3.h (definiuje prototypy OpenGL)
//   2. imgui_impl_opengl3.h po glad.h
//   3. implot.h po imgui.h

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include <cstdio>
#include <cstdlib>

#include "app.h"
#include "gui.h"

// ---------------------------------------------------------------------------
static void glfwErrorCallback(int error, const char* description) {
    fprintf(stderr, "[GLFW] Error %d: %s\n", error, description);
}

// ---------------------------------------------------------------------------
int main() {
    // -----------------------------------------------------------------------
    // 1. GLFW
    // -----------------------------------------------------------------------
    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit()) {
        fprintf(stderr, "[GLFW] glfwInit() failed\n");
        return 1;
    }

    // OpenGL 3.3 Core — wystarczy dla ImGui, obsługiwane przez wszystkie
    // współczesne GPU NVIDIA.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // Na macOS byłoby potrzebne GLFW_OPENGL_FORWARD_COMPAT — tu nie trzeba.

    GLFWwindow* window = glfwCreateWindow(
        1400, 900,
        "GPU Experiment — Phase 1: Signal Convolution",
        nullptr, nullptr
    );
    if (!window) {
        fprintf(stderr, "[GLFW] Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // VSync: ogranicz do refresh rate monitora

    // -----------------------------------------------------------------------
    // 2. GLAD — MUSI być po glfwMakeContextCurrent
    // -----------------------------------------------------------------------
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        fprintf(stderr, "[GLAD] Failed to initialize OpenGL loader\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    // Opcjonalnie: wypisz wersję OpenGL dla weryfikacji
    fprintf(stdout, "[OpenGL] Version: %s\n", glGetString(GL_VERSION));
    fprintf(stdout, "[OpenGL] Renderer: %s\n", glGetString(GL_RENDERER));

    // -----------------------------------------------------------------------
    // 3. ImGui — MUSI być po GLAD
    // -----------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Nie włączamy DockingEnable ani ViewportsEnable — zbędne w Fazie 1.

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // -----------------------------------------------------------------------
    // 4. ImPlot — MUSI być po ImGui::CreateContext
    // -----------------------------------------------------------------------
    ImPlot::CreateContext();

    // -----------------------------------------------------------------------
    // 5. Stan aplikacji i inicjalizacja CUDA
    // -----------------------------------------------------------------------
    AppState state;
    appInit(state); // query GPU + generacja sygnałów

    if (!state.cudaAvail) {
        fprintf(stdout,
                "[CUDA] No device found — running in degraded mode (no GPU).\n");
    } else {
        fprintf(stdout, "[CUDA] Device: %s (SM %d.%d)\n",
                state.deviceInfo.name,
                state.deviceInfo.computeCapabilityMajor,
                state.deviceInfo.computeCapabilityMinor);
    }

    // -----------------------------------------------------------------------
    // 6. Pętla renderowania
    // -----------------------------------------------------------------------
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Nowa klatka ImGui
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Renderuj UI aplikacji
        guiRender(state);

        // Zakończ klatkę ImGui i wyrenderuj do OpenGL
        ImGui::Render();

        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.12f, 0.12f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // -----------------------------------------------------------------------
    // 7. Sprzątanie — odwrotna kolejność inicjalizacji
    // -----------------------------------------------------------------------
    ImPlot::DestroyContext();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
```

---

## 10. Budowanie projektu

```bat
:: Z katalogu gpu-experiment/
tools\premake5.exe vs2022
```

Powinno wygenerować:
```
GpuExperiment.sln
GpuExperiment.vcxproj
GpuExperiment.vcxproj.filters
```

Otworzyć `GpuExperiment.sln` w Visual Studio 2022.
Wybrać konfigurację `Debug | x64`.
Build → Build Solution (`Ctrl+Shift+B`).

Weryfikacja w oknie Output:
```
1> NVCC: src/cuda/cuda_impl.cu
1> Compiling CUDA: src/cuda/cuda_impl.cu.h
1>   [nvcc output]
...
1> GpuExperiment.vcxproj -> build\bin\Debug\GpuExperiment.exe
```

---

## 11. Pułapki i rozwiązania

### P1 — `.obj` z nvcc nie jest linkowany

**Objaw**: `unresolved external symbol queryCudaDevice` lub `runConvolution`.

**Przyczyna**: `$(IntDir)` w `buildoutputs` nie zgadza się z miejscem, gdzie VS szuka obiektów.

**Rozwiązanie**: Sprawdzić w VS właściwościach pliku `.cu`:
`Properties → Custom Build Tool → Outputs`
Musi zawierać ścieżkę identyczną z `$(IntDir)cuda_impl.obj`.
Jeśli nie: jawnie dodać do linkera:
```lua
-- W premake5.lua, po filter {}:
filter "configurations:Debug"
    linkoptions { '"$(IntDir)cuda_impl.obj"' }
filter "configurations:Release"
    linkoptions { '"$(IntDir)cuda_impl.obj"' }
filter {}
```

---

### P2 — LNK2038: mismatch `_ITERATOR_DEBUG_LEVEL`

**Objaw**: błąd linkera LNK2038.

**Przyczyna**: nvcc skompilował z `/MD` a MSVC z `/MDd` lub odwrotnie.

**Rozwiązanie**: Upewnić się że w `premake5.lua`:
- konfiguracja Debug: `-Xcompiler "/MDd"` AND `runtime "Debug"`
- konfiguracja Release: `-Xcompiler "/MD"` AND `runtime "Release"`

---

### P3 — `CUDA_PATH` zawiera spacje, nvcc nie jest znaleziony

**Objaw**: Custom Build Tool zwraca `'C:\Program' is not recognized`.

**Przyczyna**: Ścieżka do nvcc nie jest w cudzysłowach.

**Rozwiązanie**: W `buildcommands` nvcc musi być otoczony zewnętrznymi cudzysłowami:
```lua
'"$(CUDA_PATH)/bin/nvcc" -c ...'
-- Zewnętrzne ' to cudzysłów Lua, wewnętrzne " to cudzysłów powłoki MSBuild
```

---

### P4 — `glad.h` musi być przed `glfw3.h`

**Objaw**: Błędy kompilacji: `GL/gl.h already included`.

**Rozwiązanie**: Kolejność w `main.cpp` jest obowiązkowa:
```cpp
#include <glad/glad.h>   // PIERWSZE
#include <GLFW/glfw3.h>  // DRUGIE
```

---

### P5 — ImPlot nie kompiluje się (`imgui.h` nie znalezione)

**Objaw**: `implot.cpp`: fatal error C1083: cannot open 'imgui.h'.

**Przyczyna**: ImPlot szuka `imgui.h` przez ścieżkę includedirs, nie relatywnie.

**Rozwiązanie**: `vendor/imgui/` musi być w `includedirs` w `premake5.lua` — jest.
Sprawdzić czy `imgui.h` rzeczywiście tam jest (nie w podkatalogu).

---

### P6 — `cudaGetLastError()` po launch nie łapie błędów kernela

**Wyjaśnienie**: `cudaGetLastError()` po `<<<>>>` łapie tylko błąd *launchu* (np. zły gridDim).
Błędy wewnątrz kernela (np. nieprawidłowy dostęp do pamięci) wychodzą przy następnej
synchronizacji — tu przy `cudaEventSynchronize(evK_stop)`. CUDA_CHECK po nim wystarczy.

---

### P7 — nvcc nie rozpoznaje `-std=c++17`

**Objaw**: `nvcc warning: flag '--std' not supported with '--device-c'` lub błąd.

**Rozwiązanie**: CUDA Toolkit ≥ 11.0 obsługuje `-std=c++17`. Starsze wersje wymagają
`--std=c++14`. Sprawdzić `nvcc --version`.

---

### P8 — `cudaEventElapsedTime` wymaga `float*`, nie `double*`

**Wyjaśnienie**: API CUDA zwraca czas jako `float`. W `cuda_impl.cu` używamy lokalnej
zmiennej `float elapsed` i przypisujemy do `result.transferToGpuMs` (też `float`).
Pole w `ConvolutionResult` jest `float` — nie zmieniać na `double`.

---

### P9 — Wynik splotu jest wizualnie zerowy

**Przyczyna**: Sygnały A i B nie nakładają się wystarczająco, albo dt jest za duże dla gaussiana.

**Weryfikacja**: Dla `N=4096`, `T=1.0`:
- `dt = 1/4095 ≈ 0.000244 s`
- Gaussian z `sigma=0.05` ma efektywną szerokość `6*sigma = 0.3 s → ~1229 próbek`
- Prostokąt `[0.6, 0.8]` to `0.2 s → ~820 próbek`
- Splot pojawi się w okolicach `t ∈ [0.6+0.3, 0.8+0.3] = [0.9, 1.1]` (częściowo obcięty)

Wynik nie będzie zerowy — ImPlot automatycznie skaluje oś Y.

---

## 12. Oczekiwane zachowanie po uruchomieniu

Po uruchomieniu `GpuExperiment.exe`:

1. Okno 1400×900 z ciemnym tłem i trzema panelami ImGui.
2. Panel „CUDA Device" pokazuje nazwę GPU i compute capability.
3. Panel „Convolution" ma aktywny przycisk „Run Convolution".
4. Panel „Signal Plot" pokazuje sygnał A (niebieski) i sygnał B (pomarańczowy).
5. Po kliknięciu przycisku:
   - Czasy transferów i kernela (w ms) pojawiają się w panelu.
   - Na wykresie pojawia się zielona linia (GPU) i czerwona półprzezroczysta (CPU ref.).
   - Status: `OK (tolerance 1e-9)`.
   - `Max absolute error` powinien być < `1e-12` dla tego prostego przypadku.

---

## 13. Checklista przed przejściem do Fazy 2

- [ ] `premake5 vs2022` — brak błędów Lua
- [ ] Build Debug — brak błędów i ostrzeżeń (cuda_impl.cu kompilowany przez nvcc)
- [ ] Build Release — brak błędów
- [ ] Aplikacja startuje bez crash
- [ ] Panel Device pokazuje rzeczywiste dane GPU
- [ ] Przycisk produkuje wynik (czasy > 0 ms)
- [ ] Status: OK
- [ ] `maxAbsError < 1e-9`
- [ ] GPU line i CPU ref. line na wykresie są wizualnie identyczne
- [ ] Zamknięcie okna kończy aplikację bez hang


sprawdź to repozytorium: https://github.com/sjcmdev/SST-Experiments.git
na gałęzi: GPU-integration​
najpierw clone potem ewentualne przełączenie

wprowadziłem pewne modyfikacje:
1) dodano modyfikację podstawowych parametrów sygnałów 
2) dodano dirty flag i automatyczne przeładowanie wyniku splotu po zmianie parametrów sygnałów
3) zrobiono jedną solucje i GPU-Integration z jednym vendor i tools