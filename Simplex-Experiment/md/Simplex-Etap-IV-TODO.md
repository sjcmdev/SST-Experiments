# Plan Implementacji — Etap 4 (Szczegółowy)
## Debug trace

> **Założenie:** Etapy 1, 2 i 3 zakończyły się dokładnie tak, jak
> zaprojektowano. `nelder_mead.hpp/cpp` istnieje w wersji z Etapu 3
> (konstruktor z `sa_enabled`/`sa_config`/`rng_seed`, `step()` z kryterium
> Metropolis, harmonogramy chłodzenia).
>
> **Cel:** Pełna obserwowalność każdego kroku solvera, bez narzutu gdy
> wyłączona. Trace jest fundamentem weryfikacji Fazy II (GPU).

---

## 1. Pliki — co się zmienia, co zostaje bez zmian

```
src/solver/
├── phys_const.hpp              ← BEZ ZMIAN
├── solver_types.hpp            ← BEZ ZMIAN — TraceStep już istnieje z Etapu 1
├── lambertw.hpp                ← BEZ ZMIAN
├── diode_model.hpp / .cpp      ← BEZ ZMIAN
├── objective.hpp / .cpp        ← BEZ ZMIAN
├── param_utils.hpp / .cpp      ← BEZ ZMIAN
├── diode_objective.hpp / .cpp  ← BEZ ZMIAN
├── nelder_mead.hpp / .cpp      ← MODYFIKOWANE — sekcje 4–5
└── trace_utils.hpp / .cpp      ← NOWY — sekcja 6

src/tests/
├── test_stage1–3.hpp / .cpp    ← BEZ ZMIAN
└── test_stage4.hpp / .cpp      ← NOWY — sekcja 7
```

Żaden nowy plik produkcyjny poza `trace_utils` — trace jest nierozerwalnie
związany z `step()`.

---

## 2. Decyzje architektoniczne

### A. `step()` jako cienki wrapper wokół `stepInternal()`

Cała logika algorytmu z Etapów 2–3 zostaje przeniesiona **bez zmian** do
nowej prywatnej metody `stepInternal()`. Publiczne `step()` staje się
wrapperem:

```cpp
StepType SANelderMead::step()
{
    if (!trace_enabled_)
        return stepInternal();          // ścieżka bez narzutu

    const SimplexState state_before = state_;   // kopia PRZED
    const StepType type = stepInternal();
    // budowa TraceStep i push_back...
}
```

Sygnatura i zachowanie `step()` **nie zmieniają się**. Wszystkie testy
Etapów 2–3 działają identycznie, bo `trace_enabled_` domyślnie `false`.

### B. `TraceStep.T` = temperatura **użyta** w decyzji tego kroku

`updateCooling()` jest wywoływane na **końcu** `stepInternal()` — po tym,
jak kryterium Metropolis już podjęło decyzję na podstawie *poprzedniej*
wartości `T_current`. Dlatego: `TraceStep.T = state_before.T_current`,
nie `state_after.T_current`.

**Poprawka konieczna w Etapie 4:** `initSimplexAround()` w Etapie 3 nie
synchronizowało `state_.T_current`. Wystarczyło, bo Etap 3 czytał
temperaturę przez `getTemperature()` (z `sa_config_.T_current`). Ale
pierwszy wpis trace czytałby `state_before.T_current == 0` zamiast
`T_initial`. Dodajemy na końcu `initSimplexAround()`:

```cpp
state_.T_current = sa_config_.T_current;   // NOWE — wymagane przez trace
```

Test `testTraceFirstStepTemperatureCorrect` (sekcja 7.5) weryfikuje to wprost.

### C. Monotoniczność `chi2_best` — bez wyjątku, nie "z wyjątkiem SA"

SA podmienia wyłącznie slot `worst`, nigdy `best`. `chi2_best` jest
monotonicznie nierosnące **nawet przez akceptacje SA** — silniejszy
niezmiennik niż wymaga dokument. Test 7.2 udowadnia to empirycznie.

### D. Numeracja `TraceStep.iteration` — 1-indeksowana

`stepInternal()` inkrementuje `state_.iteration` na samym końcu.
`TraceStep.iteration` czytane po wywołaniu `stepInternal()` → pierwszy
wpis ma `iteration=1`. Niezmiennik: `trace_[i].iteration == i + 1`.

### E. ASCII `->` zamiast Unicode `→`

MSVC narrow execution charset nie jest gwarantowanym UTF-8 bez `/utf-8`.
`traceStepToString` używa portable `->`. Kosmetyczna zmiana jednej linii
gdy `/utf-8` potwierdzone w projekcie.

### F. `clearTrace()` = `clear()` + `shrink_to_fit()`

Sam `clear()` nie gwarantuje zwolnienia `capacity()`. Wymóg *"zwalnia
pamięć"* z `experiment-plan.md` wymaga jawnego `shrink_to_fit()`.

### G. Konstruktor ma teraz 11 parametrów — kandydat do refaktoryzacji w Etapie 5

Każdy etap dopisuje nowy parametr na końcu (wymóg kompatybilności wstecznej).
Etap 5 ("stabilizacja API") to właściwy moment na zebranie ich w
`struct SolverConfig`. Nie robimy tego teraz.

---

## 3. `solver_types.hpp` — potwierdzenie zgodności (zero zmian)

```cpp
struct TraceStep {
    StepType     type;
    SimplexState state_before;
    SimplexState state_after;
    double       chi2_min;   // = state_after.chi2_values[state_after.best_idx]
    double       T;          // = state_before.T_current (T użyte w decyzji)
    int          iteration;  // = state_.iteration PO inkrementacji
};
```

---

## 4. `nelder_mead.hpp` — zaktualizowany interfejs

```cpp
// src/solver/nelder_mead.hpp
#pragma once
#include "solver_types.hpp"
#include <functional>
#include <random>
#include <string>
#include <vector>

class SANelderMead {
public:
    using ObjectiveFn = std::function<double(const std::vector<double>&)>;

    /// Nowy parametr Etapu 4: trace_enabled (na końcu — kompatybilność wsteczna).
    SANelderMead(
        std::vector<FitParam> all_params,
        ObjectiveFn           objective_fn,
        double alpha = 1.0,
        double gamma = 2.0,
        double rho   = 0.5,
        double sigma = 0.5,
        double degenerate_tol = 1e-12,
        bool sa_enabled = false,
        SAConfig sa_config = SAConfig{},
        unsigned int rng_seed = 0,
        bool trace_enabled = false
    );

    static SANelderMead withSA(
        std::vector<FitParam> all_params,
        ObjectiveFn            objective_fn,
        SAConfig                sa_config,
        unsigned int            rng_seed);

    void initSimplex();
    StepType step();
    FitResult runUntilConvergence(int max_iter, double chi2_tol);

    // ─── Odczyt stanu (Etap 2, bez zmian) ──────────────────────────────────
    const SimplexState& state()     const noexcept { return state_; }
    int                  iteration() const noexcept { return state_.iteration; }
    double bestChiSquared() const noexcept { return state_.chi2_values[state_.best_idx]; }
    std::vector<double> bestParams() const { return state_.vertices[state_.best_idx]; }
    std::vector<double> bestFullParams() const;

    // ─── Kontrola temperatury (Etap 3, bez zmian) ──────────────────────────
    void setTemperature(double T);
    void resetCooling();
    void setCoolingSchedule(CoolingSchedule schedule);
    double          getTemperature()     const noexcept { return sa_config_.T_current; }
    const SAConfig& getSAConfig()        const noexcept { return sa_config_; }
    bool            isSAEnabled()        const noexcept { return sa_enabled_; }
    int             getSAAcceptedCount() const noexcept { return sa_accepted_count_; }

    // ─── Etap 4: trace ──────────────────────────────────────────────────────
    /// Bezpieczne w dowolnym momencie — nie wymaga sa_enabled=true, nie rzuca.
    void setTraceEnabled(bool enabled) noexcept { trace_enabled_ = enabled; }
    bool isTraceEnabled() const noexcept { return trace_enabled_; }

    const std::vector<TraceStep>& getTrace() const noexcept { return trace_; }

    /// Rzuca std::out_of_range gdy i poza [0, getTraceSize()).
    const TraceStep& getTraceStep(int i) const;

    int getTraceSize() const noexcept { return static_cast<int>(trace_.size()); }

    /// Czyści trace i ZWALNIA pamięć (clear + shrink_to_fit).
    void clearTrace();

private:
    std::vector<FitParam> all_params_;
    std::vector<int>      free_indices_;
    ObjectiveFn            objective_fn_;
    double alpha_, gamma_, rho_, sigma_;
    double degenerate_tol_;
    std::vector<double> free_min_;
    std::vector<double> free_max_;
    SimplexState state_;

    bool         sa_enabled_;
    SAConfig     sa_config_;
    int          cooling_k_ = 0;
    std::mt19937 rng_;
    std::uniform_real_distribution<double> uniform_dist_{0.0, 1.0};
    int          sa_accepted_count_ = 0;

    bool                    trace_enabled_;
    std::vector<TraceStep>  trace_;

    void   initSimplexAround(const std::vector<double>& start);
    void   restart();
    bool   isDegenerate() const;
    void   clipToBounds(std::vector<double>& v) const;
    bool   withinBounds(const std::vector<double>& v) const;
    double evaluateVertex(const std::vector<double>& free_values) const;
    void   sortIndices(int& best, int& second_worst, int& worst) const;
    std::vector<double> computeCentroidExcluding(int exclude_idx) const;
    void   shrinkSimplex(int best_idx);
    FitResult buildResult(bool converged, const std::string& stop_reason) const;
    double computeBoltzmannTemperature(int k) const;
    double computeGeometricTemperature(int k) const;
    void   updateCooling();

    /// Cała logika algorytmu z Etapów 2–3. Publiczne step() jest wrapperem.
    StepType stepInternal();
};

std::vector<double> reflectPoint(
    const std::vector<double>& centroid, const std::vector<double>& worst, double alpha);
std::vector<double> expandPoint(
    const std::vector<double>& centroid, const std::vector<double>& x_r, double gamma);
std::vector<double> contractPoint(
    const std::vector<double>& centroid, const std::vector<double>& worst, double rho);
```

---

## 5. `nelder_mead.cpp` — kluczowe zmiany względem Etapu 3

Pełen plik = Etap 3 + te modyfikacje:

### 5.1 Konstruktor — nowy parametr `trace_enabled`

```cpp
SANelderMead::SANelderMead(
    ..., bool trace_enabled)
    : ...
    , trace_enabled_(trace_enabled)
{ ... }  // reszta bez zmian
```

### 5.2 `initSimplexAround()` — synchronizacja T_current (NOWE)

Dopisane na samym końcu funkcji, po sortowaniu i obliczeniu chi2:

```cpp
// Synchronizacja wymagana przez trace — patrz "Decyzje architektoniczne" (B)
state_.T_current = sa_config_.T_current;
```

### 5.3 Cała logika `step()` przeniesiona do `stepInternal()`

Skopiuj dosłownie ciało funkcji `step()` z Etapu 3 do nowej metody
`stepInternal()`. Sygnatura: `StepType SANelderMead::stepInternal()`.

### 5.4 Nowe publiczne `step()` — wrapper z trace

```cpp
StepType SANelderMead::step()
{
    if (!trace_enabled_)
        return stepInternal();   // zero narzutu gdy trace wyłączone

    const SimplexState state_before = state_;       // głęboka kopia PRZED
    const StepType type = stepInternal();            // mutuje state_

    TraceStep ts;
    ts.type         = type;
    ts.state_before = state_before;
    ts.state_after  = state_;                        // głęboka kopia PO
    ts.chi2_min     = ts.state_after.chi2_values[ts.state_after.best_idx];
    ts.T            = state_before.T_current;        // T użyte w decyzji
    ts.iteration    = state_.iteration;              // już zinkrementowane

    trace_.push_back(std::move(ts));
    return type;
}
```

### 5.5 API trace (NOWE)

```cpp
const TraceStep& SANelderMead::getTraceStep(int i) const
{
    if (i < 0 || i >= static_cast<int>(trace_.size()))
        throw std::out_of_range(
            "SANelderMead::getTraceStep: indeks " + std::to_string(i) +
            " poza zakresem [0," + std::to_string(trace_.size()) + ")");
    return trace_[static_cast<size_t>(i)];
}

void SANelderMead::clearTrace()
{
    trace_.clear();
    trace_.shrink_to_fit();   // faktyczne zwolnienie — patrz decyzja (F)
}
```

---

## 6. `trace_utils.hpp` + `.cpp` — nowy plik

### 6.1 `trace_utils.hpp`

```cpp
// src/solver/trace_utils.hpp
#pragma once
#include "solver_types.hpp"
#include <string>

const char* stepTypeToString(StepType type);

/// Format: "[iter=42 T=0.012 Shrink] chi2: 0.034 -> 0.029"
std::string traceStepToString(const TraceStep& step);
```

### 6.2 `trace_utils.cpp`

```cpp
// src/solver/trace_utils.cpp
#include "trace_utils.hpp"
#include <cstdio>

const char* stepTypeToString(StepType type)
{
    switch (type) {
        case StepType::Reflection:  return "Reflection";
        case StepType::Expansion:   return "Expansion";
        case StepType::Contraction: return "Contraction";
        case StepType::Shrink:      return "Shrink";
        case StepType::Restart:     return "Restart";
    }
    return "Unknown";
}

std::string traceStepToString(const TraceStep& step)
{
    const double chi2_before = step.state_before.chi2_values[step.state_before.best_idx];
    const double chi2_after  = step.state_after.chi2_values[step.state_after.best_idx];

    char buf[256];
    std::snprintf(buf, sizeof(buf), "[iter=%d T=%.3f %s] chi2: %.3f -> %.3f",
                  step.iteration, step.T, stepTypeToString(step.type),
                  chi2_before, chi2_after);
    return std::string(buf);
}
```

---

## 7. Testy

### 7.1 `getTraceSize() == iterations`

```cpp
static void testTraceSizeMatchesIterations()
{
    std::vector<FitParam> p = { FitParam("x", 5.0, -10.0, 10.0, true) };
    SANelderMead::ObjectiveFn obj = [](const std::vector<double>& x) { return x[0]*x[0]; };

    SANelderMead solver(p, obj, 1.0,2.0,0.5,0.5,1e-12,
                         false, SAConfig{}, 0, /*trace_enabled*/true);
    solver.initSimplex();
    const FitResult r = solver.runUntilConvergence(1000, 1e-10);

    assert(solver.getTraceSize() == r.iterations);
    assert(solver.getTraceSize() == solver.iteration());
    fprintf(stdout, "[Trace size] PASS — %d kroków\n", solver.getTraceSize());
}
```

### 7.2 Monotoniczność `chi2_best` — w tym przez akceptacje SA

```cpp
static void testTraceChi2BestMonotonic()
{
    std::vector<FitParam> p = { FitParam("x", -1.0, -5.0, 5.0, true) };
    SANelderMead::ObjectiveFn obj = [](const std::vector<double>& x) -> double {
        const double l = (x[0]+1.0)*(x[0]+1.0), r = (x[0]-1.0)*(x[0]-1.0);
        return std::min(l,r) - 0.05*x[0];
    };

    SAConfig sa; sa.T_initial = 5.0;
    sa.schedule = CoolingSchedule::Geometric; sa.geometric_rate = 0.995;
    SANelderMead solver(p, obj, 1.0,2.0,0.5,0.5,1e-9,
                         true, sa, 7u, /*trace_enabled*/true);
    solver.initSimplex();
    for (int i = 0; i < 1000; ++i) solver.step();

    // Weryfikacja T w pierwszym kroku (regresja poprawki)
    assert(std::abs(solver.getTraceStep(0).T - 5.0) < 1e-9);

    double prev = solver.getTraceStep(0).state_before.chi2_values[
                     solver.getTraceStep(0).state_before.best_idx];
    for (int i = 0; i < solver.getTraceSize(); ++i) {
        const auto& ts = solver.getTraceStep(i);
        const double after = ts.state_after.chi2_values[ts.state_after.best_idx];
        assert(after <= prev + 1e-12);
        prev = after;
    }
    assert(solver.getSAAcceptedCount() > 0 && "Test nie objął żadnej akceptacji SA");
    fprintf(stdout, "[chi2 monotonic] PASS — %d kroków, %d akceptacji SA\n",
            solver.getTraceSize(), solver.getSAAcceptedCount());
}
```

### 7.3 Ręczna inspekcja 20 kroków

```cpp
static void testTraceManualReview()
{
    std::vector<FitParam> p = {
        FitParam("x", 5.0, -10.0, 10.0, true),
        FitParam("y", 5.0, -10.0, 10.0, true),
    };
    SANelderMead::ObjectiveFn obj = [](const std::vector<double>& v) {
        return v[0]*v[0] + v[1]*v[1];
    };

    SANelderMead solver(p, obj, 1.0,2.0,0.5,0.5,1e-12,
                         false, SAConfig{}, 0, true);
    solver.initSimplex();
    for (int i = 0; i < 20; ++i) solver.step();
    assert(solver.getTraceSize() == 20);

    fprintf(stdout, "\n[Trace manual review] 20 kroków:\n");
    for (int i = 0; i < 20; ++i) {
        const auto& ts = solver.getTraceStep(i);
        assert(ts.iteration == i + 1);
        assert(ts.state_before.vertices.size() == 3);
        assert(ts.state_after.vertices.size() == 3);
        assert(ts.chi2_min == ts.state_after.chi2_values[ts.state_after.best_idx]);
        fprintf(stdout, "  %s\n", traceStepToString(ts).c_str());
    }
    fprintf(stdout, "[Trace manual review] PASS\n");
}
```

### 7.4 `trace_enabled=false` — brak narzutu

```cpp
static void testTraceDisabledNotSlower()
{
    auto makeP = []() { return std::vector<FitParam>{
        FitParam("a",5.0,-100.0,100.0,true), FitParam("b",5.0,-100.0,100.0,true),
        FitParam("c",5.0,-100.0,100.0,true), FitParam("d",5.0,-100.0,100.0,true) }; };
    SANelderMead::ObjectiveFn obj = [](const std::vector<double>& x) {
        double s=0; for(double v:x) s+=v*v; return s; };
    const int kSteps = 5000;

    SANelderMead off(makeP(), obj);    // trace_enabled=false (default)
    off.initSimplex();
    auto t0 = std::chrono::steady_clock::now();
    for (int i=0; i<kSteps; ++i) off.step();
    double ms_off = std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-t0).count();

    SANelderMead on(makeP(), obj, 1.0,2.0,0.5,0.5,1e-12,false,SAConfig{},0,true);
    on.initSimplex();
    t0 = std::chrono::steady_clock::now();
    for (int i=0; i<kSteps; ++i) on.step();
    double ms_on = std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-t0).count();

    assert(off.getTraceSize() == 0);
    assert(on.getTraceSize() == kSteps);
    assert(ms_off <= ms_on + 2.0);   // margines 2ms na szum pomiaru
    fprintf(stdout, "[Trace perf] off=%.1fms on=%.1fms PASS\n", ms_off, ms_on);
}
```

### 7.5 Poprawna temperatura w pierwszym kroku

```cpp
static void testTraceFirstStepTemperatureCorrect()
{
    std::vector<FitParam> p = { FitParam("x",5.0,-10.0,10.0,true) };
    SANelderMead::ObjectiveFn obj = [](const std::vector<double>& x){return x[0]*x[0];};
    SAConfig sa; sa.T_initial=17.0; sa.schedule=CoolingSchedule::Geometric; sa.geometric_rate=0.99;
    SANelderMead solver(p, obj, 1.0,2.0,0.5,0.5,1e-12, true, sa, 5u, true);
    solver.initSimplex();
    solver.step();
    assert(std::abs(solver.getTraceStep(0).T - 17.0) < 1e-9);
    fprintf(stdout, "[Trace T sync] T[0]=%.6f PASS\n", solver.getTraceStep(0).T);
}
```

### 7.6 Bounds check `getTraceStep`

```cpp
static void testTraceGetStepBoundsCheck()
{
    std::vector<FitParam> p = { FitParam("x",5.0,-10.0,10.0,true) };
    SANelderMead solver(p, [](const std::vector<double>& x){return x[0]*x[0];},
                         1.0,2.0,0.5,0.5,1e-12,false,SAConfig{},0,true);
    solver.initSimplex();
    for (int i=0;i<5;++i) solver.step();

    bool ok1=false; try{(void)solver.getTraceStep(-1);}catch(const std::out_of_range&){ok1=true;}
    bool ok2=false; try{(void)solver.getTraceStep(5);}catch(const std::out_of_range&){ok2=true;}
    (void)solver.getTraceStep(4);   // poprawny — nie rzuca
    assert(ok1 && ok2);
    fprintf(stdout, "[Trace bounds] PASS\n");
}
```

### 7.7 `clearTrace()` zwalnia pamięć

```cpp
static void testClearTrace()
{
    std::vector<FitParam> p = { FitParam("x",5.0,-10.0,10.0,true) };
    SANelderMead solver(p, [](const std::vector<double>& x){return x[0]*x[0];},
                         1.0,2.0,0.5,0.5,1e-12,false,SAConfig{},0,true);
    solver.initSimplex();
    for (int i=0;i<50;++i) solver.step();
    assert(solver.getTraceSize()==50);

    solver.clearTrace();
    assert(solver.getTraceSize()==0);
    assert(solver.getTrace().capacity()==0);

    for (int i=0;i<10;++i) solver.step();   // działa normalnie po clearTrace
    assert(solver.getTraceSize()==10);
    fprintf(stdout, "[Clear trace] PASS\n");
}
```

### 7.8 Format `traceStepToString`

```cpp
static void testTraceStepToStringFormat()
{
    std::vector<FitParam> p = { FitParam("x",5.0,-10.0,10.0,true) };
    SANelderMead solver(p, [](const std::vector<double>& x){return x[0]*x[0];},
                         1.0,2.0,0.5,0.5,1e-12,false,SAConfig{},0,true);
    solver.initSimplex(); solver.step();
    const std::string s = traceStepToString(solver.getTraceStep(0));
    assert(s.find("iter=1")!=std::string::npos);
    assert(s.find("T=")!=std::string::npos);
    assert(s.find("chi2:")!=std::string::npos);
    assert(s.find("->")!=std::string::npos);
    fprintf(stdout, "[Trace string] \"%s\" PASS\n", s.c_str());
}
```

### 7.9 Toggle trace w trakcie działania

```cpp
static void testTraceToggleMidRun()
{
    std::vector<FitParam> p = { FitParam("x",5.0,-10.0,10.0,true) };
    SANelderMead solver(p, [](const std::vector<double>& x){return x[0]*x[0];});
    solver.initSimplex();

    for (int i=0;i<10;++i) solver.step();
    assert(solver.getTraceSize()==0);

    solver.setTraceEnabled(true);
    for (int i=0;i<15;++i) solver.step();
    assert(solver.getTraceSize()==15);

    solver.setTraceEnabled(false);
    for (int i=0;i<20;++i) solver.step();
    assert(solver.getTraceSize()==15);

    assert(solver.iteration()==45);
    fprintf(stdout, "[Trace toggle] iteration=%d trace=%d PASS\n",
            solver.iteration(), solver.getTraceSize());
}
```

---

## 8. Wiring, CMake, kolejność

### `app.cpp`

```cpp
#ifndef NDEBUG
#include "tests/test_stage1.hpp"
#include "tests/test_stage2.hpp"
#include "tests/test_stage3.hpp"
#include "tests/test_stage4.hpp"
#endif

void appInit(AppState& appState) {
#ifndef NDEBUG
    runStage1Tests(); runStage2Tests(); runStage3Tests(); runStage4Tests();
#endif
}
```

### CMake (dopiski)

```cmake
add_library(solver_core STATIC
    ...
    src/solver/trace_utils.cpp      # NOWE
)
add_library(solver_tests STATIC
    ...
    src/tests/test_stage4.cpp       # NOWE
)
```

### Kolejność implementacji (~4h40m)

```
Blok 1 — Infrastruktura (1h):
  [20m] step()→stepInternal(), nowe pola trace_enabled_/trace_
  [25m] nowe step() wrapper (kopia before/after, TraceStep, push_back)
  [15m] POPRAWKA initSimplexAround(): state_.T_current sync
        → uruchom testy E1+E2+E3 — MUSZĄ przejść bez zmian

Blok 2 — API trace (40m):
  [15m] getTrace/getTraceStep/getTraceSize/clearTrace
  [10m] setTraceEnabled/isTraceEnabled
  [15m] trace_utils.hpp/cpp

Blok 3 — Testy (2h):
  [15m] testTraceSizeMatchesIterations
  [25m] testTraceChi2BestMonotonic
  [20m] testTraceManualReview
  [20m] testTraceDisabledNotSlower
  [10m] testTraceFirstStepTemperatureCorrect
  [10m] testTraceGetStepBoundsCheck
  [10m] testClearTrace
  [10m] testTraceStepToStringFormat
  [10m] testTraceToggleMidRun

Blok 4 — Integracja (30m)
```

---

## 9. Checklist ukończenia

| #   | Wymaganie                                         | Weryfikacja                        |
| --- | ------------------------------------------------- | ---------------------------------- |
| 1   | `TraceStep` po każdym kroku                       | testTraceSizeMatchesIterations     |
| 2   | `trace_enabled=false` → zero narzutu              | **testTraceDisabledNotSlower**     |
| 3   | Każdy krok zapisywany niezależnie od `StepType`   | testTraceManualReview              |
| 4   | `getTrace() const&`                               | —                                  |
| 5   | `getTraceStep(i)` z bounds check                  | **testTraceGetStepBoundsCheck**    |
| 6   | `getTraceSize()`                                  | testTraceSizeMatchesIterations     |
| 7   | `clearTrace()` zwalnia pamięć                     | **testClearTrace**                 |
| 8   | `traceStepToString` — poprawny format             | testTraceStepToStringFormat        |
| 9   | `size==iterations` po `runUntilConvergence`       | **testTraceSizeMatchesIterations** |
| 10  | `chi2_best` nierosnące (w tym przez SA)           | **testTraceChi2BestMonotonic**     |
| 11  | Typ operacji zgodny z logiką, ręczna weryfikacja  | **testTraceManualReview**          |
| 12  | `trace_enabled=false` → `size==0`, nie wolniejsze | **testTraceDisabledNotSlower**     |

**Etap 4 gotowy = wszystkie 12 ✓ + testy E1–E3 przechodzą bez zmian.**