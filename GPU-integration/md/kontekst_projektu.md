# Kontekst projektu GPU — skrót ustaleń

## Cel projektu

Nauka posługiwania się GPU: podłączenie, hot-reload kerneli, synchronizacja CPU↔GPU.
Obliczenia są **pretekstem** do przetestowania infrastruktury.
Projekt rośnie iteracyjnie przez kolejne fazy — każda dodaje dokładnie jedną nową warstwę.

---

## Stos technologiczny

| Komponent      | Wybór                         | Uwagi                                   |
| -------------- | ----------------------------- | --------------------------------------- |
| Build system   | Premake5 (binarny w `tools/`) | Metoda B: custom build rules dla `.cu`  |
| Kompilator C++ | MSVC (Visual Studio 2022)     | C++17                                   |
| Kompilator GPU | NVCC (CUDA Toolkit 12.x)      | Custom Build Tool w VS                  |
| GUI            | Dear ImGui + ImPlot           | OpenGL 3.3 Core backend                 |
| Okno/kontekst  | GLFW 3.4                      | Prebuilt vc2022                         |
| Loader OpenGL  | GLAD (OpenGL 3.3 Core)        | Generowany z glad.dav1d.de              |
| Zależności     | `vendor/` ręcznie             | Brak vcpkg, submodules, skryptów        |
| CUDA Runtime   | Dynamiczny (`cudart.lib`)     | Statyczny zbędnie komplikuje linkowanie |

---

## Architektura — 5 faz

### Faza 1 — Weryfikacja stosu (AKTUALNA)
- Statyczny kernel NVCC, synchroniczne wywołanie z głównego wątku
- Splot dwóch sygnałów (Gaussian + prostokąt), N=4096, typ `double`
- CPU reference + walidacja `maxAbsError < 1e-9`
- ImPlot: wykres sygnałów A, B, wynik GPU, wynik CPU
- **Jeden plik CUDA**: `src/cuda/cuda_impl.cu` (kernel + implementacja razem)
- Dokumentacja: `faza1_implementacja.md`

### Faza 2 — NVRTC: hot-reload kernela
- Kernel jako string kompilowany przez NVRTC w runtime
- Nowe zależności: `nvrtc.lib`, `cuda.lib` (Driver API)
- Klasa `KernelManager`: compile(source) → cuModuleLoad → cuModuleGetFunction
- Przycisk „Przeładuj kernel" w UI: wczytaj plik → NVRTC → nowy wynik bez restartu
- Czas kompilacji NVRTC wyświetlany w UI (~0.3–2 s dla prostych kerneli)

### Faza 3 — Asynchroniczny wątek GPU
- `GpuWorkerThread`: jeden wątek, kolejka zadań, `cudaStream_t`
- Schemat: `Service → task_queue → GPU worker → cudaStream → callback/std::future`
- UI pozostaje responsywne podczas obliczeń
- Kernel wciąż z Fazy 2 (NVRTC)

### Faza 4 — Node editor: budowanie sygnałów + generacja kodu
- Biblioteka: `imnodes` (vendor/)
- Użytkownik buduje sygnał z prymitywów → generator kodu → NVRTC → splot
- Wyświetlanie wygenerowanego kodu CUDA w UI

### Faza 5 — Dwa sygnały z node editora, skalowalność
- Dwa niezależne grafy (Sygnał A, Sygnał B)
- Skalowalne N (1024–65536) przez UI
- Pełna walidacja CPU vs GPU

---

## Obliczenia: splot dwóch sygnałów

**Operacja** (ta sama przez wszystkie fazy, zmienia się tylko infrastruktura):
```
C[n] = sum_{k=0}^{N-1} A[k] * B[n-k]
```
Jeden wątek GPU per punkt wyjściowy. Trivialnie paralelizowalne.

**Sygnały (Faza 1, hardcoded):**
- A: Gaussowski, `mu=0.30`, `sigma=0.05`
- B: Prostokąt, wartość 1.0 dla `t ∈ [0.60, 0.80]`
- `N = 4096`, `T = 1.0 s`, `dt = T/(N-1)`

**Tolerancja walidacji:** `maxAbsError < 1e-9` (absolutna, element-wise)

---

## Prymitywy węzłów sygnałów (Fazy 4–5)

**Generatory:**
Stała, Sinus, Cosinus, Prostokąt, Trójkąt, Piłokształtny, Gaussowski,
Zanikanie eksponencjalne, Skok jednostkowy (Heaviside), Sinc

**Operatory:**
Suma (2 wejścia), Iloczyn (2 wejścia), Skalowanie (1 wejście + param `a`),
Przesunięcie w czasie (1 wejście + param `τ`), Odbicie (1 wejście)

---

## Kluczowe decyzje techniczne

**CUDA + Premake5:** Metoda B — własne reguły kompilacji w `premake5.lua`.
`filter { "files:**.cu" }` z `buildcommands`, `buildoutputs "$(IntDir)%{file.basename}.obj"`.
VS automatycznie linkuje `.obj` z `$(IntDir)`.

**Jeden plik `.cu` w Fazie 1:** Unika problemu device linking między wieloma
plikami nvcc (bez `-rdc=true` i `nvdlink`). W Fazie 2 kernel i tak przechodzi do NVRTC.

**Simplex:** Nie w tym eksperymencie. Zbyt złożone. Osobny projekt w przyszłości.

**Hot-reload (Faza 2):** NVRTC kompiluje model (string) → PTX → cuModuleLoad.
Simplex i MC (gdy dojdą) będą szablonami źródłowymi sklejanymi z modelem i kompilowanymi
razem przez NVRTC. Nie ma tradycyjnych „dynamicznych bibliotek GPU" — GPU nie ma dynamicznego linkera.

**Wielowątkowość (Faza 3):** Jeden dedykowany GPU worker thread z kolejką zadań
i `cudaStream_t`. Wiele serwisów CPU zgłasza zadania asynchronicznie, dostaje `std::future`.
Nie wiele wątków CPU dzielących kontekst CUDA.

**Funkcje obliczeniowe:** `enum + switch` po stronie CPU wybiera kernel.
Osobne implementacje CPU i GPU — nie próbować współdzielić przez `__host__ __device__`.

**Runtime MSVC:** Krytyczne — `/MD` (Release) i `/MDd` (Debug) muszą być zgodne
między MSVC i nvcc (`-Xcompiler "/MDd"`). Niezgodność → LNK2038.

---

## Struktura plików (Faza 1)

```
gpu-experiment/
├── premake5.lua
├── tools/premake5.exe
├── vendor/
│   ├── glfw/          (include/ + lib-vc2022/)
│   ├── imgui/         (pliki źródłowe .h/.cpp)
│   ├── implot/        (implot.h, implot.cpp, implot_items.cpp)
│   └── glad/          (include/ + src/glad.c)
└── src/
    ├── main.cpp
    ├── app.h / app.cpp
    ├── gui.h / gui.cpp
    ├── cuda_interface.h
    └── cuda/
        └── cuda_impl.cu
```

---

## Pliki wygenerowane w tej sesji

| Plik                                 | Zawartość                                             |
| ------------------------------------ | ----------------------------------------------------- |
| `analiza_techniczna_cuda_premake.md` | Pełna analiza techniczna (10 sekcji)                  |
| `todo_gpu_experiment.md`             | Szczegółowy plan TODO wszystkich 5 faz                |
| `faza1_implementacja.md`             | Kompletna specyfikacja impl. Fazy 1 dla Codex/Copilot |

---

## Krytyczne pułapki (Faza 1)

1. `$(IntDir)` w `buildoutputs` musi być identyczne z wyjściem nvcc — inaczej brak linkowania
2. `glad.h` **przed** `glfw3.h` w każdym pliku — inaczej błąd kompilacji
3. `-Xcompiler "/MDd"` w Debug, `-Xcompiler "/MD"` w Release — inaczej LNK2038
4. `cudaGetLastError()` po `<<<>>>` łapie tylko błąd launchu; błędy wykonania wychodzą przy synchronizacji
5. `cudaEventElapsedTime` zwraca `float*` — nie `double*`
6. Wszystkie zmienne CUDA deklarowane przed pierwszym `CUDA_CHECK` z goto (wymóg C++)
7. `CUDA_PATH` musi być ustawione — walidować w premake5.lua przez `assert`

---

## Następny krok

Implementacja Fazy 1 zgodnie z `faza1_implementacja.md`.
Po zaliczeniu wszystkich checklistów przejście do Fazy 2 (NVRTC).
