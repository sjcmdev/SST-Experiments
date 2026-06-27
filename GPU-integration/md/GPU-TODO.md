# Szczegółowy plan TODO — eksperyment GPU: splot sygnałów

## Przegląd faz

| Faza | Co się zmienia | Nowa infrastruktura | Obliczenia |
|---|---|---|---|
| 1 | Wszystko od zera | Premake + MSVC + NVCC + GLFW + ImGui + GLAD | Splot, statyczny kernel |
| 2 | Kernel → runtime | NVRTC + Driver API | Splot, ten sam |
| 3 | Wywołanie → async | GPU worker thread + task queue | Splot, ten sam |
| 4 | Model → z UI | Node editor + generator kodu | Splot, generowany |
| 5 | Dwa sygnały z UI | Dwa grafy, skalowalność | Splot końcowy |

## Zależności per faza

| Biblioteka | Faza 1 | Faza 2 | Faza 3 | Faza 4 | Faza 5 |
|---|---|---|---|---|---|
| GLFW 3.4 (prebuilt vc2022) | ✅ | — | — | — | — |
| Dear ImGui (core + glfw + opengl3) | ✅ | — | — | — | — |
| GLAD (OpenGL 3.3 Core) | ✅ | — | — | — | — |
| ImPlot | ✅ | — | — | — | — |
| CUDA Runtime API (`cudart.lib`) | ✅ | — | — | — | — |
| NVRTC (`nvrtc.lib`) | — | ✅ | — | — | — |
| CUDA Driver API (`cuda.lib`) | — | ✅ | — | — | — |
| imnodes | — | — | — | ✅ | — |

---

## Faza 1 — Weryfikacja stosu + splot (statyczny kernel)

**Cel**: cały stos technologiczny kompiluje się, działa i produkuje poprawny wynik.
Splot dwóch sygnałów na GPU, wywołanie synchroniczne z głównego wątku.

---

### 1.1 Struktura projektu i Premake5

- [ ] Utworzyć katalog główny projektu
- [ ] Dodać `tools/premake5.exe` (binarny, nie instalowany systemowo)
- [ ] Napisać `premake5.lua`:
  - [ ] Workspace z jednym projektem `ConsoleApp`
  - [ ] Konfiguracje: Debug (`/MDd`) i Release (`/MD`)
  - [ ] `includedirs` dla wszystkich zależności z `vendor/`
  - [ ] `libdirs`: `vendor/glfw/lib-vc2022`, `$(CUDA_PATH)/lib/x64`
  - [ ] `links`: `cudart`, `glfw3`, `opengl32`, `gdi32`, `user32`
  - [ ] Custom Build Tool dla `*.cu` (Metoda B):
    - [ ] `buildmessage`, `buildcommands` z wywołaniem nvcc
    - [ ] Oddzielny filtr dla Debug (flagi: `-G -O0 -Xcompiler "/MDd"`)
    - [ ] Oddzielny filtr dla Release (flagi: `-O2 -Xcompiler "/MD"`)
    - [ ] Architektura: `arch=compute_75,code=sm_75`
    - [ ] `buildoutputs`: plik `.obj` do `cfg.objdir`
  - [ ] Walidacja `CUDA_PATH` przy generowaniu: `assert(os.getenv("CUDA_PATH"), ...)`
  - [ ] `.gitignore` dla katalogu `build/`

---

### 1.2 Zależności w `vendor/`

- [ ] GLFW:
  - [ ] Pobrać GLFW 3.4 prebuilt Windows (lib-vc2022)
  - [ ] Skopiować `include/GLFW/` i `lib-vc2022/glfw3.lib` do `vendor/glfw/`
- [ ] Dear ImGui:
  - [ ] Pobrać źródła (tag stabilny)
  - [ ] Skopiować do `vendor/imgui/`: `imgui.h`, `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`, `imgui_impl_glfw.h/.cpp`, `imgui_impl_opengl3.h/.cpp`
- [ ] GLAD:
  - [ ] Wygenerować przez glad.dav1d.de: OpenGL 3.3, Core, loader
  - [ ] Skopiować `glad.c`, `glad.h`, `khrplatform.h` do `vendor/glad/`
- [ ] ImPlot:
  - [ ] Pobrać źródła ImPlot (tag stabilny)
  - [ ] Skopiować `implot.h`, `implot.cpp`, `implot_items.cpp` do `vendor/implot/`

---

### 1.3 Struktura plików źródłowych

- [ ] `src/main.cpp` — punkt wejścia
- [ ] `src/app.h` / `src/app.cpp` — stan aplikacji
- [ ] `src/gui.h` / `src/gui.cpp` — okna ImGui
- [ ] `src/cuda_interface.h` — publiczny interfejs CUDA (bez składni CUDA)
- [ ] `src/cuda/cuda_ops.cu` — implementacja CUDA (alokacja, transfer, launch, sync)
- [ ] `src/cuda/kernel.cu` — definicja kernela splotu

---

### 1.4 Inicjalizacja aplikacji (`main.cpp`)

- [ ] Inicjalizacja GLFW:
  - [ ] `glfwInit()`
  - [ ] `glfwWindowHint` dla OpenGL 3.3 Core
  - [ ] `glfwCreateWindow`
  - [ ] Obsługa błędu: log + `return 1`
- [ ] Inicjalizacja GLAD:
  - [ ] `gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)`
  - [ ] Obsługa błędu: log + `return 1`
- [ ] Inicjalizacja ImGui:
  - [ ] `ImGui::CreateContext()`
  - [ ] `ImGui_ImplGlfw_InitForOpenGL(window, true)`
  - [ ] `ImGui_ImplOpenGL3_Init("#version 330")`
- [ ] Inicjalizacja ImPlot:
  - [ ] `ImPlot::CreateContext()`
- [ ] Inicjalizacja CUDA:
  - [ ] Wywołanie `queryCudaDevice(deviceInfo)`
  - [ ] Jeśli false: wyświetlenie komunikatu w UI, kontynuacja bez GPU
- [ ] Pętla renderowania (kolejność obowiązkowa):
  - [ ] `glfwPollEvents()`
  - [ ] `ImGui_ImplOpenGL3_NewFrame()` / `ImGui_ImplGlfw_NewFrame()` / `ImGui::NewFrame()`
  - [ ] `renderGui()`
  - [ ] `ImGui::Render()` + `glClear` + `ImGui_ImplOpenGL3_RenderDrawData`
  - [ ] `glfwSwapBuffers`
- [ ] Sprzątanie przy zamknięciu (odwrotna kolejność inicjalizacji)

---

### 1.5 Definicja sygnałów (hardcoded w Fazie 1)

- [ ] Zdefiniować stałe: `N = 4096`, `T = 1.0`, `dt = T / (N - 1)`
- [ ] Sygnał A: Gaussowski `A[i] = exp(-(t - 0.5)² / (2 · 0.01²))`
- [ ] Sygnał B: prostokąt `B[i] = 1.0` dla `t ∈ [0.4, 0.6]`, `0.0` poza
- [ ] Wypełniać tablice `double` po stronie CPU przed przekazaniem do GPU

---

### 1.6 Kernel splotu (`kernel.cu`)

- [ ] Kernel: jeden wątek per punkt wyjściowy `n`
- [ ] Dla każdego wątku: `C[n] = Σ A[k] · B[n - k]` dla `k` gdzie `n - k ∈ [0, N-1]`
- [ ] Konfiguracja launch: `blockDim = 256`, `gridDim = (N + 255) / 256`
- [ ] Typ danych wyłącznie `double`

---

### 1.7 Interfejs CUDA (`cuda_interface.h` + `cuda_ops.cu`)

- [ ] Zdefiniować struktury:
  ```
  CudaDeviceInfo { name, totalMemoryBytes, computeCapabilityMajor/Minor }
  ConvolutionResult { success, errorMessage, transferToGpuMs, kernelMs,
                      transferFromGpuMs, maxAbsError }
  ```
- [ ] Zaimplementować `queryCudaDevice(CudaDeviceInfo&)`:
  - [ ] `cudaGetDeviceCount` — jeśli 0, return false
  - [ ] `cudaGetDeviceProperties`
- [ ] Zaimplementować `runConvolution(double* A, double* B, double* C_out, size_t N, ConvolutionResult&)`:
  - [ ] `cudaMalloc` dla A, B, C na GPU
  - [ ] `cudaMemcpy` H2D dla A i B (mierzone `cudaEvent_t`)
  - [ ] Launch kernela (mierzony `cudaEvent_t`)
  - [ ] `cudaDeviceSynchronize()`
  - [ ] `cudaMemcpy` D2H dla C (mierzony `cudaEvent_t`)
  - [ ] `cudaFree` dla wszystkich buforów
  - [ ] Makro `CUDA_CHECK` przy każdym wywołaniu CUDA
- [ ] CPU reference: pętla `C_cpu[n] = Σ A[k] · B[n-k]` w `cuda_ops.cu` (lub `app.cpp`)
- [ ] Walidacja: `max|C_gpu[n] - C_cpu[n]|` → do `ConvolutionResult.maxAbsError`
- [ ] Tolerancja: `1e-9` absolutna

---

### 1.8 UI (`gui.cpp`)

- [ ] Okno "Urządzenie CUDA": nazwa GPU, compute capability, pamięć
- [ ] Okno "Obliczenia":
  - [ ] Przycisk "Uruchom splot"
  - [ ] Po uruchomieniu: czasy transferów, czas kernela, max błąd, status OK/BŁĄD
- [ ] Wykres ImPlot z trzema liniami: Sygnał A, Sygnał B, Wynik splotu
  - [ ] Wyświetlać co `k`-ty punkt jeśli N jest duże (decimacja dla czytelności)

---

### 1.9 Kryteria zaliczenia Fazy 1

- [ ] `premake5 vs2022` generuje `.sln` bez błędów
- [ ] Build Debug i Release bez błędów i ostrzeżeń
- [ ] Pliki `.cpp` kompilowane przez `cl.exe`, pliki `.cu` przez `nvcc.exe`
- [ ] Okno GLFW + ImGui pojawia się i jest responsywne
- [ ] Przycisk "Uruchom splot" produkuje wynik
- [ ] `max|C_gpu - C_cpu| < 1e-9`
- [ ] Wykres splotu wygląda poprawnie (sprawdzenie wizualne)
- [ ] Czasy transferów i kernela > 0 ms
- [ ] Brak GPU → komunikat w UI, brak crash

---

## Faza 2 — NVRTC: hot-reload kernela

**Cel**: kernel przestaje być statycznie skompilowany. Plik źródłowy `.cu` (lub string) jest kompilowany przez NVRTC w trakcie działania aplikacji. Przycisk "Przeładuj" rekompiluje i uruchamia ponownie bez restartu.

---

### 2.1 Nowe zależności w `premake5.lua`

- [ ] Dodać `nvrtc.lib` do `links`
- [ ] Dodać `cuda.lib` do `links` (Driver API)
- [ ] Dodać `nvrtc.dll` i `cuda.dll` do ścieżki uruchomieniowej (są w `CUDA_PATH/bin`)

---

### 2.2 Klasa `KernelManager`

Nowy plik: `src/kernel_manager.h` / `src/kernel_manager.cpp`

- [ ] Przechowuje: string z kodem źródłowym kernela, uchwyty Driver API (`CUmodule`, `CUfunction`)
- [ ] Metoda `compile(const std::string& source)`:
  - [ ] Inicjalizacja NVRTC: `nvrtcCreateProgram`
  - [ ] Kompilacja: `nvrtcCompileProgram` z flagami (`--gpu-architecture=compute_75`)
  - [ ] Pobranie logu błędów jeśli kompilacja się nie powiodła
  - [ ] Pobranie PTX: `nvrtcGetPTX`
  - [ ] Załadowanie PTX przez Driver API: `cuModuleLoadData`
  - [ ] Pobranie uchwytu funkcji: `cuModuleGetFunction`
  - [ ] Zwolnienie poprzedniego modułu jeśli istniał (`cuModuleUnload`)
- [ ] Metoda `isReady()` → bool
- [ ] Metoda `getFunction()` → `CUfunction`
- [ ] Obsługa błędów: log kompilacji zwracany jako string do UI

---

### 2.3 Przeniesienie kernela splotu do stringa

- [ ] Wyodrębnić kod kernela z `kernel.cu` do pliku tekstowego `kernels/convolution.cu`
- [ ] Wczytywać plik jako `std::string` przy starcie aplikacji
- [ ] Przekazywać string do `KernelManager::compile()`
- [ ] Zmienić `runConvolution` w `cuda_ops.cu` żeby używało `cuLaunchKernel` zamiast `<<<>>>`
  - [ ] Parametry kernela przekazywane przez tablicę wskaźników (`void* args[]`)

---

### 2.4 Inicjalizacja Driver API

- [ ] Dodać inicjalizację kontekstu CUDA Driver API:
  - [ ] `cuInit(0)`
  - [ ] `cuDeviceGet` / `cuCtxCreate`
  - [ ] Uwaga: Runtime API i Driver API współdzielą kontekst jeśli runtime był zainicjalizowany pierwszy — sprawdzić kolejność inicjalizacji
- [ ] Inicjalizacja w `queryCudaDevice` lub osobna funkcja `initCudaDriver()`

---

### 2.5 UI: przeładowanie kernela

- [ ] Wyświetlić ścieżkę wczytanego pliku kernela
- [ ] Przycisk "Przeładuj kernel":
  - [ ] Wczytaj plik ponownie z dysku
  - [ ] Wywołaj `KernelManager::compile()`
  - [ ] Wyświetl log kompilacji (błędy lub "OK")
  - [ ] Wyświetl czas kompilacji NVRTC
- [ ] Pole tekstowe z edytowalnym kodem kernela (opcjonalne w tej fazie — można zostawić na Fazę 4)

---

### 2.6 Kryteria zaliczenia Fazy 2

- [ ] Kernel kompilowany przez NVRTC przy starcie — wynik identyczny jak w Fazie 1
- [ ] Modyfikacja pliku `convolution.cu` + "Przeładuj" → nowy wynik bez restartu aplikacji
- [ ] Błąd składni w kernelu → log błędu w UI, brak crash
- [ ] Czas kompilacji NVRTC wyświetlany w UI
- [ ] Wszystkie kryteria Fazy 1 nadal spełnione

---

## Faza 3 — Asynchroniczny wątek GPU

**Cel**: `runConvolution` przestaje blokować wątek główny. GPU worker thread obsługuje kolejkę zadań. Serwis zgłasza zadanie i dostaje `std::future`.

---

### 3.1 Klasa `GpuWorkerThread`

Nowy plik: `src/gpu_worker.h` / `src/gpu_worker.cpp`

- [ ] Prywatny wątek (`std::thread`)
- [ ] Kolejka zadań: `std::queue<GpuTask>` + `std::mutex` + `std::condition_variable`
- [ ] `cudaStream_t` należący do wątku (tworzony w wątku, nie w main)
- [ ] Metoda `submit(GpuTask)` → `std::future<ConvolutionResult>`
- [ ] Metoda `shutdown()` — opróżnia kolejkę i kończy wątek
- [ ] Wątek pracuje w pętli: czeka na zadanie → wykonuje → fulfills promise

---

### 3.2 Struktura `GpuTask`

- [ ] Pola: wskaźniki na dane wejściowe (lub kopia), rozmiar N
- [ ] `std::promise<ConvolutionResult>` przekazywane do wątku
- [ ] Dane wejściowe kopiowane do zadania (nie przekazywać surowych wskaźników z UI)

---

### 3.3 Integracja z UI

- [ ] Przycisk "Uruchom splot" → `gpuWorker.submit(task)` → dostaje `future`
- [ ] UI przechowuje `future` i w każdej klatce sprawdza `future.wait_for(0ms)`
- [ ] Gdy future gotowy: odczytaj wynik, wyświetl w UI
- [ ] Podczas oczekiwania: UI pokazuje spinner / "Obliczanie..."
- [ ] UI pozostaje responsywne podczas obliczeń GPU

---

### 3.4 Kryteria zaliczenia Fazy 3

- [ ] Naciśnięcie przycisku nie blokuje UI
- [ ] UI renderuje się płynnie podczas obliczeń GPU
- [ ] Wynik pojawia się automatycznie po zakończeniu
- [ ] Nie można zgłosić drugiego zadania gdy pierwsze jest w toku (lub można — zdefiniować zachowanie)
- [ ] Shutdown aplikacji czeka na zakończenie wątku GPU
- [ ] Wszystkie kryteria Faz 1 i 2 nadal spełnione

---

## Faza 4 — Node editor: budowanie sygnałów + generacja kodu

**Cel**: użytkownik buduje sygnały z prymitywów w node editorze. Aplikacja generuje kod CUDA kernela, kompiluje przez NVRTC i uruchamia splot.

---

### 4.1 Nowe zależności

- [ ] Pobrać `imnodes` (https://github.com/Nelarius/imnodes)
- [ ] Skopiować `imnodes.h` / `imnodes.cpp` do `vendor/imnodes/`
- [ ] Dodać do `premake5.lua`

---

### 4.2 Model danych grafu

Nowy plik: `src/signal_graph.h` / `src/signal_graph.cpp`

- [ ] `NodeType` — enum dla wszystkich typów węzłów
- [ ] `Node` — struct: id, type, parametry (union lub `std::variant`), pozycja w edytorze
- [ ] `Link` — struct: id, id węzła wyjściowego, id węzła wejściowego
- [ ] `SignalGraph` — kolekcja węzłów i połączeń
- [ ] Metoda `isValid()` — sprawdza czy graf ma dokładnie jeden węzeł wyjściowy
- [ ] Metoda `topologicalSort()` — zwraca węzły w kolejności ewaluacji

---

### 4.3 Węzły sygnałów

Dla każdego węzła: typ, lista parametrów z wartościami domyślnymi, fragment kodu CUDA.

**Generatory (węzły bez wejść):**

| Węzeł | Parametry | Fragment kodu CUDA |
|---|---|---|
| Stała | `c` | `double out = c;` |
| Sinus | `A, f, phi` | `double out = A * sin(2.0*M_PI*f*t + phi);` |
| Cosinus | `A, f, phi` | `double out = A * cos(2.0*M_PI*f*t + phi);` |
| Prostokąt | `A, t0, t1` | `double out = (t >= t0 && t <= t1) ? A : 0.0;` |
| Trójkąt | `A, t0, t_peak, t1` | liniowe narastanie/opadanie |
| Piłokształtny | `A, f` | `double out = A * fmod(f*t, 1.0);` |
| Gaussowski | `A, mu, sigma` | `double out = A * exp(-(t-mu)*(t-mu)/(2.0*sigma*sigma));` |
| Zanikanie eksp. | `A, lambda, t0` | `double out = (t >= t0) ? A * exp(-lambda*(t-t0)) : 0.0;` |
| Skok jednostkowy | `A, t0` | `double out = (t >= t0) ? A : 0.0;` |
| Sinc | `A, f` | `double _x = M_PI*f*t; double out = (_x != 0.0) ? A*sin(_x)/_x : A;` |

**Operatory (węzły z jednym lub dwoma wejściami):**

| Węzeł | Wejścia | Fragment kodu CUDA |
|---|---|---|
| Suma | 2 | `double out = in0 + in1;` |
| Iloczyn | 2 | `double out = in0 * in1;` |
| Skalowanie | 1 + param `a` | `double out = a * in0;` |
| Przesunięcie w czasie | 1 + param `tau` | (obsługiwane przez indeks próbki) |
| Odbicie | 1 | (obsługiwane przez indeks próbki) |

- [ ] Dla każdego węzła zaimplementować strukturę z parametrami i metodę `generateCode()`
- [ ] Parametry edytowalne przez ImGui w panelu bocznym po kliknięciu węzła

---

### 4.4 Generator kodu CUDA

Nowy plik: `src/code_generator.h` / `src/code_generator.cpp`

- [ ] Metoda `generate(const SignalGraph& graph, const std::string& functionName)` → `std::string`
- [ ] Generuje kompletną funkcję `__device__ double <functionName>(double t)` obejmującą:
  - [ ] Zmienne lokalne dla każdego węzła (w kolejności topologicznej)
  - [ ] Przypisania fragmentów kodu węzłów
  - [ ] Return wartości węzła wyjściowego
- [ ] Generuje kompletny plik `.cu` (string) z: `#include <math.h>`, kernel splotu wywołujący obie funkcje sygnałów
- [ ] Unit test generatora: sprawdzenie czy wygenerowany kod kompiluje się przez NVRTC

---

### 4.5 Integracja z pipeline NVRTC (Faza 2)

- [ ] Przycisk "Kompiluj i uruchom":
  - [ ] `CodeGenerator::generate(graphA, "signalA")` + `CodeGenerator::generate(graphB, "signalB")`
  - [ ] Sklejenie obu w jeden plik źródłowy z kernelem splotu
  - [ ] `KernelManager::compile(source)`
  - [ ] Log kompilacji w UI
  - [ ] Jeśli OK: `GpuWorkerThread::submit(task)`

---

### 4.6 UI: node editor

- [ ] Panel lewego bocznego: lista dostępnych węzłów (drag & drop lub przycisk "Dodaj")
- [ ] Canvas imnodes: przeciąganie węzłów, rysowanie połączeń
- [ ] Panel prawego bocznego: parametry wybranego węzła (ImGui sliders/inputs)
- [ ] Przycisk "Kompiluj i uruchom" (aktywny jeśli graf jest poprawny)
- [ ] Wyświetlanie wygenerowanego kodu CUDA (pole tekstowe tylko do odczytu lub osobne okno)
- [ ] Wyświetlanie logu kompilacji NVRTC
- [ ] Czas kompilacji NVRTC wyświetlany po kompilacji

---

### 4.7 Kryteria zaliczenia Fazy 4

- [ ] Użytkownik buduje prosty sygnał (np. Gaussowski) w node editorze
- [ ] Aplikacja generuje poprawny kod CUDA
- [ ] NVRTC kompiluje bez błędów
- [ ] Wynik splotu z węzłem-generatorem jest identyczny z hardcoded sygnałem z Fazy 1 (dla tych samych parametrów)
- [ ] Zmiana parametrów węzła + "Kompiluj i uruchom" → nowy wynik bez restartu
- [ ] Błąd w grafie (np. brak połączenia) → komunikat w UI, brak crash

---

## Faza 5 — Dwa sygnały, skalowalność, pełna walidacja

**Cel**: dwa niezależne grafy sygnałów, skalowalny N, pełna walidacja CPU vs GPU, końcowy UI.

---

### 5.1 Dwa niezależne grafy sygnałów

- [ ] Dwa panele node editora: "Sygnał A" i "Sygnał B" (zakładki lub obok siebie)
- [ ] Każdy graf generuje osobną funkcję `__device__`: `signalA(t)` i `signalB(t)`
- [ ] Generator kodu scala obie w jeden plik źródłowy
- [ ] Kernel splotu wywołuje `signalA(t_k)` i `signalB(t_{n-k})` dla każdego punktu

---

### 5.2 Skalowalność N

- [ ] Suwak lub pole tekstowe dla N: zakres 1024 – 65536
- [ ] Dla N > 32768: wyświetlić ostrzeżenie o czasie CPU reference
- [ ] CPU reference opcjonalnie pomijana dla dużych N (przełącznik w UI)
- [ ] Automatyczna decimacja wykresu: wyświetlać max 2048 punktów niezależnie od N

---

### 5.3 Wizualizacja (ImPlot)

- [ ] Trzy subploty (lub trzy linie na jednym wykresie):
  - [ ] Sygnał A (próbkowany)
  - [ ] Sygnał B (próbkowany)
  - [ ] Wynik splotu
- [ ] Oś X: czas `t ∈ [0, T]`
- [ ] Interaktywność ImPlot: zoom, pan
- [ ] Legenda z nazwami sygnałów

---

### 5.4 Pełna walidacja CPU vs GPU

- [ ] CPU reference: `C_cpu[n] = Σ_{k=0}^{N-1} signalA(k·dt) · signalB((n-k)·dt)`
- [ ] Wyświetlić:
  - [ ] `max|C_gpu[n] - C_cpu[n]|` — błąd absolutny
  - [ ] Tolerancja: `1e-9`
  - [ ] Status: ✅ / ❌
- [ ] Czas CPU reference wyświetlany osobno

---

### 5.5 Kryteria zaliczenia Fazy 5

- [ ] Dwa różne sygnały zbudowane w node editorze dają poprawny splot
- [ ] `max|C_gpu - C_cpu| < 1e-9` dla N = 4096
- [ ] Zmiana N nie wymaga rekompilacji kernela (N przekazywane jako parametr)
- [ ] Zmiana sygnału w node editorze → rekompilacja NVRTC → nowy wynik
- [ ] Wykres czytelny i responsywny
- [ ] Cały eksperyment działa stabilnie przez min. 5 minut (brak memory leaks, brak crashy)

---

## Uwagi przekrojowe (wszystkie fazy)

### Obsługa błędów — podział

| Błąd | Zachowanie |
|---|---|
| Brak GPU / sterownika | Komunikat w UI, brak crash, aplikacja działa bez GPU |
| Błąd CUDA (malloc, memcpy) | `errorMessage` w result, ❌ w UI |
| Błąd kompilacji NVRTC | Log błędów w UI, kernel nie zastępowany |
| Błąd GLFW / OpenGL | `stderr` + `return 1` z `main` |
| Graf niepoprawny | Komunikat w UI, przycisk "Kompiluj" nieaktywny |

### Konwencje kodowania

- Wszystkie operacje CUDA przez makro `CUDA_CHECK` — nigdy ignorowane
- `cuda_interface.h` nie zawiera żadnych `#include` z CUDA Toolkit
- Zarządzanie pamięcią GPU wyłącznie w `cuda_ops.cu` — nigdy w plikach `.cpp`
- Każda faza kończy się działającym programem przed przejściem do następnej

### Kolejność implementacji w każdej fazie

1. Infrastruktura (premake, zależności)
2. Warstwa CUDA / nowa funkcjonalność
3. Integracja z istniejącym kodem
4. UI
5. Walidacja i testy
6. Weryfikacja wszystkich kryteriów zaliczenia

---

*Plan może ewoluować — każda faza jest niezależna i weryfikowalna. Dopiero po zaliczeniu kryteriów fazy N przechodzimy do fazy N+1.*
