# Faza 6 — LambertW na GPU: header injection w `KernelManager`

> Kontynuacja Fazy 2 / Fazy 4-5. Zakłada że masz już działający `KernelManager`
> z obsługą wielu funkcji (`expectedFunctions`), `GpuWorkerThread`, oraz
> zvendorowany `LambertW` w `Vendor/LambertW/` (jak w `premake5.lua` tego repo).

---

## 0. Czego brakuje

`KernelManager::compile()` (stan po Fazie 4-5) woła:

```cpp
nvrtcCreateProgram(&prog, source.c_str(), "convolution.cu", 0, nullptr, nullptr);
```

`0, nullptr, nullptr` = **zero nagłówków**. To znaczy że żaden kernel kompilowany
przez NVRTC nie może dziś zrobić `#include "cokolwiek.cuh"` — NVRTC nie ma skąd
wziąć treści tego pliku (nie ma dostępu do dysku w sensie `#include` z systemu
plików hosta tak jak robi to `nvcc`; trzeba mu *wstrzyknąć* nagłówek jako string).

To jest jedyna rzecz która stoi między Wami a użyciem `lambert_w_device.cuh`
(GPU port `Vendor/LambertW`) w dowolnym kernelu NVRTC — np. w JFM do
rozwiązywania I(V) modelu jednodiodowego/dwudiodowego w closed-form.

Patch poniżej jest **w pełni wsteczny kompatybilny** — `headerSources` ma
wartość domyślną `{}`, więc `convKernelMgr.compile(source, major, minor,
{"convolution"})` z Fazy 4-5 kompiluje się bez zmian.

---

## 1. Krok 1 — Rozszerzenie `kernel_manager.h`

Dodaj nowy parametr do `compile()`. Nic innego w klasie się nie zmienia.

```cpp
// kernel_manager.h — ZMIENIONE (Faza 6, addytywnie do Fazy 4-5)
#pragma once
#include <string>
#include <vector>
#include <utility>          // NOWE: std::pair
#include <unordered_map>
#include "cuda_interface.h"
#include <cuda.h>
#include <nvrtc.h>

class KernelManager {
public:
    KernelManager();
    ~KernelManager();

    // Skompiluj source (może zawierać WIELE __global__ funkcji).
    // expectedFunctions: lista nazw funkcji do pobrania z modułu po kompilacji.
    // headerSources: NOWE (Faza 6) — pary (nazwa_includa, treść_pliku) dla
    //                NVRTC header injection. Domyślnie puste = zachowanie
    //                identyczne jak w Fazie 4-5.
    //                Przykład: { {"lambert_w_device.cuh", treść_pliku} }
    //                pozwala kernelowi zrobić:
    //                  #include "lambert_w_device.cuh"
    NvrtcCompileResult compile(
        const std::string&              source,
        int                              capMajor,
        int                              capMinor,
        const std::vector<std::string>& expectedFunctions,
        const std::vector<std::pair<std::string, std::string>>&
                                         headerSources = {}   // NOWE
    );

    bool isReady() const;
    KernelHandle getFunction(const std::string& name) const;
    int functionCount() const;

private:
    void unloadModule();

    CUmodule   m_module = nullptr;
    std::unordered_map<std::string, CUfunction> m_functions;
    bool       m_ready  = false;
};

std::string loadKernelSourceFromFile(const std::string& path);
// ^ BEZ ZMIAN — to zwykły "wczytaj plik do string". Użyjemy go też do
//   wczytania lambert_w_device.cuh z dysku — nie jest specyficzny dla
//   kernel-source, mimo nazwy. Nowa funkcja nie jest potrzebna.
```

---

## 2. Krok 2 — Rozszerzenie `kernel_manager.cpp`

Jedyna zmiana jest w **KROKU A** (`nvrtcCreateProgram`). Wszystko od KROKU B
w dół (kompilacja, log, PTX, `cuModuleLoadData`, pętla po `expectedFunctions`)
zostaje **dokładnie takie jak w Fazie 4-5** — nie dotykaj tego.

```cpp
// kernel_manager.cpp — ZMIENIONE: tylko sygnatura i KROK A

NvrtcCompileResult KernelManager::compile(
    const std::string&              source,
    int                              capMajor,
    int                              capMinor,
    const std::vector<std::string>& expectedFunctions,
    const std::vector<std::pair<std::string, std::string>>& headerSources)  // NOWE
{
    NvrtcCompileResult result;
    if (source.empty()) { result.log = "source empty"; return result; }

    auto t0 = std::chrono::high_resolution_clock::now();

    // ================================================================
    // KROK A — Utwórz program NVRTC  (ZMIENIONE: wstrzyknij headerSources)
    // ================================================================
    // nvrtcCreateProgram(prog, source, name, numHeaders, headerSrcs, headerNames)
    //
    // headerSrcs[i]  — TREŚĆ pliku nagłówkowego jako C-string
    // headerNames[i] — nazwa pod jaką kernel go widzi w #include "nazwa"
    //
    // WAŻNE: headerNamesArr/headerSrcsArr muszą żyć do końca tego wywołania.
    // Budujemy je z `headerSources` (parametr, więc żyje przez cały compile()).
    std::vector<const char*> headerNamesArr;
    std::vector<const char*> headerSrcsArr;
    headerNamesArr.reserve(headerSources.size());
    headerSrcsArr.reserve(headerSources.size());
    for (const auto& kv : headerSources) {
        headerNamesArr.push_back(kv.first.c_str());
        headerSrcsArr.push_back(kv.second.c_str());
    }

    nvrtcProgram prog = nullptr;
    nvrtcResult nvErr = nvrtcCreateProgram(
        &prog,
        source.c_str(),
        "kernel.cu",                 // etykieta diagnostyczna (bez zmian funkcjonalnie)
        static_cast<int>(headerSrcsArr.size()),
        headerSrcsArr.empty()  ? nullptr : headerSrcsArr.data(),
        headerNamesArr.empty() ? nullptr : headerNamesArr.data()
    );
    if (nvErr != NVRTC_SUCCESS) {
        result.log = std::string("nvrtcCreateProgram failed: ")
                   + nvrtcGetErrorString(nvErr);
        return result;
    }

    // --- KROK B i dalej: BEZ ZMIAN, dokładnie jak w Fazie 4-5 ---
    // (kompilacja --gpu-architecture=compute_XY --std=c++17, log,
    //  GetPTX, unloadModule(), cuModuleLoadData, pętla po expectedFunctions)
    // ...
}
```

Gdy `headerSources` jest puste (domyślnie), `headerSrcsArr.empty()` jest `true`,
więc wołanie redukuje się do `nvrtcCreateProgram(..., 0, nullptr, nullptr)` —
**bitowo identyczne** z obecnym zachowaniem. Stare wywołania `compile()` bez
piątego argumentu nie wiedzą nawet że coś się zmieniło.

---

## 3. Krok 3 — Plik `lambert_w_device.cuh`

Plik trafia **obok** istniejących plików CPU, w tym samym katalogu co
`LambertW.h` / `FukushimaLambertW.h`:

```
Vendor/LambertW/
├── LambertW.h              ← bez zmian (CPU, referencja)
├── LambertW.cc
├── FukushimaLambertW.h
├── FukushimaLambertW.cc
├── Horner.h
├── premake5.lua            ← bez zmian
└── lambert_w_device.cuh    ← NOWY (ten dokument)
```

**Bezpieczeństwo wobec `premake5.lua`**: blok `files { "%{prj.location}/*.h",
"%{prj.location}/*.cc" }` w istniejącym `premake5.lua` łapie pliki kończące
się dokładnie na `.h` i `.cc`. `lambert_w_device.cuh` kończy się na `.cuh`,
**nie** na `.h` — glob go nie złapie. `cl.exe` nigdy nie spróbuje skompilować
tego pliku jako część `LambertW` (StaticLib), co i tak by się nie udało
(`__device__`, `__forceinline__` to słowa kluczowe NVCC/NVRTC, nieznane MSVC).
Zero zmian w `premake5.lua` jest wymagane.

Plik jest czystym C++ string injection — nigdy nie trafia do `nvcc` jako
osobna jednostka kompilacji. Jedyna ścieżka jaką "widzi" to:
dysk → `loadKernelSourceFromFile()` → `std::string` → `nvrtcCreateProgram`
jako wirtualny header.

(Treść pliku: patrz `lambert_w_device.cuh` w tym samym zestawie plików —
implementuje `LambertW0_d`, `LambertWm1_d` i warianty `float`, algorytm
Veberic identyczny matematycznie z `Vendor/LambertW/LambertW.cc`, ale bez
`static` lokalnych tablic i bez STL, więc legalny w `__device__` code.)

---

## 4. Krok 4 — Przykładowy kernel: `kernels/jfm_single_diode.cu`

Tak jak `kernels/convolution.cu`, ten plik **nie jest kompilowany przez system
budowania** — jest tekstem wczytywanym w runtime przez NVRTC.

```cuda
// kernels/jfm_single_diode.cu
//
// Jawne rozwiązanie I(V) dla modelu jednodiodowego (5P) przez Lambert W.
// #include rozwiązywany przez NVRTC header injection (Faza 6) —
// "lambert_w_device.cuh" musi być przekazany w headerSources przy compile().
#include "lambert_w_device.cuh"

extern "C" __global__ void jfm_iv_single_diode(
    const double* __restrict__ V,
    double*       __restrict__ I,
    double Iph, double I0, double n, double Vt, double Rs, double Rsh,
    int N)
{
    int idx = blockDim.x * blockIdx.x + threadIdx.x;
    if (idx >= N) return;

    double v     = V[idx];
    double denom = 1.0 + Rs / Rsh;
    double arg   = (I0 * Rs / (n * Vt))
                 * exp((Rs * (Iph + I0) + v) / (n * Vt * denom));

    I[idx] = (Iph + I0 - v / Rsh) / denom - (n * Vt / Rs) * LambertW0_d(arg);
}

// Residuals dla LM fittingu na GPU (batch po punktach pomiarowych)
extern "C" __global__ void jfm_residuals_single_diode(
    const double* __restrict__ V,
    const double* __restrict__ I_meas,
    double*       __restrict__ residuals,
    double Iph, double I0, double n, double Vt, double Rs, double Rsh,
    int N)
{
    int idx = blockDim.x * blockIdx.x + threadIdx.x;
    if (idx >= N) return;

    double v       = V[idx];
    double denom   = 1.0 + Rs / Rsh;
    double arg     = (I0 * Rs / (n * Vt))
                   * exp((Rs * (Iph + I0) + v) / (n * Vt * denom));
    double I_model = (Iph + I0 - v / Rsh) / denom
                   - (n * Vt / Rs) * LambertW0_d(arg);

    residuals[idx] = I_meas[idx] - I_model;
}
```

---

## 5. Krok 5 — Wiring w `app.cpp` (wzorzec z Fazy 4-5)

Analogicznie do `convKernelMgr` / `signalKernelMgr`, dodaj trzeci manager.
W strukturze stanu (tam gdzie `convKernelMgr`, `signalKernelMgr`):

```cpp
KernelManager     jfmKernelMgr;       // NOWE: kernele JFM (Lambert W)
```

Przy starcie (tam gdzie ładowany jest `convolution.cu`):

```cpp
// Faza 6: załaduj kernel JFM + nagłówek LambertW, skompiluj z header injection
state.jfmKernelFilePath = "kernels/jfm_single_diode.cu";
state.jfmKernelSource   = loadKernelSourceFromFile(state.jfmKernelFilePath);

// Reużywamy loadKernelSourceFromFile — to zwykły file→string, nieważne
// że nazwa sugeruje "kernel source". Wczytujemy nim też nagłówek .cuh.
std::string lambertWHeader =
    loadKernelSourceFromFile("Vendor/LambertW/lambert_w_device.cuh");

if (!state.jfmKernelSource.empty() && !lambertWHeader.empty() && state.cudaAvail) {
    state.lastJfmCompile = state.jfmKernelMgr.compile(
        state.jfmKernelSource,
        state.deviceInfo.computeCapabilityMajor,
        state.deviceInfo.computeCapabilityMinor,
        { "jfm_iv_single_diode", "jfm_residuals_single_diode" },  // expectedFunctions
        { { "lambert_w_device.cuh", lambertWHeader } }            // headerSources, NOWE
    );
    if (!state.lastJfmCompile.success) {
        fprintf(stderr, "[App] Błąd kompilacji JFM kernela:\n%s\n",
                state.lastJfmCompile.log.c_str());
    }
}
```

Kolejność inicjalizacji (Runtime API → `cuInit` → wczytanie plików → `compile()`)
jest dokładnie ta sama co w Fazie 2 — header injection nie zmienia tej zasady,
bo `lambertWHeader` to tylko kolejny string wczytywany przed `compile()`,
tak samo jak `kernelSource`.

---

## 6. Wywołanie z `GpuWorkerThread` (opcjonalnie)

Jeśli kompilacja JFM kernela ma być wyzwalana asynchronicznie (np. po edycji
node-grafu modelu, analogicznie do `signalKernelMgr` w Fazie 4), `compile()`
samo w sobie nie zmienia się względem `GpuWorkerThread` — przekazujesz po
prostu dodatkowy argument `headerSources` tak jak dziś przekazujesz
`expectedFunctions`. `GpuWorkerThread::submit()` nie musi nic wiedzieć
o nagłówkach — to czysto wewnętrzna sprawa `KernelManager::compile()`.

---

## 7. Problemy

### P1 — NVRTC: `error: lambert_w_device.cuh: No such file or directory`

**Przyczyna**: `headerNames[i]` w `nvrtcCreateProgram` nie zgadza się z
łańcuchem w `#include "..."` kernela. Muszą być **bajt-w-bajt identyczne**
(wielkość liter też, na Linuksie/WSL).

**Rozwiązanie**: Sprawdź że `{ "lambert_w_device.cuh", lambertWHeader }` w
wywołaniu `compile()` ma dokładnie taki napis jak `#include "lambert_w_device.cuh"`
w pliku kernela.

### P2 — NVRTC: `error: identifier "LambertW0_d" is undefined`

**Przyczyna A**: `lambertWHeader` jest pusty (plik nie wczytał się —
`loadKernelSourceFromFile` zwraca `""` po cichu jeśli ścieżka jest zła
względem working directory, dokładnie jak opisano w P9 z Fazy 2).

**Rozwiązanie**: Sprawdź working directory tak jak w P9 (Faza 2) — debug
z Visual Studio ma working dir = katalog projektu, nie katalog `.exe`.
Dodaj `fprintf` po `loadKernelSourceFromFile` sprawdzający `.empty()`.

**Przyczyna B**: Kernel zapomniał `#include "lambert_w_device.cuh"` na górze
pliku `.cu` mimo że header injection działa poprawnie.

### P3 — Kompiluje się, ale `cuModuleGetFunction` nie znajduje `jfm_iv_single_diode`

**Przyczyna**: Brak `extern "C"` przed `__global__ void jfm_iv_single_diode`.
Bez `extern "C"`, NVRTC mangluje nazwę (C++ name mangling) i
`cuModuleGetFunction(..., "jfm_iv_single_diode")` szuka dosłownej nazwy,
której nie ma w module.

**Rozwiązanie**: Dokładnie jak w P1 z Fazy 2 — każda funkcja w
`expectedFunctions` musi mieć `extern "C" __global__` w źródle.

---

## 8. Walidacja numeryczna

Implementacja `lambert_w_device.cuh` została zweryfikowana niezależnie przy
użyciu `mpmath` (50 cyfr precyzji) — pełny opis w `VALIDATION.md`. Skrót:

- Błąd względny < 2×10⁻¹³ w całej dziedzinie obu gałęzi (W₀, W₋₁), łącznie
  z bezpośrednim sąsiedztwem punktu gałęzi `x = -1/e`, gdzie `scipy.special.lambertw`
  sama traci precyzję do ~10⁻⁵ (zweryfikowane — patrz `VALIDATION.md`, sekcja 4).
- Te same współczynniki Padé/branch-point co `Vendor/LambertW/LambertW.cc` —
  GPU i CPU to ta sama matematyka, inna platforma.
- Test rzucony przeciw obu referencjom CPU z tego repo (`utl::LambertW` i
  `Fukushima::LambertW`) w `lambert_w_gpu_test.cu`, w duchu istniejącego
  `test_accuracy.cxx` (ten sam epsilon rzędu 10⁻¹⁵, ten sam pattern sweepa
  po obu gałęziach).

---

## 9. Pliki w tym dostarczeniu

```
lambert_w_device.cuh                  → Vendor/LambertW/lambert_w_device.cuh
faza6_lambertw_gpu_integracja.md      → docs/ (ten dokument)
kernels_jfm_single_diode.cu           → kernels/jfm_single_diode.cu
lambert_w_gpu_test.cu                 → standalone NVCC validation/benchmark
                                         (NIE część NVRTC pipeline — osobny
                                         .exe do weryfikacji dokładności/throughput,
                                         linkowany bezpośrednio z Vendor/LambertW
                                         jak test_accuracy.cxx)
VALIDATION.md                         → docs/per-algorytm/lambert_w_gpu/VALIDATION.md
```
