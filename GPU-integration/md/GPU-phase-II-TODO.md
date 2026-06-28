# Faza 2 — NVRTC Hot-Reload Kernela: Instrukcja Implementacji

> **Dokument dla samodzielnej implementacji.**  
> Każda sekcja = jeden plik lub jeden krok. Nie pomijaj kolejności. Kod
> jest kompletny i gotowy do wklejenia — miejsca wymagające edycji są
> wyraźnie oznaczone komentarzami `// ZMIEŃ:` lub `// TWOJE:`.

---

## 0. Co się zmienia w Fazie 2 — przegląd

| Element              | Faza 1                                                | Faza 2                                                                        |
| -------------------- | ----------------------------------------------------- | ----------------------------------------------------------------------------- |
| Definicja kernela    | `__global__` w `cuda_impl.cu`, kompilowany przez NVCC | Plik tekstowy `kernels/convolution.cu`, kompilowany przez **NVRTC** w runtime |
| Uruchamianie kernela | `convolution<<<grid, block>>>(...)`                   | `cuLaunchKernel(func, ...)` — Driver API                                      |
| Wymiana kernela      | Wymaga rekompilacji + restartu aplikacji              | Przycisk „Przeładuj kernel" → kompilacja w ~0.5 s bez restartu                |
| Nowe biblioteki      | —                                                     | `nvrtc.lib` (NVRTC), `cuda.lib` (Driver API)                                  |
| Nowe pliki           | —                                                     | `kernel_manager.h`, `kernel_manager.cpp`, `kernels/convolution.cu`            |
| Zmodyfikowane pliki  | —                                                     | `premake5.lua`, `cuda_interface.h`, `cuda_impl.cu`, `app.h/cpp`, `gui.h/cpp`  |

Obliczenia (splot), walidacja CPU i UI wykresów pozostają **bez zmian**.

---

## 1. Teoria — 5 minut czytania, zero pomijania

### 1.1 NVRTC — czym jest

NVRTC (NVIDIA Runtime Compilation) to biblioteka C++, która potrafi **skompilować
kod CUDA C++ do PTX w trakcie działania aplikacji**.

PTX (Parallel Thread eXecution) to pośredni asembler GPU — niezależny od
konkretnej karty. Sterownik GPU dalej kompiluje PTX do kodu maszynowego dla
danego GPU.

```
string (kod CUDA C++) → nvrtcCompileProgram() → PTX string → cuModuleLoadData()
→ CUmodule (załadowany na GPU) → cuModuleGetFunction("convolution") → CUfunction
→ cuLaunchKernel() → wyniki
```

### 1.2 CUDA Driver API vs Runtime API

**Runtime API** (`cudaXxx`) — to czego używałeś w Fazie 1:
```cpp
cudaMalloc(&ptr, size);
cudaMemcpy(dst, src, size, kind);
convolution<<<grid, block>>>(args...);  // <<<>>> to składnia tylko NVCC
cudaDeviceSynchronize();
```

**Driver API** (`cuXxx`) — niższy poziom, wymagany do użycia modułów NVRTC:
```cpp
CUmodule  module;
CUfunction func;
cuModuleLoadData(&module, ptxString);          // załaduj PTX
cuModuleGetFunction(&func, module, "name");     // pobierz uchwyt funkcji
cuLaunchKernel(func, grid..., block..., args); // uruchom kernel
```

**Kluczowy fakt:** obie API **współdzielą kontekst CUDA**. Runtime API tworzy
„primary context" przy pierwszym wywołaniu (np. `cudaMalloc`). Driver API
używa tego samego kontekstu — nie trzeba tworzyć drugiego. Jedyna inicjalizacja
potrzebna po stronie Driver API to `cuInit(0)`, które musi być wywołane
**po** dowolnym wywołaniu Runtime API.

### 1.3 `cuLaunchKernel` — jak przekazać parametry

Zamiast składni `<<<>>>` (którą obsługuje tylko kompilator NVCC) używamy:

```cpp
// Kernel: __global__ void convolution(const double* A, const double* B,
//                                      double* C, int N)

double* d_A = ...; // wskaźnik urządzenia (z cudaMalloc)
double* d_B = ...;
double* d_C = ...;
int     N   = 4096;

// args[i] = wskaźnik do i-tego parametru kernela
void* args[] = { &d_A, &d_B, &d_C, &N };

CUresult err = cuLaunchKernel(
    func,          // CUfunction (z cuModuleGetFunction)
    gridDim, 1, 1, // wymiary siatki (x, y, z)
    blockDim, 1, 1,// wymiary bloku (x, y, z)
    0,             // shared memory bytes
    0,             // stream (0 = default)
    args,          // tablica wskaźników do parametrów
    nullptr        // extra = zawsze nullptr
);
```

### 1.4 `extern "C"` w kernelu NVRTC — obowiązkowe

C++ wykonuje „name mangling" — zmienia nazwy funkcji w kodzie obiektowym.
`cuModuleGetFunction(func, module, "convolution")` szuka funkcji po nazwie
literalnej. Bez `extern "C"` szukałoby `_Z11convolutioniPdPKdS0_` (lub
podobnego). Dlatego **kernel musi być zadeklarowany z `extern "C"`**.

---

## 2. Nowa struktura plików po Fazie 2

```
gpu-experiment/          (lub SST-Experiments/GPU-Integration/)
├── premake5.lua             ← ZMODYFIKOWANY
├── tools/
│   └── premake5.exe
├── vendor/                  ← bez zmian
├── kernels/                 ← NOWY KATALOG
│   └── convolution.cu       ← NOWY (tekst, NIE kompilowany przez build)
└── src/
    ├── main.cpp             ← bez zmian
    ├── app.h                ← ZMODYFIKOWANY (dodanie KernelManager)
    ├── app.cpp              ← ZMODYFIKOWANY (inicjalizacja + kompilacja)
    ├── gui.h                ← ZMODYFIKOWANY (nowe funkcje UI)
    ├── gui.cpp              ← ZMODYFIKOWANY (przycisk reload, log)
    ├── cuda_interface.h     ← ZMODYFIKOWANY (nowe typy i deklaracje)
    ├── kernel_manager.h     ← NOWY
    ├── kernel_manager.cpp   ← NOWY
    └── cuda/
        └── cuda_impl.cu     ← ZMODYFIKOWANY (cuLaunchKernel, bez __global__)
```

Katalog `kernels/` tworzy użytkownik ręcznie. Plik `convolution.cu` w nim
**nie jest kompilowany przez system budowania** — jest wczytywany jako tekst
w runtime przez NVRTC.

---

## 3. Krok 1 — Modyfikacja `premake5.lua`

### 3.1 Dodaj nowe biblioteki do `links`

Znajdź blok `links { ... }` i dodaj dwie pozycje:

```lua
links {
    "cudart",
    "nvrtc",    -- NOWE: NVRTC Runtime Compilation
    "cuda",     -- NOWE: CUDA Driver API (niski poziom, potrzebny do cuModuleLoad*)
    "glfw3",
    "opengl32",
    "gdi32",
    "user32",
    "shell32",
}
```

`nvrtc.lib` i `cuda.lib` są w `%CUDA_PATH%\lib\x64\` — już w `libdirs`,
więc żadnego dodatkowego katalogu nie potrzeba.

### 3.2 Zmień filtr `.cu` z `**.cu` na `src/**.cu`

Pliki w `kernels/` mają rozszerzenie `.cu`, ale **nie powinny być kompilowane
przez NVCC**. Zmień wszystkie filtry od `files:**.cu` na `files:src/**.cu`:

```lua
-- PRZED (stare):
-- filter { "files:**.cu" }
-- filter { "files:**.cu", "configurations:Debug" }
-- filter { "files:**.cu", "configurations:Release" }

-- PO (nowe):
filter { "files:src/**.cu" }
    buildmessage "NVCC: %{file.relpath}"
    buildoutputs { "$(IntDir)%{file.basename}.obj" }

filter { "files:src/**.cu", "configurations:Debug" }
    buildcommands {
        '"$(CUDA_PATH)/bin/nvcc"'
        .. ' -c'
        .. ' -G'
        .. ' -g'
        .. ' -O0'
        .. ' -std=c++17'
        .. ' -gencode arch=compute_75,code=sm_75'  -- TWOJE: dopasuj compute cap
        .. ' -Xcompiler "/MDd /Zi /FS"'
        .. ' -I"$(CUDA_PATH)/include"'
        .. ' -I"src"'
        .. ' -o "$(IntDir)%{file.basename}.obj"'
        .. ' "%{file.relpath}"'
    }

filter { "files:src/**.cu", "configurations:Release" }
    buildcommands {
        '"$(CUDA_PATH)/bin/nvcc"'
        .. ' -c'
        .. ' -O2'
        .. ' -std=c++17'
        .. ' -gencode arch=compute_75,code=sm_75'  -- TWOJE: dopasuj compute cap
        .. ' -Xcompiler "/MD"'
        .. ' -DNDEBUG'
        .. ' -I"$(CUDA_PATH)/include"'
        .. ' -I"src"'
        .. ' -o "$(IntDir)%{file.basename}.obj"'
        .. ' "%{file.relpath}"'
    }
```

### 3.3 Dodaj `kernels/` do listy plików projektu (opcjonalne, dla edycji w VS)

```lua
files {
    "src/**.h",
    "src/**.cpp",
    "src/**.cu",
    -- vendor files jak poprzednio...
    "kernels/*.cu",   -- NOWE: widoczne w VS jako plik tekstowy, nie kompilowane
}
```

Ponieważ filtr NVCC to teraz `src/**.cu`, pliki `kernels/*.cu` nie trafią
do custom build tool — będą po prostu widoczne w Solution Explorer jako pliki
do edycji.

### 3.4 Zregeneruj projekt

```bat
tools\premake5.exe vs2022
```

Zamknij VS przed regeneracją, jeśli był otwarty. Potem otwórz ponownie.

---

## 4. Krok 2 — Nowy plik `kernels/convolution.cu`

Utwórz katalog `kernels/` w głównym katalogu projektu, a w nim plik
`convolution.cu` z poniższą treścią:

```cuda
// kernels/convolution.cu
// WAŻNE: Ten plik NIE jest kompilowany przez NVCC podczas budowania.
//        Jest wczytywany jako tekst w runtime i kompilowany przez NVRTC.
//
// extern "C" jest OBOWIĄZKOWE — bez tego cuModuleGetFunction nie znajdzie
// funkcji (C++ name mangling zmienia jej nazwę w kodzie obiektowym).
//
// Możesz edytować ten plik i kliknąć "Przeładuj kernel" bez rekompilacji projektu.

extern "C" __global__ void convolution(
    const double* __restrict__ A,
    const double* __restrict__ B,
    double*       __restrict__ C,
    int N)
{
    int n = blockDim.x * blockIdx.x + threadIdx.x;
    if (n >= N) return;

    double sum = 0.0;
    for (int k = 0; k < N; ++k) {
        int idx = n - k;
        if (idx >= 0 && idx < N) {
            sum += A[k] * B[idx];
        }
    }
    C[n] = sum;
}
```

**Dlaczego `__restrict__`?** Podpowiedź kompilatorowi, że wskaźniki nie nakładają
się — umożliwia lepszą optymalizację. W Fazie 1 mogło go nie być, dodanie
nie zmienia wyników.

**Ścieżka w runtime:** aplikacja będzie szukać tego pliku jako
`"kernels/convolution.cu"` względem **katalogu roboczego**. W Visual Studio
debugger domyślnym katalogiem roboczym jest katalog projektu (tam gdzie
`premake5.lua`) — więc `kernels/convolution.cu` znajdzie się automatycznie.

---

## 5. Krok 3 — Rozszerzenie `cuda_interface.h`

Dodaj poniższe elementy do istniejącego pliku. Nie usuwaj nic co tam jest.

### 5.1 Nowe `#include` na górze pliku

```cpp
// cuda_interface.h
// Ten plik nadal NIE może zawierać nagłówków CUDA.
// Musi kompilować się przez MSVC bez CUDA Toolkit.
#pragma once
#include <cstddef>
#include <string>   // DODAJ jeśli jeszcze nie ma
```

### 5.2 Nowe typy — dodaj przed lub za istniejącymi struct

```cpp
// ---------------------------------------------------------------------------
// Opaque handle do CUfunction (void* żeby nie musieć includować cuda.h tutaj).
// CUfunction to w rzeczywistości wskaźnik — rzutowanie jest bezpieczne.
// ---------------------------------------------------------------------------
using KernelHandle = void*;

// ---------------------------------------------------------------------------
// Wynik kompilacji NVRTC
// ---------------------------------------------------------------------------
struct NvrtcCompileResult {
    bool        success      = false;
    std::string log;           // log błędów lub "OK"
    float       compileTimeMs = 0.0f;
};
```

### 5.3 Nowe deklaracje funkcji — dodaj za istniejącymi

```cpp
// ---------------------------------------------------------------------------
// Inicjalizacja CUDA Driver API.
// MUSI być wywołane po queryCudaDevice() (czyli po dowolnym Runtime API).
// Bezpieczne do wywołania wielokrotnie.
// ---------------------------------------------------------------------------
bool initCudaDriver();

// ---------------------------------------------------------------------------
// Zmodyfikowana sygnatura runConvolution — dodano parametr kernelFunc.
// Jeśli kernelFunc == nullptr, funkcja ustawia result.success = false.
// STARA sygnatura (bez kernelFunc) powinna być usunięta lub zastąpiona.
// ---------------------------------------------------------------------------
void runConvolution(
    const double*      A,
    const double*      B,
    double*            C_out,
    int                N,
    KernelHandle       kernelFunc,   // <- NOWE
    ConvolutionResult& result
);
```

> **Uwaga:** Jeśli Twoja istniejąca `runConvolution` ma inną sygnaturę
> (np. `size_t N` zamiast `int N`, lub inną kolejność parametrów), zachowaj
> swój styl — kluczowe jest tylko dodanie `KernelHandle kernelFunc` jako
> parametru przed `ConvolutionResult&`.

---

## 6. Krok 4 — Nowy plik `src/kernel_manager.h`

```cpp
// kernel_manager.h
// Zarządzanie kompilacją kernela przez NVRTC i ładowaniem przez Driver API.
//
// Ten plik includuje cuda.h i nvrtc.h — to jest celowe.
// kernel_manager.cpp kompiluje MSVC (cl.exe), nie NVCC — i to jest poprawne,
// bo NVRTC i Driver API to zwykłe biblioteki C++ bez specjalnej składni GPU.
#pragma once
#include <string>
#include "cuda_interface.h"  // KernelHandle, NvrtcCompileResult

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
```

---

## 7. Krok 5 — Nowy plik `src/kernel_manager.cpp` (PEŁNA IMPLEMENTACJA)

To jest serce Fazy 2. Przeczytaj komentarze — wyjaśniają każdy krok.

```cpp
// kernel_manager.cpp
// Kompilowany przez MSVC (cl.exe) — NIE przez NVCC.
// NVRTC i Driver API to zwykłe biblioteki C++, nie wymagają kompilatora GPU.
#include "kernel_manager.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <chrono>

// ---------------------------------------------------------------------------
// Nazwa funkcji kernela szukanej w skompilowanym module.
// Musi pasować do "extern "C" __global__ void NAZWA" w pliku kernela.
// ---------------------------------------------------------------------------
static constexpr const char* KERNEL_FUNC_NAME = "convolution";

// ---------------------------------------------------------------------------
// Makra do sprawdzania błędów — dwa osobne, bo typy błędów się różnią
// ---------------------------------------------------------------------------

// CUDA Driver API — CUresult
#define CU_CHECK_EXPR(call, onFail)                         \
    do {                                                     \
        CUresult _r = (call);                               \
        if (_r != CUDA_SUCCESS) {                           \
            const char* _s = "?";                           \
            cuGetErrorString(_r, &_s);                      \
            fprintf(stderr, "[CU] %s:%d  %s → %s (%d)\n", \
                __FILE__, __LINE__, #call, _s, (int)_r);   \
            onFail;                                          \
        }                                                    \
    } while (0)

// NVRTC — nvrtcResult
#define NVRTC_CHECK_EXPR(call, onFail)                          \
    do {                                                         \
        nvrtcResult _r = (call);                                \
        if (_r != NVRTC_SUCCESS) {                              \
            fprintf(stderr, "[NVRTC] %s:%d  %s → %s (%d)\n",  \
                __FILE__, __LINE__, #call,                       \
                nvrtcGetErrorString(_r), (int)_r);              \
            onFail;                                              \
        }                                                        \
    } while (0)

// ---------------------------------------------------------------------------
KernelManager::KernelManager()  = default;
KernelManager::~KernelManager() { unloadModule(); }

void KernelManager::unloadModule() {
    if (m_module) {
        cuModuleUnload(m_module);   // zwalnia załadowany PTX z VRAM GPU
        m_module   = nullptr;
        m_function = nullptr;       // CUfunction z tego modułu jest teraz nieważna
        m_ready    = false;
    }
}

// ---------------------------------------------------------------------------
NvrtcCompileResult KernelManager::compile(
    const std::string& source,
    int                capMajor,
    int                capMinor)
{
    NvrtcCompileResult result;

    if (source.empty()) {
        result.log = "ERROR: source string is empty";
        return result;
    }

    // --- Pomiar czasu kompilacji (host-side chrono) ---
    auto t0 = std::chrono::high_resolution_clock::now();

    // ================================================================
    // KROK A — Utwórz program NVRTC
    // ================================================================
    // nvrtcCreateProgram(prog, source, name, numHeaders, headers, headerNames)
    //   source      — kod CUDA C++ jako C-string
    //   name        — nazwa pliku (tylko do komunikatów diagnostycznych)
    //   0, null, null — nie przekazujemy dodatkowych nagłówków
    nvrtcProgram prog = nullptr;
    nvrtcResult nvErr = nvrtcCreateProgram(
        &prog,
        source.c_str(),
        "convolution.cu",  // tylko diagnostyczna etykieta
        0, nullptr, nullptr
    );
    if (nvErr != NVRTC_SUCCESS) {
        result.log = std::string("nvrtcCreateProgram failed: ")
                   + nvrtcGetErrorString(nvErr);
        return result;
    }

    // ================================================================
    // KROK B — Skompiluj do PTX
    // ================================================================
    // Opcja --gpu-architecture=compute_XY musi pasować do GPU.
    // capMajor=7, capMinor=5 → "compute_75"
    // Dla RTX 3xxx (Ampere): compute_86
    // Dla RTX 4xxx (Ada):    compute_89
    char archOpt[64];
    snprintf(archOpt, sizeof(archOpt),
             "--gpu-architecture=compute_%d%d", capMajor, capMinor);

    const char* opts[] = {
        archOpt,
        "--std=c++17"
    };
    const int nOpts = 2;

    nvErr = nvrtcCompileProgram(prog, nOpts, opts);

    // ================================================================
    // KROK C — Pobierz log kompilacji (ZAWSZE, nawet przy sukcesie)
    // ================================================================
    // Log zawiera ostrzeżenia i błędy. Rozmiar zawiera bajt '\0' na końcu.
    size_t logSize = 0;
    nvrtcGetProgramLogSize(prog, &logSize);
    if (logSize > 1) {
        result.log.resize(logSize - 1);  // -1 żeby nie wliczać '\0'
        nvrtcGetProgramLog(prog, result.log.data());
    }

    if (nvErr != NVRTC_SUCCESS) {
        // Kompilacja się nie powiodła — log zawiera błędy
        result.success = false;
        nvrtcDestroyProgram(&prog);
        return result;
        // Nie wywołujemy unloadModule() — stary kernel nadal działa
    }

    // ================================================================
    // KROK D — Pobierz PTX (Parallel Thread eXecution — asembler GPU)
    // ================================================================
    size_t ptxSize = 0;
    nvrtcGetPTXSize(prog, &ptxSize);
    std::string ptx(ptxSize, '\0');
    nvrtcGetPTX(prog, ptx.data());

    // Program NVRTC już niepotrzebny po pobraniu PTX
    nvrtcDestroyProgram(&prog);

    // ================================================================
    // KROK E — Zwolnij stary moduł (jeśli był) i załaduj nowy PTX
    // ================================================================
    // WAŻNE: robimy to DOPIERO po udanej kompilacji.
    // Jeśli kompilacja się nie powiodła, stary kernel nadal działa.
    unloadModule();

    // cuModuleLoadData ładuje PTX (lub CUBIN) do pamięci GPU.
    // Wymaga aktywnego kontekstu CUDA — dlatego queryCudaDevice() musi
    // być wywołane przed compile().
    CUresult cuErr = cuModuleLoadData(&m_module, ptx.c_str());
    if (cuErr != CUDA_SUCCESS) {
        const char* errStr = "?";
        cuGetErrorString(cuErr, &errStr);
        result.log += "\n[DRIVER] cuModuleLoadData failed: "
                    + std::string(errStr);
        m_module = nullptr;
        return result;
    }

    // ================================================================
    // KROK F — Pobierz uchwyt do funkcji kernela
    // ================================================================
    // Szukamy po nazwie literalnej — stąd obowiązek extern "C" w kernelu.
    cuErr = cuModuleGetFunction(&m_function, m_module, KERNEL_FUNC_NAME);
    if (cuErr != CUDA_SUCCESS) {
        const char* errStr = "?";
        cuGetErrorString(cuErr, &errStr);
        result.log += std::string("\n[DRIVER] cuModuleGetFunction(\"")
                    + KERNEL_FUNC_NAME + "\") failed: " + errStr
                    + "\nSprwdź: czy kernel ma extern \"C\" i poprawną nazwę?";
        cuModuleUnload(m_module);
        m_module = nullptr;
        return result;
    }

    // ================================================================
    // SUKCES
    // ================================================================
    m_ready = true;

    auto t1 = std::chrono::high_resolution_clock::now();
    result.compileTimeMs =
        std::chrono::duration<float, std::milli>(t1 - t0).count();
    result.success = true;
    if (result.log.empty()) {
        result.log = "OK";
    }

    return result;
}

// ---------------------------------------------------------------------------
bool KernelManager::isReady() const { return m_ready; }

KernelHandle KernelManager::getFunction() const {
    // CUfunction jest typedef na wskaźnik (struct CUfunc_st*) —
    // rzutowanie na void* jest bezpieczne.
    return reinterpret_cast<KernelHandle>(m_function);
}

// ---------------------------------------------------------------------------
std::string loadKernelSourceFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        fprintf(stderr, "[KernelManager] Nie można otworzyć: %s\n",
                path.c_str());
        fprintf(stderr, "[KernelManager] Katalog roboczy = projekt (przy debug z VS)\n");
        return "";
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}
```

---

## 8. Krok 6 — Modyfikacja `src/cuda/cuda_impl.cu`

W Fazie 1 plik ten zawierał kernel `__global__ void convolution(...)` ORAZ
implementację `runConvolution`. W Fazie 2:

- Kernel `__global__` **usuwamy** (jest teraz w `kernels/convolution.cu`)
- Dodajemy `#include <cuda.h>` dla Driver API
- Dodajemy funkcję `initCudaDriver()`
- Zmieniamy `runConvolution` by używała `cuLaunchKernel`

### 8.1 Nowe i zmodyfikowane `#include` na górze pliku

```cpp
// cuda_impl.cu — kompilowany przez NVCC
#include "cuda_interface.h"
#include <cuda.h>            // NOWE: CUDA Driver API (cuModuleLoadData, cuLaunchKernel...)
#include <cuda_runtime.h>    // Runtime API (cudaMalloc, cudaMemcpy...)
#include <cstdio>
#include <cmath>
#include <string>
```

### 8.2 Zaktualizuj/dodaj makra błędów

Makro `CUDA_CHECK` (dla Runtime API) pewnie już masz. Dodaj makro dla
Driver API jeśli chcesz go też tutaj używać:

```cpp
// Makro dla Runtime API (cudaError_t) — zapewne już masz
#ifndef CUDA_CHECK
#define CUDA_CHECK(call) do {                                               \
    cudaError_t _err = (call);                                              \
    if (_err != cudaSuccess) {                                              \
        fprintf(stderr, "[CUDA] %s:%d %s → %s\n",                         \
            __FILE__, __LINE__, #call, cudaGetErrorString(_err));           \
        goto cuda_error;                                                    \
    }                                                                       \
} while(0)
#endif

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
```

### 8.3 Usuń `__global__ void convolution(...)` z `cuda_impl.cu`

Ten blok kodu całkowicie usuwasz — kernel teraz żyje w `kernels/convolution.cu`.

### 8.4 Dodaj funkcję `initCudaDriver()`

Wklej przed `queryCudaDevice` lub przed `runConvolution`:

```cpp
bool initCudaDriver() {
    // cuInit(0) inicjalizuje bibliotekę Driver API.
    // Musi być wywołane PRZED pierwszym cuModuleLoadData / cuLaunchKernel.
    // Może być wywołane po Runtime API (cudaMalloc etc.) — kontekst jest już aktywny.
    // Wywołanie wielokrotne jest bezpieczne (zwraca CUDA_SUCCESS jeśli już zainicjowane).
    CUresult res = cuInit(0);
    if (res != CUDA_SUCCESS) {
        const char* errStr = "?";
        cuGetErrorString(res, &errStr);
        fprintf(stderr, "[CU] cuInit(0) failed: %s\n", errStr);
        return false;
    }
    return true;
}
```

### 8.5 Zmodyfikuj `runConvolution` — zamień `<<<>>>` na `cuLaunchKernel`

Poniżej pełna, zmodyfikowana implementacja. Zaznaczone są nowe fragmenty.

```cpp
void runConvolution(
    const double*      A,
    const double*      B,
    double*            C_out,
    int                N,
    KernelHandle       kernelFuncHandle,  // NOWE: z KernelManager::getFunction()
    ConvolutionResult& result)
{
    // --- Wstępna walidacja ---
    result.success = false;
    result.errorMessage.clear();

    // NOWE: sprawdź czy kernel jest skompilowany
    if (!kernelFuncHandle) {
        result.errorMessage =
            "Kernel not ready. Compile first (KernelManager::compile).";
        return;
    }

    // NOWE: rzutuj KernelHandle (void*) na CUfunction
    CUfunction kernelFunc = reinterpret_cast<CUfunction>(kernelFuncHandle);

    // --- Wskaźniki urządzenia ---
    double* d_A = nullptr;
    double* d_B = nullptr;
    double* d_C = nullptr;

    // --- Zdarzenia CUDA do pomiaru czasu ---
    cudaEvent_t evH2D_0, evH2D_1, evK_0, evK_1, evD2H_0, evD2H_1;

    // Inicjalizacja zdarzeń (jeśli masz inny styl, zachowaj swój)
    cudaEventCreate(&evH2D_0); cudaEventCreate(&evH2D_1);
    cudaEventCreate(&evK_0);   cudaEventCreate(&evK_1);
    cudaEventCreate(&evD2H_0); cudaEventCreate(&evD2H_1);

    const size_t bytes = (size_t)N * sizeof(double);

    // --- Alokacja na GPU ---
    CUDA_CHECK(cudaMalloc(&d_A, bytes));
    CUDA_CHECK(cudaMalloc(&d_B, bytes));
    CUDA_CHECK(cudaMalloc(&d_C, bytes));

    // --- Transfer H2D (host→device) ---
    cudaEventRecord(evH2D_0);
    CUDA_CHECK(cudaMemcpy(d_A, A, bytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_B, B, bytes, cudaMemcpyHostToDevice));
    cudaEventRecord(evH2D_1);
    cudaEventSynchronize(evH2D_1);
    cudaEventElapsedTime(&result.transferToGpuMs, evH2D_0, evH2D_1);

    // --- Launch kernela przez Driver API ---
    // ZMIANA FAZY 2: zamiast convolution<<<grid,block>>>(d_A,d_B,d_C,N)
    // używamy cuLaunchKernel z uchwytem skompilowanym przez NVRTC.
    {
        unsigned int gridDim  = ((unsigned int)N + 255u) / 256u;
        unsigned int blockDim = 256u;

        // args[i] = wskaźnik do i-tego parametru kernela
        // Kernel: (const double* A, const double* B, double* C, int N)
        void* args[] = { &d_A, &d_B, &d_C, &N };

        cudaEventRecord(evK_0);
        CU_CHECK_LAUNCH(
            cuLaunchKernel(
                kernelFunc,
                gridDim, 1, 1,   // siatka (x,y,z)
                blockDim, 1, 1,  // blok  (x,y,z)
                0,               // shared memory [bajty]
                0,               // stream (0 = default)
                args,            // tablica parametrów
                nullptr          // extra (zawsze nullptr)
            ),
            result
        );
        cudaEventRecord(evK_1);
        cudaEventSynchronize(evK_1);
        cudaEventElapsedTime(&result.kernelMs, evK_0, evK_1);
    }

    // --- Transfer D2H (device→host) ---
    cudaEventRecord(evD2H_0);
    CUDA_CHECK(cudaMemcpy(C_out, d_C, bytes, cudaMemcpyDeviceToHost));
    cudaEventRecord(evD2H_1);
    cudaEventSynchronize(evD2H_1);
    cudaEventElapsedTime(&result.transferFromGpuMs, evD2H_0, evD2H_1);

    result.success = true;
    goto cuda_cleanup;  // pomiń sekcję błędu

cuda_error:
    // Błąd już ustawiony w result przez makra
    ;

cuda_cleanup:
    // Zwolnij pamięć GPU (bezpieczne nawet jeśli wskaźnik == nullptr)
    cudaFree(d_A);
    cudaFree(d_B);
    cudaFree(d_C);

    // Zwolnij zdarzenia
    cudaEventDestroy(evH2D_0); cudaEventDestroy(evH2D_1);
    cudaEventDestroy(evK_0);   cudaEventDestroy(evK_1);
    cudaEventDestroy(evD2H_0); cudaEventDestroy(evD2H_1);
}
```

> **Jeśli Twoja Faza 1 używa innego schematu pomiaru czasu lub innego CUDA_CHECK,**
> zachowaj swój styl — ważna jest tylko zamiana `convolution<<<>>>` na
> `cuLaunchKernel` i dodanie parametru `KernelHandle`.

---

## 9. Krok 7 — Modyfikacja `src/app.h`

Dodaj do struktury `AppState` (lub `App`, zależnie od Twojego nazewnictwa):

```cpp
// app.h
#pragma once
#include "cuda_interface.h"
#include "kernel_manager.h"   // NOWE — includuje cuda.h + nvrtc.h
#include <string>
#include <vector>

struct AppState {
    // --- istniejące pola z Fazy 1 (zachowaj wszystkie) ---
    bool              cudaAvail   = false;
    CudaDeviceInfo    deviceInfo  = {};
    ConvolutionResult convResult  = {};
    bool              dirty       = false;
    // ...sygnały, N, itd...

    // --- NOWE pola Fazy 2 ---
    KernelManager     kernelMgr;            // obiekt zarządzający kompilacją NVRTC
    std::string       kernelSource;         // bieżący kod źródłowy kernela (jako tekst)
    std::string       kernelFilePath;       // ścieżka do pliku kernela na dysku
    NvrtcCompileResult lastCompile;         // wynik ostatniej kompilacji NVRTC
    bool              kernelAutoRerun = false;  // trigger po przeładowaniu kernela
};
```

---

## 10. Krok 8 — Modyfikacja `src/app.cpp`

### 10.1 Zmodyfikuj funkcję `appInit` (lub jak ją masz nazwana)

Dodaj inicjalizację Driver API i pierwszą kompilację kernela:

```cpp
// app.cpp
#include "app.h"
#include "kernel_manager.h"
// pozostałe includes...

void appInit(AppState& state) {
    // --- Istniejący kod z Fazy 1 ---
    // queryCudaDevice(state.deviceInfo);   // <- musi być wywołane PIERWSZE
    // state.cudaAvail = ...;
    // generacja sygnałów A i B...
    // state.dirty = true;
    //
    // TWÓJ KOD TUTAJ:
    // ...

    // --- NOWE: inicjalizacja Driver API ---
    // OBOWIĄZKOWO po dowolnym wywołaniu Runtime API (np. queryCudaDevice).
    if (state.cudaAvail) {
        if (!initCudaDriver()) {
            fprintf(stderr, "[App] initCudaDriver() failed — NVRTC nie będzie dostępne\n");
            state.cudaAvail = false;
        }
    }

    // --- NOWE: załaduj i skompiluj kernel startowy ---
    if (state.cudaAvail) {
        state.kernelFilePath = "kernels/convolution.cu";  // względem CWD

        state.kernelSource = loadKernelSourceFromFile(state.kernelFilePath);

        if (state.kernelSource.empty()) {
            fprintf(stderr, "[App] Brak pliku kernela: %s\n",
                    state.kernelFilePath.c_str());
            fprintf(stderr, "[App] Utwórz plik lub zmień ścieżkę.\n");
        } else {
            fprintf(stdout, "[App] Kompilacja kernela startowego...\n");
            state.lastCompile = state.kernelMgr.compile(
                state.kernelSource,
                state.deviceInfo.computeCapabilityMajor,
                state.deviceInfo.computeCapabilityMinor
            );
            if (state.lastCompile.success) {
                fprintf(stdout, "[App] Kernel OK (%.1f ms)\n",
                        state.lastCompile.compileTimeMs);
            } else {
                fprintf(stderr, "[App] Błąd kompilacji:\n%s\n",
                        state.lastCompile.log.c_str());
            }
        }
    }
}
```

### 10.2 Zmodyfikuj logikę dirty flag (jeśli jest w `app.cpp`)

Jeśli automatyczne odpalanie splotu jest w `app.cpp` (nie w `gui.cpp`),
dodaj sprawdzenie `kernelMgr.isReady()`:

```cpp
// Jeśli masz coś takiego w app.cpp:
void appUpdate(AppState& state) {
    if (state.dirty && state.cudaAvail && state.kernelMgr.isReady()) {
        runConvolution(
            state.signalA.data(),
            state.signalB.data(),
            state.convOutput.data(),
            state.N,
            state.kernelMgr.getFunction(),  // NOWE: przekaż uchwyt kernela
            state.convResult
        );
        // CPU reference + walidacja — bez zmian z Fazy 1
        state.dirty = false;
    }
}
```

> Jeśli Twoja dirty-flag logika jest w `gui.cpp`, zrób to samo tam.

---

## 11. Krok 9 — Modyfikacja `src/gui.cpp`

### 11.1 Dodaj panel NVRTC do istniejącego okna

Znajdź okno „Convolution" lub „Controls" i dodaj poniższy blok po istniejących
przyciskach/statystykach:

```cpp
// gui.cpp — w funkcji guiRender lub renderGui:

// --- SEKCJA NVRTC (Faza 2) ---
ImGui::Separator();
ImGui::TextUnformatted("── Kernel NVRTC ─────────────────────");

// Wyświetl ścieżkę do pliku kernela
ImGui::Text("Plik: %s", state.kernelFilePath.c_str());
ImGui::SameLine();

// Przycisk przeładowania
if (ImGui::Button("Przeładuj kernel")) {
    std::string newSource = loadKernelSourceFromFile(state.kernelFilePath);
    if (!newSource.empty()) {
        state.kernelSource  = newSource;
        state.lastCompile   = state.kernelMgr.compile(
            newSource,
            state.deviceInfo.computeCapabilityMajor,
            state.deviceInfo.computeCapabilityMinor
        );
        if (state.lastCompile.success) {
            state.dirty = true;  // przelicz splot z nowym kernelem
        }
    } else {
        state.lastCompile.success = false;
        state.lastCompile.log     = "Nie można odczytać pliku kernela.";
    }
}

// Status kompilacji
if (state.kernelMgr.isReady()) {
    ImGui::TextColored({0.0f, 1.0f, 0.3f, 1.0f},
        "Stan: OK  (%.1f ms)", state.lastCompile.compileTimeMs);
} else {
    ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f},
        "Stan: Brak skompilowanego kernela");
}

// Log kompilacji — pokaż jeśli nie jest pusty lub "OK"
if (!state.lastCompile.log.empty() && state.lastCompile.log != "OK") {
    ImGui::PushStyleColor(ImGuiCol_Text,
        state.lastCompile.success
            ? ImVec4(1.0f, 1.0f, 0.5f, 1.0f)   // żółty = ostrzeżenia
            : ImVec4(1.0f, 0.4f, 0.4f, 1.0f)); // czerwony = błędy

    // TextWrapped łamie długie linie automatycznie
    // Ogranicz widoczną wysokość przez scrollable child
    ImGui::BeginChild("##nvrtc_log", ImVec2(0, 80), true);
    ImGui::TextWrapped("%s", state.lastCompile.log.c_str());
    ImGui::EndChild();

    ImGui::PopStyleColor();
}
```

### 11.2 Zabezpiecz przycisk „Uruchom" przed brakiem kernela

Jeśli masz ręczny przycisk „Uruchom splot", dodaj warunek:

```cpp
// Przycisk aktywny tylko gdy kernel jest gotowy
bool canRun = state.cudaAvail && state.kernelMgr.isReady();
if (!canRun) ImGui::BeginDisabled();
if (ImGui::Button("Uruchom splot")) {
    runConvolution(
        state.signalA.data(),
        state.signalB.data(),
        state.convOutput.data(),
        state.N,
        state.kernelMgr.getFunction(),
        state.convResult
    );
    // walidacja CPU bez zmian...
}
if (!canRun) ImGui::EndDisabled();

// Opcjonalnie: tooltip gdy nieaktywny
if (!canRun && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("Przeładuj kernel żeby aktywować");
}
```

---

## 12. Kolejność inicjalizacji — KRYTYCZNE

Cały `main.cpp` pozostaje bez zmian. Zmieniają się tylko wnętrzności `appInit()`.
Oto obowiązkowa kolejność wywołań:

```
1. glfwInit() + glfwCreateWindow()          — okno
2. gladLoadGLLoader()                       — OpenGL
3. ImGui::CreateContext() + ImPlot           — GUI
4. queryCudaDevice(state.deviceInfo)        — Runtime API → tworzy primary context CUDA
5. initCudaDriver()                          — cuInit(0) → aktywuje Driver API
6. loadKernelSourceFromFile(path)            — wczytaj tekst z dysku
7. kernelMgr.compile(source, major, minor)  — NVRTC → PTX → cuModuleLoadData → CUfunction
```

**Dlaczego ta kolejność jest obowiązkowa:**

- Krok 4 musi być przed krokiem 5: `cuInit(0)` współpracuje poprawnie z Runtime API
  tylko jeśli Runtime API zainicjowało wcześniej primary context (przez `cudaGetDeviceCount`,
  `cudaSetDevice`, `cudaMalloc` lub jakiekolwiek inne wywołanie).
- Kroki 5, 6, 7 muszą być po 4: `cuModuleLoadData` (wewnątrz `compile()`) wymaga
  aktywnego kontekstu.
- Kroki 1–3 mogą być w dowolnej kolejności względem siebie (ale muszą być przed 4).

---

## 13. Budowanie projektu

```bat
:: Z katalogu projektu (gdzie jest premake5.lua)
tools\premake5.exe vs2022
```

Otwórz `.sln` w VS 2022. Wybierz `Debug | x64`. Build → Build Solution.

**Oczekiwane komunikaty w Output:**

```
1> NVCC: src/cuda/cuda_impl.cu
1>   [nvcc output — bez __global__, tylko host code]
1> GpuExperiment.vcxproj → build\bin\Debug\GpuExperiment.exe
```

`kernel_manager.cpp` będzie skompilowany przez `cl.exe` (MSVC), bez komunikatu NVCC.

**Oczekiwane komunikaty w konsoli po uruchomieniu:**

```
[App] Kompilacja kernela startowego...
[App] Kernel OK (543.2 ms)       ← pierwsza kompilacja NVRTC trwa 0.5–2 s
[CUDA] Device: NVIDIA GeForce RTX 2070 (SM 7.5)
```

---

## 14. Pułapki i rozwiązania

### P1 — `cuModuleGetFunction` zwraca błąd `CUDA_ERROR_NOT_FOUND`

**Objaw**: `cuModuleGetFunction("convolution") failed: named symbol not found`

**Przyczyna 1**: Brak `extern "C"` w pliku kernela.

**Rozwiązanie**: Sprawdź w `kernels/convolution.cu` czy jest:
```cuda
extern "C" __global__ void convolution(...)
```
Bez `extern "C"` kompilator C++ zmienia nazwę na coś w stylu
`_Z11convolutionPKdS0_Pdi` i `cuModuleGetFunction("convolution")` jej nie znajdzie.

**Przyczyna 2**: Literówka w nazwie — `KERNEL_FUNC_NAME` w `kernel_manager.cpp`
musi być dokładnie taki sam jak nazwa funkcji w kernelu.

---

### P2 — `cuModuleLoadData` zwraca błąd `CUDA_ERROR_NO_DEVICE`

**Objaw**: `cuModuleLoadData failed: no CUDA-capable device is detected`

**Przyczyna**: `cuModuleLoadData` wywołane przed `queryCudaDevice()` lub przed
`cuInit(0)`.

**Rozwiązanie**: Upewnij się, że kolejność inicjalizacji jest zgodna z
Sekcją 12. Sprawdź w `appInit()` czy `queryCudaDevice` jest wywoływane PRZED
`initCudaDriver()` i PRZED `kernelMgr.compile()`.

---

### P3 — Brak DLL: `nvrtc64_12x.dll` lub `nvrtc-builtins64_12x.dll`

**Objaw**: Aplikacja nie startuje, błąd systemu Windows o brakującym DLL.

**Przyczyna**: CUDA Toolkit nie dodał `%CUDA_PATH%\bin\` do PATH, lub PATH
nie jest odświeżony w tej sesji.

**Rozwiązanie A (trwałe)**: Sprawdź `sysdm.cpl` → Zaawansowane → Zmienne
środowiskowe. `CUDA_PATH\bin` powinien być w `Path`. Jeśli nie — dodaj.
Zrestartuj VS lub terminal.

**Rozwiązanie B (tymczasowe)**: Skopiuj z `%CUDA_PATH%\bin\`:
- `nvrtc64_12x_0.dll` (x = numer wersji, np. 120, 126)
- `nvrtc-builtins64_12x.dll`

do `build\bin\Debug\` (obok `.exe`). Dla Release: `build\bin\Release\`.

---

### P4 — Błąd NVRTC: `error: invalid target architecture`

**Objaw**: Log NVRTC zawiera `error: invalid target architecture` lub
`unknown option '--gpu-architecture=compute_XY'`

**Przyczyna**: Compute capability podana do `compile()` nie pasuje do zainstalowanego
CUDA Toolkit.

**Rozwiązanie**: Sprawdź `state.deviceInfo.computeCapabilityMajor/Minor`.
Dla RTX 2070 powinno być 7/5. Możesz też sprawdzić przez `nvidia-smi`:
```
nvidia-smi --query-gpu=compute_cap --format=csv
```

---

### P5 — LNK2019: unresolved external `nvrtcCreateProgram` lub `cuModuleLoadData`

**Objaw**: Błąd linkera.

**Przyczyna**: Brak `nvrtc.lib` lub `cuda.lib` w linkowanych bibliotekach.

**Rozwiązanie**: Sprawdź `premake5.lua` — blok `links` musi zawierać `"nvrtc"`
i `"cuda"`. Zregeneruj projekt przez `premake5 vs2022`.

---

### P6 — `cuLaunchKernel` kończy się sukcesem, ale wyniki są złe lub zerowe

**Objaw**: Kompilacja OK, launch OK (brak błędu), ale `C_out` jest pełne zer
lub bzdur.

**Przyczyna 1**: Parametry w tablicy `args[]` w złej kolejności lub złego
typu. Kernel widzi „śmieciowe" dane.

**Rozwiązanie**: Porównaj kolejność w `args[]` z sygnaturą kernela:
```cpp
// Kernel:  (const double* A, const double* B, double* C, int N)
void* args[] = { &d_A, &d_B, &d_C, &N };
//               ^A      ^B     ^C    ^N   — kolejność musi być identyczna
```

**Przyczyna 2**: `N` jest `size_t` a kernel oczekuje `int`. `size_t` to 8
bajtów, `int` to 4 bajty — kernel odczyta połowę wartości.

**Rozwiązanie**: Upewnij się że lokalna zmienna `N` w `runConvolution` jest
`int` i kernel też deklaruje `int N`.

---

### P7 — `kernel_manager.cpp` nie kompiluje się: `C2065: cuda.h: No such file`

**Objaw**: MSVC nie może znaleźć `cuda.h` lub `nvrtc.h`.

**Przyczyna**: `$(CUDA_PATH)/include` nie jest w ścieżkach includedirs dla
pliku `.cpp`.

**Rozwiązanie**: Sprawdź `premake5.lua` — w sekcji `includedirs` musi być:
```lua
cudaPath .. "/include",
```
Ta ścieżka powinna już być z Fazy 1. Zregeneruj projekt po zmianie.

---

### P8 — Stary kernel uruchamia się po `Przeładuj`

**Objaw**: Kliknięcie „Przeładuj kernel" daje wyniki jak stary kernel.

**Przyczyna**: `compile()` nie wywołała `unloadModule()` przed załadowaniem
nowego — `m_function` nadal wskazuje na starą funkcję.

**Rozwiązanie**: Sprawdź w `kernel_manager.cpp` czy `unloadModule()` jest
wywoływane na początku sekcji ładowania PTX (po `nvrtcDestroyProgram`):
```cpp
unloadModule();  // MUSI być przed cuModuleLoadData
CUresult cuErr = cuModuleLoadData(&m_module, ptx.c_str());
```

---

### P9 — Plik kernela nie jest znajdowany przy uruchomieniu z exe bezpośrednio

**Objaw**: `[KernelManager] Nie można otworzyć: kernels/convolution.cu`
przy uruchomieniu `build\bin\Debug\GpuExperiment.exe` bezpośrednio.

**Przyczyna**: Katalog roboczy exe to `build\bin\Debug\`, a nie katalog projektu.

**Rozwiązanie A (zalecane)**: Uruchamiaj z VS debuggera — tam CWD = katalog projektu.

**Rozwiązanie B**: Zmień w `appInit` ścieżkę na względną od exe:
```cpp
// Wylicz ścieżkę do kernela od executable
// W prawdziwym projekcie można użyć _pgmptr lub GetModuleFileName
state.kernelFilePath = "kernels/convolution.cu";  // dla uruchomienia z VS
// Alternatywnie — absolutna ścieżka lub parametr CLI
```

**Rozwiązanie C**: Dodaj w VS właściwości projektu: `Debugging → Working Directory`
= `$(SolutionDir)` (lub ścieżka do głównego katalogu projektu).

---

### P10 — Błąd `CUDA_ERROR_INVALID_PTX`

**Objaw**: `cuModuleLoadData failed: invalid PTX` mimo że NVRTC raportuje sukces.

**Przyczyna**: PTX wygenerowany przez NVRTC jest niekompatybilny z wersją
sterownika GPU.

**Najczęstsza przyczyna**: Zbyt nowa wersja CUDA Toolkit dla starszego sterownika.
NVRTC generuje PTX dla nowszego ISA niż sterownik obsługuje.

**Rozwiązanie**: Zaktualizuj sterownik NVIDIA do najnowszej wersji. Albo
użyj niższej wartości `compute_XX` w opcjach NVRTC (np. `compute_70` zamiast
`compute_75`).

---

## 15. Checklista zaliczenia Fazy 2

Zaznacz każdy punkt przed przejściem do Fazy 3.

```
Infrastruktura:
[ ] premake5 vs2022 — brak błędów Lua, projekt wygenerowany
[ ] Build Debug — kompiluje się bez błędów
[ ] Build Release — kompiluje się bez błędów
[ ] kernel_manager.cpp widoczny w Output jako kompilowany przez cl.exe (MSVC)
[ ] cuda_impl.cu widoczny w Output jako kompilowany przez nvcc.exe
[ ] kernels/convolution.cu widoczny w Solution Explorer, NIE kompilowany

Uruchomienie:
[ ] Aplikacja startuje bez crash
[ ] W konsoli pojawia się "Kernel OK (X ms)" przy starcie
[ ] Panel CUDA Device wyświetla dane GPU
[ ] Wykres sygnałów renderuje się poprawnie

Funkcjonalność NVRTC:
[ ] Po starcie: wynik splotu jest identyczny jak w Fazie 1 (wizualnie)
[ ] maxAbsError < 1e-9 (ta sama tolerancja)
[ ] Czas kompilacji NVRTC wyświetlany w UI
[ ] Status "OK" w UI po starcie

Hot-reload:
[ ] Zmodyfikuj kernels/convolution.cu (np. zmień `+=` na `-=` lub dodaj komentarz)
[ ] Kliknij "Przeładuj kernel"
[ ] Aplikacja NIE restartuje się
[ ] Nowy wynik pojawia się automatycznie (dirty flag)
[ ] Czas kompilacji NVRTC aktualizuje się w UI

Obsługa błędów:
[ ] Wstaw błąd składni w kernels/convolution.cu (np. usuń `}`)
[ ] Kliknij "Przeładuj kernel"
[ ] UI wyświetla log błędu NVRTC (nie crash, nie biały ekran)
[ ] Stary kernel nadal działa (wynik splotu nadal widoczny)
[ ] Po poprawieniu błędu i przeładowaniu — wynik poprawny

Kompatybilność z Fazą 1:
[ ] Dirty flag nadal działa (zmiana parametrów sygnałów → auto-przeliczenie)
[ ] Parametry sygnałów edytowalne jak w Fazie 1
[ ] maxAbsError nadal < 1e-9
[ ] Zamknięcie okna kończy aplikację bez hang
```

---

## Appendix A — Jak przetestować hot-reload ręcznie

1. Uruchom aplikację. Splot powinien być widoczny.
2. Otwórz `kernels/convolution.cu` w dowolnym edytorze tekstu (np. VS Code).
3. Zmień:
   ```cuda
   sum += A[k] * B[idx];
   ```
   na:
   ```cuda
   sum += A[k] * B[idx] * 2.0;  // splot x2
   ```
4. Zapisz plik.
5. W aplikacji kliknij „Przeładuj kernel".
6. Wynik splotu na wykresie powinien być dokładnie dwukrotnie większy.
7. Przywróć oryginalną linię, przeładuj ponownie — wynik wraca.

---

## Appendix B — Minimalna struktura do debugowania NVRTC

Jeśli kompilacja NVRTC nie działa i nie wiadomo dlaczego, stwórz tymczasowy
test w `main.cpp` (przed pętlą renderowania):

```cpp
// TEST NVRTC — usuń po weryfikacji
const char* testSrc = R"(
extern "C" __global__ void testKernel(float* out) {
    out[threadIdx.x] = (float)threadIdx.x * 2.0f;
}
)";

nvrtcProgram prog;
nvrtcCreateProgram(&prog, testSrc, "test.cu", 0, nullptr, nullptr);
const char* opts[] = { "--gpu-architecture=compute_75" };  // TWOJE compute cap
nvrtcResult r = nvrtcCompileProgram(prog, 1, opts);

size_t logSz;
nvrtcGetProgramLogSize(prog, &logSz);
std::string log(logSz, '\0');
nvrtcGetProgramLog(prog, log.data());

fprintf(stdout, "NVRTC test: %s\nLog: %s\n",
    r == NVRTC_SUCCESS ? "OK" : "FAIL", log.c_str());
nvrtcDestroyProgram(&prog);
```

Ten test pozwala zweryfikować NVRTC niezależnie od reszty aplikacji.

---

*Gotowe. Powodzenia z Fazą 2.*