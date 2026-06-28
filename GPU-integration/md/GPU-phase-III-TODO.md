# Faza 3 — Asynchroniczny Wątek GPU: Instrukcja Implementacji

> **Dokument dla samodzielnej implementacji.**  
> Każda sekcja = jeden plik lub jeden krok. Nie pomijaj kolejności.  
> Kod jest kompletny i gotowy do wklejenia — miejsca wymagające dostosowania
> oznaczone są komentarzem `// TWOJE:`.

---

## 0. Co się zmienia w Fazie 3 — przegląd jednym rzutem

| Element             | Faza 2                           | Faza 3                                                       |
| ------------------- | -------------------------------- | ------------------------------------------------------------ |
| `runConvolution`    | Blokuje wątek główny (~kilka ms) | Odpala w osobnym wątku → UI nie czeka                        |
| Wynik               | Dostępny natychmiast po powrocie | Dostępny przez `std::future` — może przyjść za chwilę        |
| UI podczas obliczeń | Zawieszone (czeka)               | Responsywne, pokazuje spinner                                |
| Zlecanie zadania    | Bezpośrednie wywołanie           | `gpuWorker.submit(...)` → future                             |
| Dirty flag          | Natychmiast przelicza            | Odwiedza zlecenie jeśli wątek wolny; ignoruje jeśli zajęty   |
| Nowe zależności     | —                                | Brak — `<thread>`, `<mutex>`, `<future>` są w C++17 standard |
| Nowe pliki          | —                                | `gpu_worker.h`, `gpu_worker.cpp`                             |

Obliczenia (splot + CPU reference + walidacja), NVRTC i premake5.lua
pozostają **bez zmian**.

---

## 1. Teoria — 10 minut, nie pomijaj

### 1.1 Problem blokowania UI

W Faza 2 kliknięcie „Uruchom" lub zmiana parametru sygnału wywołuje
`runConvolution(...)` BEZPOŚREDNIO w wątku renderowania ImGui. Przez ten czas
(kilka–kilkanaście ms) okno jest „zamrożone" — nie reaguje na zdarzenia myszy.

Przy N=4096 to akceptowalne. Przy N=65536 (Faza 5) CPU reference zajmie
sekundy. UI musi pozostać responsywne.

### 1.2 `std::thread` — wątek roboczy

```cpp
// Uruchom funkcję w osobnym wątku:
std::thread myThread(funkcja, arg1, arg2);
// Wątek wykonuje funkcję współbieżnie z wątkiem głównym.

// Poczekaj na zakończenie (blokuje wątek wywołujący):
myThread.join();
```

`std::thread` nie może być kopiowany, tylko przenoszony (`std::move`).
Destruktor niezajoinowanego wątku = `std::terminate()`. Zawsze join przed
zniszczeniem.

### 1.3 `std::mutex` + `std::condition_variable` — klasyczny producent-konsument

```
WĄTEK GŁÓWNY (producent)         WĄTEK GPU (konsument)
─────────────────────────────   ─────────────────────────────
lock(mutex)                     lock(mutex)
queue.push(task)                cv.wait(lock, []{ return !queue.empty() || exit; })
unlock(mutex)                     ← obudził się, queue nie pusta
cv.notify_one()                 task = queue.front(); queue.pop()
                                unlock(mutex)
                                // przetwórz task (GPU work)
                                promise.set_value(result)
```

Kluczowe reguły:
- `cv.wait(lock, predicate)` — atomicznie zwalnia `lock` i śpi;
  budzony przez `notify`, ponownie sprawdza predicate
- Wątek konsumenta blokuje tylko na `lock` (chwilowo) i na `cv.wait`
  (śpi, nie marnuje CPU)

### 1.4 `std::promise` / `std::future` — wynik przyszłości

```cpp
// Strona producenta (wątek główny):
std::promise<int> prom;
std::future<int>  fut = prom.get_future();
// Oddaj prom do wątku roboczego (przez move — promise nie jest kopiowalny):
workerThread(std::move(prom));   // wątek roboczy wykona prom.set_value(42)

// Sprawdzenie (non-blocking, każda klatka renderowania):
if (fut.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
    int wynik = fut.get();  // po get() future staje się nieważny (valid() == false)
}
```

**Ważne:** `fut.get()` można wywołać tylko RAZ. Po wywołaniu `valid() == false`.

### 1.5 Schemat Fazy 3

```
┌─────────────────────────────────────────────────────────────────┐
│  WĄTEK GŁÓWNY (ImGui render loop, ~60 fps)                      │
│                                                                  │
│  Klatka N:  dirty && !busy → submit() → future przechowywany   │
│  Klatka N+k: future.wait_for(0ms) == ready → get() wynik       │
│              → aktualizacja wykresu                              │
└──────────────────────┬──────────────────────────────────────────┘
                       │ submit(task) — move task do queue
                       │ notify_one()
                       ▼
┌─────────────────────────────────────────────────────────────────┐
│  WĄTEK GPU (GpuWorkerThread::workerLoop, przez cały czas życia) │
│                                                                  │
│  Śpi na condition_variable (0% CPU)                             │
│  Budzi się: runConvolution(A, B, C, N, kernel, result)         │
│             CPU reference + walidacja                            │
│             promise.set_value(asyncResult)   ← wątek główny    │
│             m_busy = false                      może teraz get() │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. Nowa struktura plików po Fazie 3

```
src/
├── main.cpp             ← BEZ ZMIAN (!)
├── app.h                ← ZMODYFIKOWANY
├── app.cpp              ← ZMODYFIKOWANY
├── gui.h                ← bez zmian lub minimalne
├── gui.cpp              ← ZMODYFIKOWANY (polling + spinner)
├── cuda_interface.h     ← ZMODYFIKOWANY (nowy struct AsyncConvResult)
├── kernel_manager.h     ← bez zmian
├── kernel_manager.cpp   ← bez zmian
├── gpu_worker.h         ← NOWY
├── gpu_worker.cpp       ← NOWY
└── cuda/
    └── cuda_impl.cu     ← BEZ ZMIAN (!)
```

`premake5.lua` — BEZ ZMIAN. `gpu_worker.cpp` jest standardowym C++17, kompiluje
go MSVC tak samo jak `app.cpp`.

> **Uwaga:** `main.cpp` w Fazie 3 dostaje JEDNĄ zmianę: wywołanie
> `state.gpuWorker.shutdown()` przed cleanup (Sekcja 8).

---

## 3. Krok 1 — Modyfikacja `cuda_interface.h`

Dodaj nowy struct po `ConvolutionResult`. **Nie zmieniaj** istniejącego
`ConvolutionResult` — zachowaj wsteczną kompatybilność z Fazą 2.

```cpp
// cuda_interface.h — DODAJ na końcu pliku, przed #pragma once nie (po include)

// ---------------------------------------------------------------------------
// Wynik asynchronicznego obliczenia (Faza 3).
// Przenoszony przez std::future<AsyncConvResult>.
// Zawiera dane wyjściowe (N wartości) oraz metadane z ConvolutionResult.
// ---------------------------------------------------------------------------
struct AsyncConvResult {
    ConvolutionResult info;        // timing, maxAbsError, success — jak Faza 2
    std::vector<double> gpuOutput; // wynik splotu GPU (N wartości)
    // cpuOutput można dodać jeśli chcesz plotować referencję CPU oddzielnie
    // Na razie maxAbsError w info wystarczy do walidacji.
};
```

---

## 4. Krok 2 — Nowe pliki `src/gpu_worker.h` i `src/gpu_worker.cpp`

### 4.1 `src/gpu_worker.h` — pełny plik

```cpp
// gpu_worker.h
// Asynchroniczny wątek GPU z kolejką zadań i zwrotem wyników przez std::future.
//
// Ten plik NIE includuje nagłówków CUDA — cudaStream_t jest ukryte za void*.
// Wszystkie CUDA calls są w gpu_worker.cpp.
#pragma once
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <future>
#include <atomic>
#include <vector>
#include "cuda_interface.h"  // KernelHandle, ConvolutionResult, AsyncConvResult

// ---------------------------------------------------------------------------
// Zadanie do wykonania przez wątek GPU.
// Dane sygnałów są KOPIOWANE — bezpieczne nawet jeśli UI zmieni sygnały
// podczas obliczeń.
// GpuTask jest TYLKO PRZENASZALNY (promise nie pozwala na kopiowanie).
// ---------------------------------------------------------------------------
struct GpuTask {
    std::vector<double>          signalA;
    std::vector<double>          signalB;
    int                          N;
    KernelHandle                 kernelFunc;
    std::promise<AsyncConvResult> promise;

    // Wymagane: tylko konstruktor przenoszący (nie kopiujący)
    GpuTask() = default;
    GpuTask(GpuTask&&) = default;
    GpuTask& operator=(GpuTask&&) = default;
    GpuTask(const GpuTask&) = delete;
    GpuTask& operator=(const GpuTask&) = delete;
};

// ---------------------------------------------------------------------------
// Wątek GPU z kolejką zadań.
// Tworzony raz przy starcie aplikacji, żyje do shutdown().
//
// Użycie:
//   GpuWorkerThread worker;
//   // ... startup ...
//   std::future<AsyncConvResult> fut = worker.submit(A, B, N, kernelFunc);
//   // każda klatka:
//   if (fut.valid() && fut.wait_for(0ms) == ready) { auto r = fut.get(); }
//   // przed zamknięciem:
//   worker.shutdown();
// ---------------------------------------------------------------------------
class GpuWorkerThread {
public:
    GpuWorkerThread();
    ~GpuWorkerThread();

    // Zgłoś zadanie. Zwraca future do AsyncConvResult.
    // Kopiuje signalA i signalB — NIE przekazuj surowych wskaźników.
    // Zablokowane gdy isBusy() == true (zwraca nieprawidłowy future).
    std::future<AsyncConvResult> submit(
        const std::vector<double>& signalA,
        const std::vector<double>& signalB,
        int                        N,
        KernelHandle               kernelFunc
    );

    // Czy wątek aktualnie przetwarza zadanie?
    bool isBusy() const;

    // Zatrzymaj wątek. Blokuje do zakończenia bieżącego zadania + join.
    // MUSI być wywołane przed zniszczeniem CUDA context.
    void shutdown();

    // Czy wątek jest gotowy do przyjmowania zadań?
    bool isRunning() const;

private:
    void workerLoop();

    std::thread              m_thread;
    std::queue<GpuTask>      m_taskQueue;
    std::mutex               m_mutex;
    std::condition_variable  m_cv;
    std::atomic<bool>        m_busy         {false};
    std::atomic<bool>        m_exitRequested{false};
    std::atomic<bool>        m_initialized  {false};

    // cudaStream_t trzymamy jako void* żeby nie includować cuda.h w tym pliku.
    // Tworzony w workerLoop (w kontekście wątku roboczego).
    void* m_stream = nullptr;
};
```

### 4.2 `src/gpu_worker.cpp` — pełna implementacja

```cpp
// gpu_worker.cpp
// Kompilowany przez MSVC (cl.exe). Nie jest plikiem .cu — nie wymaga NVCC.
// Używa CUDA Runtime API przez cuda_runtime_api.h (nagłówek C, działa z MSVC).
#include "gpu_worker.h"
#include <cuda_runtime_api.h>   // cudaStream_t, cudaStreamCreate/Destroy
#include <cstdio>
#include <cmath>
#include <string>
#include <chrono>

// Deklaracja runConvolution z cuda_impl.cu (przez cuda_interface.h jest już widoczna,
// ale sygnatura musi się zgadzać z Fazą 2).
// runConvolution(A, B, C_out, N, kernelFunc, result) — jak zadeklarowane w cuda_interface.h

// ---------------------------------------------------------------------------
// Pomocnicza funkcja — referencja CPU (splot liniowy O(N²))
// Wywoływana w wątku roboczym, POZA wątkiem UI → nie blokuje UI.
// ---------------------------------------------------------------------------
static void computeCpuReference(
    const double* A,
    const double* B,
    double*       C_cpu,
    int           N)
{
    for (int n = 0; n < N; ++n) {
        double sum = 0.0;
        for (int k = 0; k < N; ++k) {
            int idx = n - k;
            if (idx >= 0 && idx < N) {
                sum += A[k] * B[idx];
            }
        }
        C_cpu[n] = sum;
    }
}

// ---------------------------------------------------------------------------
// GpuWorkerThread
// ---------------------------------------------------------------------------

GpuWorkerThread::GpuWorkerThread() {
    // Uruchom wątek roboczy. Wątek natychmiast śpi na condition_variable.
    m_thread = std::thread(&GpuWorkerThread::workerLoop, this);
}

GpuWorkerThread::~GpuWorkerThread() {
    // Jeśli nie wywołano shutdown() ręcznie, robimy to tutaj jako zabezpieczenie.
    // Preferowany sposób: ręczny shutdown() przed zniszczeniem CUDA context.
    if (m_initialized.load() && !m_exitRequested.load()) {
        shutdown();
    }
    // Wątek musi być joinable — join jeśli jest
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

// ---------------------------------------------------------------------------
std::future<AsyncConvResult> GpuWorkerThread::submit(
    const std::vector<double>& signalA,
    const std::vector<double>& signalB,
    int                        N,
    KernelHandle               kernelFunc)
{
    if (m_busy.load()) {
        // Zwróć nieprawidłowy future jeśli wątek zajęty.
        // Wywołujący sprawdza future.valid() przed użyciem.
        fprintf(stderr, "[GpuWorker] submit() odrzucone — wątek zajęty\n");
        return std::future<AsyncConvResult>{};
    }

    GpuTask task;
    task.signalA   = signalA;  // KOPIA — bezpieczna
    task.signalB   = signalB;  // KOPIA
    task.N         = N;
    task.kernelFunc = kernelFunc;

    std::future<AsyncConvResult> fut = task.promise.get_future();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_busy.store(true);
        m_taskQueue.push(std::move(task));
    }
    m_cv.notify_one();

    return fut;
}

// ---------------------------------------------------------------------------
bool GpuWorkerThread::isBusy() const {
    return m_busy.load();
}

bool GpuWorkerThread::isRunning() const {
    return m_initialized.load();
}

// ---------------------------------------------------------------------------
void GpuWorkerThread::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_exitRequested.store(true);
    }
    m_cv.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

// ---------------------------------------------------------------------------
// Główna pętla wątku roboczego.
// Wywołana raz przy starcie w konstruktorze — żyje do shutdown().
// ---------------------------------------------------------------------------
void GpuWorkerThread::workerLoop() {
    // ================================================================
    // Inicjalizacja zasobów CUDA dla tego wątku
    // ================================================================
    // cudaStream_t pozwala na asynchroniczne operacje CUDA niezależne od
    // innych strumieni. Tworzymy go tutaj (w wątku GPU), a nie w main.
    cudaStream_t stream = nullptr;
    cudaError_t streamErr = cudaStreamCreate(&stream);
    if (streamErr != cudaSuccess) {
        fprintf(stderr, "[GpuWorker] cudaStreamCreate failed: %s\n",
                cudaGetErrorString(streamErr));
        // Kontynuuj z nullptr — runConvolution użyje domyślnego strumienia
    } else {
        m_stream = reinterpret_cast<void*>(stream);
    }

    m_initialized.store(true);

    // ================================================================
    // Pętla główna: czekaj na zadanie, wykonaj, poinformuj przez promise
    // ================================================================
    while (true) {
        GpuTask task;

        // --- Czekaj na zadanie lub sygnał wyjścia ---
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            // wait() atomicznie: zwalnia lock + śpi; budzi się gdy notify
            // warunek: jest zadanie LUB zażądano wyjścia
            m_cv.wait(lock, [this] {
                return !m_taskQueue.empty() || m_exitRequested.load();
            });

            // Jeśli sygnał wyjścia i kolejka pusta → zakończ
            if (m_exitRequested.load() && m_taskQueue.empty()) {
                break;
            }

            // Pobierz zadanie z kolejki (move — przenosimy promise)
            task = std::move(m_taskQueue.front());
            m_taskQueue.pop();
            // lock zwolniony przez unique_lock po wyjściu z bloku {}
        }

        // --- Wykonaj zadanie (GPU + CPU reference) ---
        AsyncConvResult asyncResult;
        asyncResult.gpuOutput.resize(task.N, 0.0);

        // KROK 1: GPU splot
        runConvolution(
            task.signalA.data(),
            task.signalB.data(),
            asyncResult.gpuOutput.data(),  // wynik GPU tutaj
            task.N,
            task.kernelFunc,
            asyncResult.info               // timing + maxAbsError
        );

        // KROK 2: CPU reference + walidacja (jeśli GPU się powiodło)
        // Uwaga: jeśli runConvolution już liczy CPU reference wewnętrznie
        // i ustawia asyncResult.info.maxAbsError, pomiń ten blok.
        // Sprawdź swoją implementację cuda_impl.cu — jeśli maxAbsError
        // jest już ustawione, ten blok jest zbędny.
        //
        // Jeśli CPU reference jest POZA runConvolution (np. w app.cpp w Fazie 1/2),
        // przenieś ją tutaj:
        if (asyncResult.info.success) {
            std::vector<double> cpuOutput(task.N, 0.0);

            auto t0 = std::chrono::high_resolution_clock::now();
            computeCpuReference(
                task.signalA.data(),
                task.signalB.data(),
                cpuOutput.data(),
                task.N
            );
            auto t1 = std::chrono::high_resolution_clock::now();
            float cpuMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
            fprintf(stdout, "[GpuWorker] CPU reference: %.1f ms\n", cpuMs);

            // Walidacja — oblicz maxAbsError jeśli nie robi tego runConvolution
            // (jeśli runConvolution już ustawia maxAbsError, odkomentuj tylko
            // poniższy print i pomiń obliczanie):
            double maxErr = 0.0;
            for (int i = 0; i < task.N; ++i) {
                double err = std::fabs(asyncResult.gpuOutput[i] - cpuOutput[i]);
                if (err > maxErr) maxErr = err;
            }
            asyncResult.info.maxAbsError = static_cast<float>(maxErr);
            // (cpuOutput nie jest przechowywany w asyncResult — tylko maxAbsError)
        }

        // KROK 3: Poinformuj wątek główny przez promise
        // Uwaga: po set_value(), future w wątku głównym staje się "ready".
        // m_busy musi być false PRZED set_value — inaczej wątek główny
        // może próbować submit() przed tym jak go ustawimy.
        m_busy.store(false);
        task.promise.set_value(std::move(asyncResult));
    }

    // ================================================================
    // Sprzątanie zasobów CUDA wątku
    // ================================================================
    if (stream != nullptr) {
        cudaStreamDestroy(stream);
        m_stream = nullptr;
    }

    m_initialized.store(false);
    fprintf(stdout, "[GpuWorker] Wątek zakończony.\n");
}
```

> **WAŻNA UWAGA dotycząca CPU reference:**  
> Sprawdź swoją implementację `cuda_impl.cu` z Fazy 2. Jeśli `runConvolution`
> już oblicza CPU reference WEWNĘTRZNIE i ustawia `result.maxAbsError`, usuń
> blok „KROK 2" w `workerLoop`. Duplikowanie obliczeń byłoby stratą czasu.  
> Jeśli CPU reference było w `app.cpp` (wywołane PO `runConvolution`), przenieś
> je do `workerLoop` jak pokazano powyżej.

---

## 5. Krok 3 — Modyfikacja `src/app.h`

Dodaj pola do `AppState`:

```cpp
// app.h — dodaj #include i pola do AppState

#include "gpu_worker.h"  // NOWE
#include <future>        // std::future (może być już w gpu_worker.h)

struct AppState {
    // --- istniejące pola z Fazy 2 (zachowaj wszystkie) ---
    bool               cudaAvail   = false;
    CudaDeviceInfo     deviceInfo  = {};
    KernelManager      kernelMgr;
    std::string        kernelSource;
    std::string        kernelFilePath;
    NvrtcCompileResult lastCompile;
    ConvolutionResult  convResult  = {};
    std::vector<double> convOutput;   // dla ImPlot — GPU result
    bool               dirty       = false;
    // ... sygnały, N, itd. ...

    // --- NOWE pola Fazy 3 ---
    GpuWorkerThread                gpuWorker;
    std::future<AsyncConvResult>   gpuFuture;
    bool                           computing = false;
};
```

> **Kolejność pól ma znaczenie:** `gpuWorker` musi być PRZED `gpuFuture`
> (inicjalizacja w kolejności deklaracji). `KernelManager` i `GpuWorkerThread`
> mają konstruktory — zostaną zbudowane automatycznie.

---

## 6. Krok 4 — Modyfikacja `src/app.cpp`

### 6.1 Brak zmian w `appInit` (!)

`GpuWorkerThread` uruchamia swój wątek w konstruktorze — automatycznie
gdy `AppState` jest budowane. **Nie dodajesz nic do `appInit`** — wątek
już działa.

Sprawdź jednak: `appInit` MUSI być wywołane po zainicjowaniu CUDA (jak w Fazie 2).
Kolejność jest bez zmian: `queryCudaDevice` → `initCudaDriver` → `compile`.

### 6.2 Usuń bezpośrednie wywołanie `runConvolution` z `app.cpp`

Jeśli miałeś w `app.cpp` coś takiego (z Fazy 2):

```cpp
// FAZA 2 — to usuń lub zamień:
if (state.dirty && state.cudaAvail && state.kernelMgr.isReady()) {
    runConvolution(
        state.signalA.data(),
        state.signalB.data(),
        state.convOutput.data(),
        state.N,
        state.kernelMgr.getFunction(),
        state.convResult
    );
    state.dirty = false;
}
```

Zamień na wersję asynchroniczną (patrz Sekcja 7).

### 6.3 Dodaj funkcję `pollAndSubmit` (opcjonalnie w `app.cpp`)

Możesz wyodrębnić logikę pollingu do osobnej funkcji wywoływanej z `gui.cpp`
(lub zostawić całość w `gui.cpp` — wybór stylu):

```cpp
// app.cpp — opcjonalna funkcja pomocnicza

void appPollAndSubmit(AppState& state) {
    // --- KROK A: Sprawdź czy future jest gotowy ---
    if (state.computing && state.gpuFuture.valid()) {
        using namespace std::chrono;
        if (state.gpuFuture.wait_for(milliseconds(0)) == std::future_status::ready) {
            // Pobierz wynik (blokuje tylko jeśli wątek jeszcze nie skończył —
            // skoro wait_for zwróciło ready, get() jest natychmiastowe)
            AsyncConvResult asyncResult = state.gpuFuture.get();

            // Przenieś dane do stanu aplikacji
            state.convOutput = std::move(asyncResult.gpuOutput);
            state.convResult = asyncResult.info;

            state.computing = false;

            // Jeśli parametry zmieniły się PODCZAS obliczeń, dirty jest ustawione
            // → następne wywołanie pollAndSubmit zleci nowe zadanie
        }
    }

    // --- KROK B: Zlec zadanie jeśli są powody ---
    bool shouldSubmit =
        state.dirty           &&    // parametry zmieniły się
        !state.computing      &&    // wątek wolny
        state.cudaAvail       &&    // GPU dostępne
        state.kernelMgr.isReady();  // kernel skompilowany

    if (shouldSubmit) {
        // submit() kopiuje sygnały wewnętrznie — bezpieczne
        state.gpuFuture = state.gpuWorker.submit(
            state.signalA,
            state.signalB,
            state.N,
            state.kernelMgr.getFunction()
        );

        if (state.gpuFuture.valid()) {
            state.computing = true;
            state.dirty     = false;
        } else {
            // submit() zwróciło nieprawidłowy future (wątek zajęty — nie powinno się zdarzyć)
            fprintf(stderr, "[App] submit() failed — future invalid\n");
        }
    }
}
```

---

## 7. Krok 5 — Modyfikacja `src/gui.cpp`

### 7.1 Wywołaj `appPollAndSubmit` na początku każdej klatki

Dodaj na POCZĄTKU funkcji `guiRender` (lub `renderGui`) — PRZED renderowaniem
jakichkolwiek okien ImGui:

```cpp
// gui.cpp — na początku guiRender:

void guiRender(AppState& state) {
    // NOWE Faza 3: sprawdź wyniki + zlec zadanie jeśli potrzeba
    appPollAndSubmit(state);  // lub wstaw kod bezpośrednio tutaj

    // ... reszta renderowania ImGui bez zmian ...
}
```

Jeśli nie wyodrębniałeś `appPollAndSubmit`, wstaw kod bezpośrednio.

### 7.2 Zmodyfikuj sekcję obliczeń w UI

Znajdź panel z przyciskiem uruchamiającym obliczenia i zmodyfikuj:

```cpp
// STARA Faza 2 (zamień):
// if (ImGui::Button("Uruchom splot")) {
//     runConvolution(...);
// }

// NOWA Faza 3:

// Wskaźnik stanu — "Obliczanie..." z animowanym spinnerem
if (state.computing) {
    // Prosty animowany tekst korzystający z czasu ImGui
    float t = (float)ImGui::GetTime();
    const char* spinner[] = { "|", "/", "─", "\\" };
    int idx = (int)(t * 6.0f) % 4;
    ImGui::TextColored({1.0f, 0.8f, 0.0f, 1.0f},
        "%s Obliczanie...", spinner[idx]);
} else {
    // Przycisk aktywny tylko gdy możemy zlecić zadanie
    bool canSubmit = state.cudaAvail && state.kernelMgr.isReady() && !state.computing;
    if (!canSubmit) ImGui::BeginDisabled();

    if (ImGui::Button("Uruchom splot")) {
        // Ręczne wymuszenie — ustaw dirty i submit nastąpi w appPollAndSubmit
        state.dirty = true;
    }

    if (!canSubmit) ImGui::EndDisabled();

    if (!canSubmit && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (!state.kernelMgr.isReady())
            ImGui::SetTooltip("Najpierw skompiluj kernel (Przeładuj kernel)");
        else if (!state.cudaAvail)
            ImGui::SetTooltip("Brak GPU");
    }
}

// Wyniki — wyświetlaj przez cały czas (nie znikają gdy computing)
if (state.convResult.success) {
    ImGui::Text("GPU:  %.3f ms kernel | %.3f ms H2D | %.3f ms D2H",
        state.convResult.kernelMs,
        state.convResult.transferToGpuMs,
        state.convResult.transferFromGpuMs);
    ImGui::Text("maxAbsError: %.2e %s",
        state.convResult.maxAbsError,
        state.convResult.maxAbsError < 1e-9 ? "✓ OK" : "✗ FAIL");
} else if (!state.convResult.errorMessage.empty()) {
    ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f},
        "Błąd: %s", state.convResult.errorMessage.c_str());
}
```

### 7.3 Dirty flag z parametrami sygnałów — bez zmian

Jeśli zmiana parametru sygnału ustawia `state.dirty = true`, zachowaj to.
`appPollAndSubmit` automatycznie zleci nowe zadanie w następnej klatce:

```cpp
// Przykład — w panelu parametrów sygnałów (jak w Fazie 1/2):
if (ImGui::SliderDouble("mu", &state.sigMu, 0.1, 0.9)) {
    // regeneruj sygnał A...
    state.dirty = true;  // bez zmian
}
```

### 7.4 Przycisk „Przeładuj kernel" — minimalna modyfikacja

Po przeładowaniu kernela (sukces NVRTC), ustaw dirty aby wynik był przeliczony
z nowym kernelem:

```cpp
// W sekcji NVRTC (jak w Fazie 2), po udanej kompilacji:
if (state.lastCompile.success) {
    state.dirty = true;  // już było? — bez zmian, działa tak samo
}
```

---

## 8. Shutdown — KRYTYCZNE — `main.cpp`

To jest JEDYNA zmiana w `main.cpp`. Dodaj wywołanie shutdown **przed** cleanup CUDA i ImGui:

```cpp
// main.cpp — w sekcji sprzątania, PRZED dotychczasowym cleanup:

// -----------------------------------------------------------------------
// 7. Sprzątanie — NOWA KOLEJNOŚĆ w Fazie 3
// -----------------------------------------------------------------------

// NOWE: poczekaj na zakończenie wątku GPU PRZED czymkolwiek innym.
// Wątek może właśnie pisać do pamięci GPU — nie wolno nic niszczyć wcześniej.
state.gpuWorker.shutdown();

// Reszta bez zmian (kolejność jak w Fazie 2):
ImPlot::DestroyContext();
ImGui_ImplOpenGL3_Shutdown();
ImGui_ImplGlfw_Shutdown();
ImGui::DestroyContext();
glfwDestroyWindow(window);
glfwTerminate();
return 0;
```

**Dlaczego ta kolejność jest obowiązkowa:**

`shutdown()` wywołuje `thread.join()` — blokuje do zakończenia wątku roboczego.
Wątek roboczy może właśnie wykonywać `runConvolution` (cudaMemcpy, cuLaunchKernel).
Gdyby CUDA context został zniszczony wcześniej, te operacje crashowałyby.

---

## 9. Kolejność inicjalizacji i shutdown — pełny obraz

```
STARTUP (kolejność obowiązkowa):
  1. glfwInit() + glfwCreateWindow()
  2. gladLoadGLLoader()
  3. ImGui::CreateContext() + ImPlot::CreateContext()
  4. AppState state;              ← konstruktor buduje GpuWorkerThread (wątek startuje)
  5. appInit(state)               ← queryCudaDevice → initCudaDriver → compile NVRTC
     state.dirty = true;          ← pierwsze obliczenie będzie zlecone w klatce 1

RENDER LOOP (każda klatka):
  6. glfwPollEvents()
  7. ImGui::NewFrame()
  8. guiRender(state)             ← wewnątrz: appPollAndSubmit() sprawdza future
  9. ImGui::Render() + swap

SHUTDOWN (kolejność obowiązkowa):
  10. state.gpuWorker.shutdown()  ← join wątku GPU (PIERWSZY!)
  11. ImPlot::DestroyContext()
  12. ImGui::DestroyContext()
  13. glfwTerminate()
```

---

## 10. Budowanie projektu

`premake5.lua` nie wymaga zmian. `gpu_worker.cpp` jest automatycznie
dodany do projektu przez `"src/**.cpp"` w bloku `files`.

```bat
:: Tylko jeśli dodałeś nowe pliki (co zrobiłeś):
tools\premake5.exe vs2022
```

Otwórz `.sln`. Kliknij Build → Build Solution.

**Oczekiwane w Output:**
```
1> Compiling...
1> gpu_worker.cpp      ← MSVC, nie NVCC
1> app.cpp
1> gui.cpp
1> NVCC: src/cuda/cuda_impl.cu
1> GpuExperiment.vcxproj → build\bin\Debug\GpuExperiment.exe
```

---

## 11. Pułapki i rozwiązania

### P1 — `future.get()` wywołane dwa razy → crash lub exception

**Objaw**: `std::future_error: No associated state` lub crash.

**Przyczyna**: `get()` może być wywołane tylko RAZ. Po `get()` future jest
nieważny (`valid() == false`).

**Rozwiązanie**: Zawsze sprawdź `valid()` przed operacją:
```cpp
if (state.gpuFuture.valid() &&
    state.gpuFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
    auto result = state.gpuFuture.get();  // po tym: valid() == false
    // ...
}
```

---

### P2 — `thread.join()` nigdy nie wraca — aplikacja wisi przy zamknięciu

**Objaw**: Okno znika, ale proces trwa wiecznie.

**Przyczyna**: Wątek roboczy utknął w `runConvolution` lub `cv.wait()` i nie
widzi `m_exitRequested = true`.

**Przyczyna A** (cv.wait): `shutdown()` musi wywołać `notify_all()` PO
ustawieniu `m_exitRequested = true`. Sprawdź kolejność w `shutdown()`:
```cpp
void GpuWorkerThread::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_exitRequested.store(true);  // PIERWSZE
    }
    m_cv.notify_all();               // DRUGIE (po unlock)
    if (m_thread.joinable()) {
        m_thread.join();             // czekaj
    }
}
```

**Przyczyna B** (runConvolution deadlock): `cudaDeviceSynchronize()` nigdy
nie wraca — kernel zawiesił się (infinite loop, out-of-bounds access). Sprawdź
kernel `kernels/convolution.cu` pod kątem warunków granicznych.

---

### P3 — Wynik jest „stary" — po zmianie parametrów wykres nie aktualizuje się

**Objaw**: Zmieniam parametry sygnału, ale wykres pokazuje poprzedni wynik.

**Przyczyna**: `dirty` jest czyszczone podczas `submit()`, ale `computing = true`
blokuje nowe zgłoszenie. Jeśli parametry zmieniły się PODCZAS obliczeń, dirty
jest ustawione → po zakończeniu kolejna klatka POWINNA zlecić nowe.

**Rozwiązanie**: Sprawdź czy `dirty = true` jest ustawiane przy każdej zmianie
parametru sygnału. I sprawdź czy `appPollAndSubmit` jest wywołane KAŻDĄ klatkę
(nie tylko gdy jest jakiś event).

---

### P4 — `submit()` zwraca nieprawidłowy future (choć wątek był wolny)

**Objaw**: `state.gpuFuture.valid() == false` mimo że `!state.computing`.

**Przyczyna**: Wyścig między sprawdzeniem `isBusy()` w `submit()` a
ustawieniem `m_busy = false` w wątku roboczym.

**Wyjaśnienie**: `m_busy.store(false)` jest ustawiane w wątku roboczym PRZED
`set_value()`. Ale między `m_busy.store(false)` a fizycznym zwolnieniem przyszłego
wyniku jest okno. Jeśli wątek główny wywołuje `submit()` w tym oknie, `isBusy()`
zwraca false i submit się uda — bo queue jest pusta i `m_busy = true` jest znowu
ustawione.

W praktyce to nie jest problem — główna pętla wywołuje `appPollAndSubmit` raz
na klatkę, a okno czasu jest rzędu nanosekund. Jeśli jednak widzisz ten błąd,
dodaj sprawdzenie `gpuFuture.valid()` przed `submit()`:
```cpp
// W appPollAndSubmit:
bool shouldSubmit = state.dirty && !state.computing && ...;
// Dodaj: && !state.gpuFuture.valid()  jeśli widzisz podwójne submity
```

---

### P5 — Wątek GPU wykonuje stary kernel po przeładowaniu NVRTC

**Objaw**: Kliknięcie „Przeładuj kernel" + zmiana sygnału → wynik taki sam jak stary.

**Przyczyna**: `kernelFunc` w GpuTask jest kopiowany w momencie `submit()`.
Jeśli przeładowałeś kernel PODCZAS obliczeń (stary `CUfunction` jest już w task),
ten task wykona stary kernel.

**Rozwiązanie**: Zablokuj przeładowanie kernela gdy `computing == true`:
```cpp
// W sekcji UI "Przeładuj kernel":
bool canReload = !state.computing;
if (!canReload) ImGui::BeginDisabled();
if (ImGui::Button("Przeładuj kernel") && canReload) {
    // ... jak w Fazie 2
}
if (!canReload) ImGui::EndDisabled();
```

---

### P6 — `GpuTask` nie kompiluje się: „deleted copy constructor"

**Objaw**: Błąd MSVC: `C2280: attempting to reference a deleted function`.

**Przyczyna**: `std::promise` jest non-copyable, więc `GpuTask` jest non-copyable.
`std::queue::push(task)` próbuje kopiować.

**Rozwiązanie**: Użyj `push(std::move(task))`:
```cpp
m_taskQueue.push(std::move(task));  // NIE: m_taskQueue.push(task)
```

Sprawdź też że w `workerLoop`:
```cpp
task = std::move(m_taskQueue.front());  // NIE: task = m_taskQueue.front()
m_taskQueue.pop();
```

---

### P7 — Program crasha przy zamknięciu: `abort() / terminate()`

**Objaw**: Crash przy zamknięciu okna, message: `std::terminate`.

**Przyczyna**: `std::thread` jest niszczone bez `join()`. Destruktor `std::thread`
wywołuje `std::terminate()` jeśli wątek jest joinable i nie był join'd.

**Rozwiązanie**: Upewnij się że `state.gpuWorker.shutdown()` jest wywołane
przed zniszczeniem `state` (czyli przed `return 0` w `main()`). Sprawdź Sekcję 8.

---

### P8 — Kompilacja: `gpu_worker.cpp` nie może znaleźć `cuda_runtime_api.h`

**Objaw**: MSVC nie może znaleźć `cuda_runtime_api.h`.

**Przyczyna**: CUDA include path nie jest w `includedirs` dla `.cpp` — tylko
dla `.cu`.

**Rozwiązanie**: Sprawdź `premake5.lua` — blok `includedirs` musi zawierać:
```lua
cudaPath .. "/include",
```
To powinno być już z Fazy 1. Jeśli problem nadal istnieje, zregeneruj projekt.

---

### P9 — Race condition: wykres aktualizuje się w połowie obliczeń

**Objaw**: Na chwilę wykres pokazuje „stare + nowe" punkty.

**Wyjaśnienie**: To nie jest race condition — `state.convOutput` jest aktualizowany
atomowo w głównym wątku przez `std::move`. ImPlot widzi albo stary, albo nowy
wektor — nigdy mieszankę.

`state.convOutput = std::move(asyncResult.gpuOutput)` jest operacją na wektorze
w wątku głównym, a wątek GPU do tego momentu już zakończył pisanie (skoro `future`
jest ready). Brak data race.

---

## 12. Checklista zaliczenia Fazy 3

```
Budowanie:
[ ] premake5 vs2022 — brak błędów Lua
[ ] Build Debug bez błędów
[ ] gpu_worker.cpp widoczny jako kompilowany przez cl.exe (nie nvcc)

Startup:
[ ] Aplikacja startuje bez crash
[ ] "[GpuWorker]" komunikaty są widoczne w konsoli
[ ] Wynik splotu pojawia się automatycznie po starcie (dirty flag + submit)

Responsywność UI:
[ ] Kliknij "Uruchom splot" (lub zmień parametr sygnału)
[ ] Okno aplikacji NADAL reaguje na mysz podczas obliczeń
[ ] Spinner "Obliczanie..." jest widoczny
[ ] Przycisk "Uruchom splot" jest nieaktywny (disabled) podczas obliczeń

Poprawność wyników:
[ ] maxAbsError < 1e-9 (ta sama tolerancja co Faza 1/2)
[ ] Wynik wizualnie identyczny jak w Fazie 2
[ ] Czasy GPU (kernel, transfer) wyświetlane poprawnie po zakończeniu

Dirty flag:
[ ] Zmiana parametru sygnału (slider) → nowe obliczenie startuje automatycznie
[ ] Zmiana parametrów wielokrotnie podczas obliczeń → po zakończeniu jedne
    automatycznie startuje nowe (z ostatnimi parametrami)

NVRTC (zachowanie z Fazy 2):
[ ] "Przeładuj kernel" nadal działa
[ ] Log NVRTC nadal widoczny
[ ] Błąd składni w kernelu → komunikat, brak crash

Shutdown:
[ ] Zamknięcie okna → aplikacja kończy się bez hang
[ ] "[GpuWorker] Wątek zakończony." widoczny w konsoli przed zamknięciem
[ ] Brak std::terminate lub abort przy zamknięciu

Fazy 1/2 nadal spełnione:
[ ] Parametry sygnałów edytowalne
[ ] Panel CUDA Device wyświetla dane GPU
[ ] Hot-reload kernela działa
```

---

## Appendix A — Jak weryfikować responsywność UI

Symuluj wolne obliczenia wstawiając `std::this_thread::sleep_for(2s)` na
początku `workerLoop` (po lock, przed przetwarzaniem):

```cpp
// W workerLoop, po pobraniu task z kolejki — TYLKO DO TESTU:
#include <thread>
std::this_thread::sleep_for(std::chrono::seconds(2));
// Usuń po weryfikacji!
```

Po kliknięciu "Uruchom": spinner powinien się kręcić przez 2 sekundy, okno
powinno być responsywne, po 2 sekundach wynik się pojawi.

---

## Appendix B — Uproszczony diagram przepływu danych

```
appInit():
  AppState konstruktor → GpuWorkerThread() → wątek śpi na cv.wait()
  appInit() → queryCudaDevice, compile NVRTC
  dirty = true

Klatka 1:
  appPollAndSubmit():
    dirty && !computing → submit(A, B, N, kernel)
      → kopiuje A, B do GpuTask
      → queue.push(task)
      → notify_one()
    computing = true, dirty = false
  → wynik: gpuFuture (valid, not ready)

Wątek GPU:
  cv.wait() obudzony
  queue.pop() → task
  runConvolution(A, B, output, N, kernel, info)
  computeCpuRef() + walidacja
  m_busy = false
  promise.set_value(asyncResult)

Klatka N (N≥2):
  appPollAndSubmit():
    gpuFuture.valid() && wait_for(0ms) == ready
    → get() → asyncResult
    → convOutput = move(gpuOutput)
    → convResult = info
    computing = false
  ImPlot wyświetla convOutput ← zaktualizowany
```

---

*Gotowe. Powodzenia z Fazą 3.*