# SA-Enhanced Nelder–Mead — Eksperyment Optymalizacyjny
## Pełny opis projektu, wymagania i API

> Ten dokument jest self-contained — zawiera wszystko, czego potrzeba aby
> zrozumieć projekt, zaimplementować jego część lub kontynuować go w nowym kontekście.
> Sekcja **API** jest rozbudowywana w miarę stabilizacji kolejnych etapów.

---

## 1. Kontekst — istniejące repozytorium

Projekt `gpu-experiment` to interaktywna aplikacja C++ z node editorem
do modelowania i dopasowywania krzywych IV półprzewodników.
Aktualnie zawiera eksperyment ze splotem sygnałów na GPU.

```
gpu-experiment/
├── kernels/convolution.cu
├── src/
│   ├── app.h / app.cpp           ← AppState, dirty flag, pollAndSubmit
│   ├── gui.h / gui.cpp           ← ImGui / ImPlot / imnodes
│   ├── cuda_interface.h          ← wspólne typy (KernelHandle, GpuTask, …)
│   ├── kernel_manager.h/.cpp     ← NVRTC + Driver API: kompilacja, CUmodule
│   ├── gpu_worker.h/.cpp         ← asynchroniczny wątek GPU, kolejka, future
│   ├── signal_graph.h/.cpp       ← NodeGraph, SignalNode, CPU ewaluator
│   ├── code_gen.h/.cpp           ← generator kodu CUDA z grafu
│   └── cuda/cuda_impl.cu
└── vendor/ imgui / implot / imnodes
```

### Komponenty do ponownego użycia w solverze

| Komponent                      | Rola w solverze                                                        |
| ------------------------------ | ---------------------------------------------------------------------- |
| `KernelManager`                | kompilacja kernela modelu IV — raz per sesja, nie podczas iteracji     |
| `GpuWorkerThread`              | asynchroniczny batch dopasowań                                         |
| `NodeGraph` + `evaluateSignal` | CPU-side ewaluacja modelu (debug, referencja)                          |
| `code_gen`                     | rozszerzenie o parametry runtime (`double* params`) zamiast literałów  |
| ImPlot                         | wykresy χ², zbieżności, temperatury SA, per-parametr scatter simpleksu |

---

## 2. Cel eksperymentu

Zaimplementowanie algorytmu **SA-Enhanced Nelder–Mead** do dopasowywania
modeli IV generowanych dynamicznie przez node editor. Solver minimalizuje χ²
między krzywą modelu a danymi pomiarowymi, operując na parametrach wolnych (free).

**Faza I (ten dokument):** Pełna implementacja na CPU — wszystkie funkcje,
stabilne API, pełna obserwowalność. CPU jest referencją i środowiskiem debug.

**Faza II (osobny chat):** Implementacja na GPU z użyciem API ustalonego w Fazie I.
GPU = produkcja i batch. CPU pozostaje jako ground truth do walidacji.

---

## 3. Model IV — 4-parametrowa dioda

Eksperyment operuje na modelu **ciemnej charakterystyki IV** (dark IV)
z czterema wolnymi parametrami:

| Symbol | Nazwa                   | Typowy zakres fizyczny |
| ------ | ----------------------- | ---------------------- |
| I₀     | prąd nasycenia          | 1e-15 … 1e-6 A         |
| A      | współczynnik idealności | 0.5 … 3.0              |
| Rₛ     | rezystancja szeregowa   | 0 … 10 Ω               |
| Rₛₕ    | rezystancja bocznikowa  | 10 … 1e6 Ω             |

Równanie niejawne:
```
I = -I₀ · (exp((V + I·Rₛ) / (A·Vt)) - 1) - (V + I·Rₛ) / Rₛₕ
```
gdzie `Vt = kT/q ≈ 0.02585 V` przy T = 300 K (hardkodowane lub przekazywane).

Równanie niejawne rozwiązywane jest jawnie przez funkcję **LambertW**,
co eliminuje wewnętrzną iterację Newtona i daje deterministyczny I(V, params).

### LambertW — implementacje

**CPU (Faza I):** iteracja Halleya. 3–4 kroki od dobrego punktu startowego
dają dokładność maszynową. Wyniki weryfikowane przez `scipy.special.lambertw`.

**GPU (Faza II):** osobny chat implementuje kernel CUDA. CPU i GPU muszą dawać
zgodne wyniki w granicy tolerancji numerycznej (brak bitowej zgodności jest oczekiwany).

### Skala parametrów

Solver operuje wyłącznie na wartościach **liniowych**. Jeśli parametr
fizycznie obejmuje wiele rzędów (np. I₀), transformacja log/exp jest
**wewnątrz funkcji modelu**, nie w solverze. Granice `[min, max]`
podawane w skali liniowej. Perturbacje SA i operacje simpleksu są jednorodne.

---

## 4. Algorytm: SA-Enhanced Nelder–Mead

SA nie jest osobnym solverem — to modyfikacja kryterium akceptacji
wewnątrz każdego kroku Nelder–Mead. Jeden algorytm, nie dwa.

```
Standardowy NM:
  f(reflected) < f(worst) → akceptuj zawsze
  f(reflected) ≥ f(worst) → odrzuć, rób contraction/shrink

SA-Enhanced NM:
  f(reflected) < f(best)  → ekspansja (jak NM)
  f(reflected) < f(worst) → akceptuj zawsze (jak NM)
  f(reflected) ≥ f(worst) → akceptuj z P = exp(-Δf / T)      ← SA
                             gdzie Δf = f(reflected) - f(worst)
                             jeśli odrzucono → contraction/shrink (jak NM)
```

Gdy T → 0: degeneracja do standardowego NM (deterministyczny).
Gdy T duże: szeroka eksploracja przestrzeni parametrów.

### Operacje simpleksu

| Operacja        | Formuła                                      | Kiedy                         |
| --------------- | -------------------------------------------- | ----------------------------- |
| **Reflection**  | `x_r = centroid + α·(centroid - worst)`      | zawsze próbowana pierwsza     |
| **Expansion**   | `x_e = centroid + γ·(x_r - centroid)`        | gdy reflection lepsza od best |
| **Contraction** | `x_c = centroid + ρ·(worst - centroid)`      | gdy reflection odrzucona      |
| **Shrink**      | `xᵢ = best + σ·(xᵢ - best)` dla wszystkich i | gdy contraction nie pomaga    |

Domyślne współczynniki NM: α=1.0, γ=2.0, ρ=0.5, σ=0.5.

### Harmonogram chłodzenia

```cpp
enum class CoolingSchedule { Boltzmann, Geometric, Adaptive };
```

| Harmonogram             | Formuła                | Uwagi                                   |
| ----------------------- | ---------------------- | --------------------------------------- |
| **Boltzmann** (default) | `T_k = T₀ / ln(1 + k)` | powolne schładzanie, bezpieczny default |
| **Geometric**           | `T_k = T₀ · α^k`       | szybsze, α ∈ (0, 1) konfigurowalny      |
| **Adaptive**            | heurystyczna           | do doprecyzowania w testach             |

Temperatura `T_current` jest **runtime parameter** — modyfikowalny przez
użytkownika w trakcie działania algorytmu bez restartu.

Operacje dostępne z UI:
- **obniżyć T** → wymusić zbieżność lokalną (tryb NM)
- **podnieść T / reheat** → uciec z lokalnego minimum
- **zerować T** → natychmiastowe przejście w tryb czystego NM

---

## 5. Funkcja celu

### χ² (chi-kwadrat)

```
χ²(p) = Σᵢ [(I_meas(Vᵢ) - I_model(Vᵢ, p))² / σᵢ²]
```

Minimalizacja χ² = dopasowanie krzywej modelu do danych pomiarowych.

### Δχ² (delta chi-kwadrat)

```
Δχ²(p) = χ²(p) - χ²_min
```

Zastosowania:
- **Przedziały ufności:** profil Δχ²(pₖ) daje 1σ przy Δχ²=1, 2σ przy Δχ²=4
- **Kryterium zatrzymania:** solver zatrzymuje się gdy Δχ² < ε
- **Mapa akceptowalnych dopasowań:** regiony Δχ² < progu

---

## 6. Kluczowe decyzje projektowe

| Kwestia                  | Decyzja                                                            |
| ------------------------ | ------------------------------------------------------------------ |
| Relacja NM i SA          | SA wewnątrz każdego kroku NM — jeden algorytm                      |
| Skala parametrów solvera | Zawsze liniowa; exp() wewnątrz modelu, nie w solverze              |
| Temperatura T            | Runtime, edytowalna przez UI w trakcie działania                   |
| Harmonogram chłodzenia   | Boltzmann default; CoolingSchedule enum (wymienialny)              |
| Kompilacja NVRTC (GPU)   | Raz per struktura grafu; zero razy podczas iteracji solvera        |
| Parametry do GPU         | `double* params` + ParameterMap generowana przy kompilacji         |
| Nazwy parametrów         | `std::string` na CPU (czytelność, trace, JSON); `int` index na GPU |
| Format prefit            | JSON z `model_id` (fingerprint topologii grafu)                    |
| NaN w funkcji celu       | Traktowane jako +∞ — odrzucenie kandydata, nie crash               |
| Granice [min, max]       | Clipping + penalizacja (+∞) za wyjście poza zakres                 |
| Degeneracja simpleksu    | Detekcja (diameter < próg) + automatyczny restart                  |
| Podział CPU/GPU          | GPU = produkcja i batch; CPU = debug i referencja                  |
| Wizualizacja simpleksu   | Per-parametr scatter: N+1 kolorowych punktów per wykres            |
| Struktura grafu          | Stała podczas sesji; zmiana = nowa sesja (nowa kompilacja NVRTC)   |
| Pamięć                   | Nie jest ograniczeniem (128 GB RAM, 48 GB+ VRAM docelowo)          |

---

## 7. Podział CPU / GPU — role

### CPU = debug i referencja

```
CPU:  [NM/SA krok po kroku] → [evaluateDiodeIV (LambertW)] → [χ²]
           ↓
      TraceStep zapisany, stan inspekcjonowalny
```

- Pełny trace iteracji — każdy krok, każda operacja
- Inspekcja dowolnego wierzchołka simpleksu
- Weryfikacja poprawności implementacji GPU
- Nie używany do batch (zbyt duże dane trace)

### GPU = produkcja

```
GPU:  [NM/SA: generuj kandydatów] → [evaluateIV kernel] → [reduce χ²]
           ↑______________[ zaktualizuj stan solvera ]______________↑
```

- Cały pipeline iteracji na GPU
- Batch: tysiące niezależnych dopasowań równolegle
- Brak trace; tylko wyniki końcowe

### Walidacja CPU ↔ GPU

Empiryczna, nie z góry zdefiniowanymi tolerancjami:
1. Te same parametry → χ² CPU vs χ² GPU w sensownej tolerancji
2. Kilka pierwszych kroków simpleksu: porównanie ścieżki CPU vs GPU
3. Modele testowe z prostą formułą analityczną jako ground truth

Brak bitowej zgodności jest oczekiwany (kolejność operacji FP, FMA,
różne implementacje funkcji transcendentnych).

---

## 8. API — Faza I (CPU)

> Stan: **szkic po Etapie 1–5**. Aktualizowany w miarę stabilizacji.

### 8.1 Struktury danych

```cpp
// ----------------------------------------------------------------
// cooling_schedule.h
// ----------------------------------------------------------------
enum class CoolingSchedule {
    Boltzmann,   // T_k = T₀ / ln(1 + k)         [default]
    Geometric,   // T_k = T₀ · α^k
    Adaptive     // heurystyczny — do doprecyzowania
};

// ----------------------------------------------------------------
// fit_param.h
// ----------------------------------------------------------------
struct FitParam {
    std::string name;   // czytelna nazwa — CPU/debug/JSON
                        // GPU używa int index z ParameterMap
    double value;       // aktualna wartość (skala liniowa)
    double min, max;    // granice (skala liniowa)
    bool free;          // true = optymalizowany; false = stały
};

// ----------------------------------------------------------------
// sa_config.h
// ----------------------------------------------------------------
struct SAConfig {
    double T_initial   = 1.0;
    double T_current   = 1.0;   // modyfikowalne w runtime
    CoolingSchedule schedule = CoolingSchedule::Boltzmann;
    double geometric_rate    = 0.99;   // α dla Geometric
};

// ----------------------------------------------------------------
// simplex_state.h
// ----------------------------------------------------------------
struct SimplexState {
    std::vector<std::vector<double>> vertices;  // (N+1) × N_free
    std::vector<double> chi2_values;            // chi2 per wierzchołek
    int    best_idx, worst_idx;
    std::vector<double> centroid;               // N_free wartości
    int    iteration;
    double T_current;
};

enum class StepType {
    Reflection,
    Expansion,
    Contraction,
    Shrink,
    Restart
};

struct TraceStep {
    StepType     type;
    SimplexState state_before;
    SimplexState state_after;
    double       chi2_min;
    double       T;
    int          iteration;
};

// ----------------------------------------------------------------
// fit_result.h
// ----------------------------------------------------------------
struct FitResult {
    std::vector<double> best_params;  // skala liniowa, tylko free params
    double chi2_min;
    double delta_chi2;                // 0 jeśli brak chi2_floor
    int    iterations;
    bool   converged;
    std::string stop_reason;          // "tol", "max_iter", "degenerate", …
};
```

### 8.2 Model IV i funkcja celu

```cpp
// ----------------------------------------------------------------
// lambertw.h
// ----------------------------------------------------------------

/// LambertW(x) — gałąź główna W₀, iteracja Halleya.
/// Precondition: x >= -1/e
/// Wyniki weryfikowane przez scipy.special.lambertw z tol=1e-10
double lambertW(double x);

// ----------------------------------------------------------------
// diode_model.h
// ----------------------------------------------------------------

/// Ciemna charakterystyka IV (dark IV), model 1-diodowy bez fotoprądu.
/// params = {I₀, A, Rₛ, Rₛₕ} w skali liniowej
/// Vt = kT/q ≈ 0.02585 V (hardkodowane 300 K)
/// Zwraca I(V) przez jawną formułę z LambertW.
/// Zwraca NaN jeśli parametry są poza zakresem numerycznym.
double evaluateDiodeIV(double V, const double* params);

// ----------------------------------------------------------------
// objective.h
// ----------------------------------------------------------------

/// χ²(p) = Σ [(I_meas(Vᵢ) - I_model(Vᵢ, p))² / σᵢ²]
/// Zwraca +∞ jeśli którykolwiek I_model(Vᵢ) = NaN.
double computeChiSquared(
    const double* params,
    const double* V_data,
    const double* I_meas,
    const double* sigma,
    int           N_points,
    std::function<double(double V, const double*)> model_fn
);

/// Δχ²(p) = χ²(p) - chi2_min
double computeDeltaChiSquared(double chi2, double chi2_min);
```

### 8.3 Solver

```cpp
// ----------------------------------------------------------------
// sa_nelder_mead.h
// ----------------------------------------------------------------

class SANelderMead {
public:
    /// params      — wszystkie parametry modelu (free i fixed)
    /// sa_config   — konfiguracja SA i harmonogram chłodzenia
    /// objective   — funkcja celu f(double* free_params) → double
    ///               solver przekazuje tylko free params (packed vector)
    SANelderMead(
        std::vector<FitParam>               params,
        SAConfig                            sa_config,
        std::function<double(const double*)> objective
    );

    // --- Sterowanie iteracjami ---

    /// Wykonuje jeden krok NM/SA. Zwraca aktualne best χ².
    double step();

    /// Wykonuje n kroków.
    void runSteps(int n);

    /// Uruchamia solver do zbieżności lub max_iter.
    /// chi2_tol: zatrzymaj gdy χ²_best < chi2_tol
    FitResult runUntilConvergence(int max_iter, double chi2_tol);

    // --- Temperatura (runtime, bez restartu) ---

    /// Modyfikuje T_current. Nie resetuje licznika k ani T_initial.
    void setTemperature(double T);

    /// Przywraca T_current = T_initial i k = 0.
    void resetCooling();

    // --- Stan ---

    const SimplexState&      getState()      const;
    double                   getBestChi2()   const;
    std::vector<double>      getBestParams() const;  // tylko free, skala liniowa

    // --- Restart ---

    /// start_point: opcjonalny nowy punkt startowy (tylko free params).
    /// Brak → perturbacja aktualnego best.
    void restart(std::optional<std::vector<double>> start_point = std::nullopt);

    // --- Debug trace ---

    bool trace_enabled = true;  // false → brak narzutu alokacji

    const std::vector<TraceStep>& getTrace() const;
    const TraceStep&              getTraceStep(int i) const;
    int                           getTraceSize() const;
    void                          clearTrace();

private:
    // NM coefficients (konfigurowalne przez setter lub konstruktor)
    double alpha_ = 1.0;  // reflection
    double gamma_ = 2.0;  // expansion
    double rho_   = 0.5;  // contraction
    double sigma_ = 0.5;  // shrink

    // Degeneracja: restart gdy max_vertex_distance < degenerate_tol_
    double degenerate_tol_ = 1e-12;

    // ...
};
```

### 8.4 Prefit JSON

```cpp
// ----------------------------------------------------------------
// prefit.h
// ----------------------------------------------------------------

/// Serializuje aktualny stan parametrów do JSON string.
/// model_id = fingerprint topologii grafu (hash string).
std::string exportPrefit(
    const std::vector<FitParam>& params,
    const std::string&           model_id
);

/// Deserializuje plik prefit.
/// - Parametr w JSON i w mapie       → wczytaj wartość
/// - Parametr nie w JSON, jest w mapie → zostaw wartość domyślną
/// - Parametr w JSON, nie w mapie    → ignoruj z ostrzeżeniem (warning)
/// - Niezgodny model_id              → warning (stderr), kontynuuj
std::vector<FitParam> importPrefit(
    const std::string& json_str,
    const std::string& expected_model_id
);
```

Format JSON:
```json
{
  "model_id": "sha256:abc123...",
  "parameters": [
    { "name": "I0",  "value": 1.23e-12, "free": true,  "min": 1e-15, "max": 1e-6 },
    { "name": "A",   "value": 1.5,      "free": true,  "min": 0.5,   "max": 3.0  },
    { "name": "Rs",  "value": 0.01,     "free": true,  "min": 0.0,   "max": 1.0  },
    { "name": "Rsh", "value": 1000.0,   "free": true,  "min": 10.0,  "max": 1e6  }
  ]
}
```

---

## 9. Zakres

### Faza I — wchodzi

- SA-Enhanced Nelder–Mead na CPU (jeden algorytm hybrydowy)
- Model IV: 4-parametrowa dioda z LambertW (CPU, iteracja Halleya)
- χ² i Δχ² jako funkcje celu
- CoolingSchedule: Boltzmann, Geometric, Adaptive
- Temperatura T jako runtime parameter, modyfikowalny w trakcie działania
- Debug trace: pełny zapis krok po kroku, nawigacja forward/backward
- Prefit: JSON import/export z model_id i walidacją
- Obsługa NaN (+∞), granic [min, max], degeneracji simpleksu
- Stabilne API gotowe do dokumentacji i handoffu do Fazy II

### Faza II — wchodzi (osobny chat)

- LambertW kernel CUDA
- ParameterMap i rozszerzenie code_gen o `double* params` (bez rekompilacji NVRTC per iterację)
- Kernel NVRTC dla modelu IV generowanego z grafu
- Pełen pipeline GPU: NM/SA → evaluateIV → reduce χ²
- Batch: wiele niezależnych dopasowań równolegle
- Walidacja CPU ↔ GPU (empiryczna tolerancja)

### Poza zakresem eksperymentu

- Pełna aplikacja produkcyjna
- Gradient-based methods (BFGS, L-BFGS)
- Optymalizacja kerneli GPU (shared memory, warp efficiency)
- GPU prefit (rozważyć po Fazie II)
- System pluginów

---

## 10. Ryzyka

| Ryzyko                           | Kategoria       | Mitygacja                                                   |
| -------------------------------- | --------------- | ----------------------------------------------------------- |
| Degeneracja simpleksu            | Algorytmiczne   | Detekcja (diameter < próg) + automatyczny restart           |
| NaN / overflow w modelu          | Algorytmiczne   | Traktuj NaN jako +∞; solver nie crashuje                    |
| Clipping na granicach [min, max] | Algorytmiczne   | Clipping + penalizacja; nie zaburzaj kształtu simpleksu     |
| Lokalne minima                   | Algorytmiczne   | SA z wysoką T + multi-start                                 |
| Zbyt szybkie/wolne chłodzenie    | Algorytmiczne   | Boltzmann jako safe default; strojenie empiryczne           |
| Wydajność FP64 na GPU            | Implementacyjne | RTX: ~1/32 vs FP32; zmierzyć w boju                         |
| CURAND per wątek (Faza II)       | Implementacyjne | Stan generatora jako część stanu solvera per dopasowanie    |
| Zmiana T przez UI podczas batch  | Systemowe       | Synchronizacja UI ↔ wątek GPU przez atomic / mutex          |
| NVRTC z wątku głównego           | Systemowe       | Kompilacja tylko przy zmianie grafu, nigdy podczas iteracji |

---

## 11. Pytania otwarte

- **Batch i prefit:** Jeden punkt startowy dla wszystkich dopasowywanych
  charakterystyk (batch) czy konieczny GPU prefit generujący indywidualne
  punkty startowe per charakterystyka? Do ustalenia po uruchomieniu simpleksu.

- **Strojenie SA:** Jaki T_initial i jaka szybkość Boltzmanna działają dla
  typowych modeli IV półprzewodnikowych? Brak odpowiedzi a priori — wymaga
  testów na realnych danych.

- **Tolerancja CPU ↔ GPU:** Jaka jest empiryczna tolerancja Δχ² między CPU
  i GPU dla typowego modelu IV? Do ustalenia podczas testów walidacyjnych.

- **Adaptive CoolingSchedule:** Konkretna heurystyka do doprecyzowania
  po zebraniu doświadczeń z Boltzmann i Geometric.

---

## 12. Fazy realizacji

| Faza           | Zakres                                                                   | Status                |
| -------------- | ------------------------------------------------------------------------ | --------------------- |
| **I**          | Pełna implementacja CPU — model, solver, trace, prefit, stabilizacja API | W toku                |
| **I → I+1**    | Dokumentacja API Fazy I → handoff do chatu Fazy II                       | Po ukończeniu Etapu 5 |
| **II**         | Implementacja GPU: LambertW CUDA, code_gen + ParameterMap, batch         | Osobny chat           |
| **II → finał** | Walidacja CPU ↔ GPU, integracja z node editorem                          | Po Fazie II           |

Szczegółowy plan Fazy I: patrz **implementation-plan.md**.