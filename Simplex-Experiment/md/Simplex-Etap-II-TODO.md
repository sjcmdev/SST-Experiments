# Plan Implementacji — Etap 2 (Szczegółowy)
## Nelder–Mead (bez SA) — rdzeń solvera

> **Buduje na:** Etap 1 (`phys_const.hpp`, `solver_types.hpp`, `lambertw.hpp`,
> `diode_model.hpp/cpp`, `objective.hpp/cpp`) — **żaden plik Etapu 1 nie jest
> modyfikowany.**
> **Cel:** Działający, deterministyczny solver NM. Na końcu tego etapu solver
> potrafi odtworzyć parametry IV z syntetycznych danych.

---

## 1. Drzewo plików do stworzenia

```
src/solver/
├── phys_const.hpp          ← Etap 1, bez zmian
├── solver_types.hpp        ← Etap 1, bez zmian — FitParam, SimplexState,
│                                StepType, FitResult używane bezpośrednio
├── lambertw.hpp             ← Etap 1, bez zmian
├── diode_model.hpp / .cpp   ← Etap 1, bez zmian — evaluateDiodeIV4 używana w testach
├── objective.hpp / .cpp     ← Etap 1, bez zmian — computeChiSquared używana wewnątrz
│
├── param_utils.hpp / .cpp        ← NOWY: mapowanie free↔full parametry — sekcja 3
├── diode_objective.hpp / .cpp    ← NOWY: łącznik model+dane → ObjectiveFn — sekcja 4
└── nelder_mead.hpp / .cpp        ← NOWY: klasa SANelderMead (rdzeń NM) — sekcje 5–6

src/tests/
├── test_stage1.hpp / .cpp   ← NOWY (refaktoryzacja): przeniesione testy Etapu 1
└── test_stage2.hpp / .cpp   ← NOWY: testy Etapu 2 — sekcja 7
```

**Uwaga o nazewnictwie:** moduł `param_utils` celowo NIE nazywa się
`ParameterMap` — ta nazwa jest zarezerwowana dla przyszłego mechanizmu GPU
(`ParameterMap` z `double* params`, indeksy zamiast nazw — patrz notatki
projektu o architekturze GPU). CPU i GPU rozwiązują ten sam problem (mapowanie
parametr→wartość) różnymi mechanizmami i celowo różnymi nazwami.

---

## 2. Decyzje architektoniczne Etapu 2

Zanim przejdziemy do kodu — cztery decyzje, które determinują kształt całej
reszty etapu. Każda ma swoje uzasadnienie wynikające wprost z `experiment-plan.md`.

### A. `ObjectiveFn` jako abstrakcja generyczna

`SANelderMead` **nie wie nic o diodach**. Przyjmuje
`std::function<double(const std::vector<double>&)>` — funkcję, która z wektora
wolnych parametrów robi liczbę do minimalizacji. To rozdzielenie ma konkretny,
testowalny powód: kryterium ukończenia Etapu 2 wymaga zminimalizowania
`f(x) = Σ(xᵢ-cᵢ)²` — funkcji, która **nie ma nic wspólnego** z modelem IV.
Gdyby solver był zaszyty na sztywno pod `evaluateDiodeIV4`, ten test wymagałby
sztucznego obejścia.

Połączenie z modelem fizycznym dzieje się **na zewnątrz** klasy, przez
`makeDiodeIVObjective()` (sekcja 4) — funkcję, która bierze model + dane
pomiarowe i zwraca gotową `ObjectiveFn`. To dokładnie realizuje zasadę
"solver jest model-agnostyczny" już przyjętą w projekcie.

### B. Nie-destrukcyjne sortowanie

`SimplexState` (Etap 1) ma jawne pola `best_idx`/`worst_idx` — gdyby algorytm
fizycznie sortował `vertices` po każdym kroku, te pola byłyby zbędne (best
zawsze byłby na indeksie 0). Zamiast tego: `vertices`/`chi2_values` **zachowują
swoją pozycję** (slot) między krokami; tylko zawartość danego slotu się zmienia,
gdy zostaje on "przegrany" wierzchołek zastępowany nowym kandydatem.
`sortIndices()` liczy tymczasową permutację indeksów (tanie — N≤7 elementów),
nie rusza samej tablicy.

Korzyść poza wydajnością: to ułatwi Etap 4 (trace), gdzie `state_before` vs
`state_after` będzie zwykle różnić się tylko w JEDNYM slocie (poza Shrink) —
czytelniejsze do ręcznej weryfikacji.

### C. Próg akceptacji reflection: `second_worst` vs `worst` — i gdzie wchodzi SA

To najbardziej subtelna decyzja tego etapu — warto ją zrozumieć teraz, żeby
Etap 3 nie wymagał przepisywania `step()`.

Klasyczny Nelder-Mead rozróżnia **outside** i **inside contraction** (dwie
różne formuły, używane zależnie od tego, czy `f(x_r)` mieści się między
`second_worst` a `worst`, czy jest gorsze nawet od `worst`).
`experiment-plan.md` podaje **tylko jedną** formułę kontrakcji:
`x_c = centroid + ρ·(worst - centroid)` — klasycznie jest to wariant *inside*.

W Etapie 2 ta jedna formuła jest używana jednolicie dla **całego** zakresu
`f(x_r) ≥ f(second_worst)` (i "outside", i "inside" klasycznego NM są tu
połączone w jedną gałąź). Próg `second_worst` pozostaje punktem podziału:
**przyjmij reflection wprost** / **idź do kontrakcji**.

Etap 3 (`f(x_r) ≥ f(worst)` jako wyzwalacz SA — patrz `experiment-plan.md`)
precyzuje **węższy** podwarunek **wewnątrz** tej samej gałęzi "idź do
kontrakcji" z Etapu 2. SA dostaje swoją szansę tylko w przypadku, gdy
reflection jest gorsza nawet od bieżącego najgorszego punktu — nie w całym
zakresie `f_r ≥ f_second_worst`. W kodzie poniżej (sekcja 6) to miejsce jest
oznaczone komentarzem `ETAP 3 HOOK`. Dzięki tej precyzji Etap 3 wstawi tylko
nowy blok kodu w tym miejscu — bez zmiany istniejącej struktury `step()`.

### D. Walidacja: wyjątki przy konstrukcji, `+∞` w trakcie pracy

Dwie różne kategorie błędów, dwa różne mechanizmy:

| Kategoria                                    | Przykład                               | Mechanizm                                     | Uzasadnienie                                                   |
| -------------------------------------------- | -------------------------------------- | --------------------------------------------- | -------------------------------------------------------------- |
| Błąd programisty (zła konfiguracja)          | `min >= max`, brak wolnych parametrów  | `throw std::invalid_argument` w konstruktorze | Fail-fast — to się nie powinno zdarzyć w działającym programie |
| Błąd/przypadek danych (w trakcie poszukiwań) | NaN z modelu, wierzchołek poza granicą | zwróć `+∞`, nigdy nie rzucaj                  | Zgodne z Etapem 1: `computeChiSquared` też nigdy nie rzuca     |

To rozróżnienie jest konsekwentne z Etapem 1, gdzie `evaluateDiodeIV4` i
`computeChiSquared` są `noexcept`/nigdy-nie-rzucające z założenia.

---

## 3. `param_utils.hpp` + `.cpp` — mapowanie free ↔ full

Solver operuje na wektorze **tylko wolnych** parametrów (`free_values`,
długość N_free). Funkcja modelu (np. `evaluateDiodeIV4`) oczekuje **pełnego**
wektora (`double* params`, długość 4 lub 6, w tym parametry stałe). Te cztery
funkcje są jedynym miejscem, gdzie ta konwersja się dzieje.

### 3.1 `param_utils.hpp`

```cpp
// src/solver/param_utils.hpp
#pragma once
#include "solver_types.hpp"
#include <vector>

/// Zwraca indeksy (w all_params) parametrów z free==true, w kolejności
/// występowania w all_params. len(wynik) = N_free.
std::vector<int> freeIndices(const std::vector<FitParam>& all_params);

/// Buduje PEŁNY wektor parametrów (len = all_params.size()), podstawiając
/// free_values pod indeksy free_indices, a pozostałe biorąc z all_params[i].value.
///
/// Precondition: free_values.size() == free_indices.size()
std::vector<double> buildFullParams(
    const std::vector<double>&   free_values,
    const std::vector<FitParam>& all_params,
    const std::vector<int>&      free_indices);

/// Wyciąga aktualne wartości (FitParam::value) TYLKO dla wolnych parametrów,
/// w kolejności free_indices. Użyteczne jako punkt startowy simpleksu.
std::vector<double> extractFreeValues(
    const std::vector<FitParam>& all_params,
    const std::vector<int>&      free_indices);

/// Wyciąga granice [min,max] TYLKO dla wolnych parametrów, w kolejności
/// free_indices.
void extractFreeBounds(
    const std::vector<FitParam>& all_params,
    const std::vector<int>&      free_indices,
    std::vector<double>&         out_min,
    std::vector<double>&         out_max);
```

### 3.2 `param_utils.cpp`

```cpp
// src/solver/param_utils.cpp
#include "param_utils.hpp"

std::vector<int> freeIndices(const std::vector<FitParam>& all_params)
{
    std::vector<int> result;
    for (size_t i = 0; i < all_params.size(); ++i)
        if (all_params[i].free)
            result.push_back(static_cast<int>(i));
    return result;
}

std::vector<double> buildFullParams(
    const std::vector<double>&   free_values,
    const std::vector<FitParam>& all_params,
    const std::vector<int>&      free_indices)
{
    std::vector<double> full(all_params.size());
    for (size_t i = 0; i < all_params.size(); ++i)
        full[i] = all_params[i].value;
    for (size_t j = 0; j < free_indices.size(); ++j)
        full[free_indices[j]] = free_values[j];
    return full;
}

std::vector<double> extractFreeValues(
    const std::vector<FitParam>& all_params,
    const std::vector<int>&      free_indices)
{
    std::vector<double> result(free_indices.size());
    for (size_t j = 0; j < free_indices.size(); ++j)
        result[j] = all_params[free_indices[j]].value;
    return result;
}

void extractFreeBounds(
    const std::vector<FitParam>& all_params,
    const std::vector<int>&      free_indices,
    std::vector<double>&         out_min,
    std::vector<double>&         out_max)
{
    out_min.resize(free_indices.size());
    out_max.resize(free_indices.size());
    for (size_t j = 0; j < free_indices.size(); ++j) {
        out_min[j] = all_params[free_indices[j]].min;
        out_max[j] = all_params[free_indices[j]].max;
    }
}
```

---

## 4. `diode_objective.hpp` + `.cpp` — łącznik model+dane → ObjectiveFn

### 4.1 `diode_objective.hpp`

```cpp
// src/solver/diode_objective.hpp
#pragma once
#include "solver_types.hpp"
#include "nelder_mead.hpp"   // dla SANelderMead::ObjectiveFn

#include <functional>
#include <vector>

/// Buduje funkcję celu (SANelderMead::ObjectiveFn) wiążącą model IV z
/// konkretnym zestawem danych pomiarowych. Wynikowa funkcja przyjmuje
/// WYŁĄCZNIE wolne parametry i zwraca chi2.
///
/// all_params — pełna lista parametrów (free+fixed) w kolejności modelu;
///              MUSI być tym samym wektorem (ta sama kolejność) co przekazany
///              do konstruktora SANelderMead, inaczej mapowanie się rozjedzie.
/// model_fn   — np. [](double V, const double* p){ return evaluateDiodeIV4(V, p); }
/// V_data, I_meas, sigma — dane pomiarowe; muszą mieć tę samą długość
///
/// Rzuca std::invalid_argument gdy długości V_data/I_meas/sigma się różnią.
SANelderMead::ObjectiveFn makeDiodeIVObjective(
    std::vector<FitParam>                        all_params,
    std::function<double(double, const double*)> model_fn,
    std::vector<double>                          V_data,
    std::vector<double>                          I_meas,
    std::vector<double>                          sigma
);
```

### 4.2 `diode_objective.cpp`

```cpp
// src/solver/diode_objective.cpp
#include "diode_objective.hpp"
#include "param_utils.hpp"
#include "objective.hpp"

#include <stdexcept>

SANelderMead::ObjectiveFn makeDiodeIVObjective(
    std::vector<FitParam>                        all_params,
    std::function<double(double, const double*)> model_fn,
    std::vector<double>                          V_data,
    std::vector<double>                          I_meas,
    std::vector<double>                          sigma)
{
    if (V_data.size() != I_meas.size() || V_data.size() != sigma.size())
        throw std::invalid_argument(
            "makeDiodeIVObjective: V_data, I_meas, sigma muszą mieć tę samą długość");

    const auto free_idx = freeIndices(all_params);
    const int  N_points = static_cast<int>(V_data.size());

    // Capture-by-value: bezpieczne przy dowolnym czasie życia wynikowej
    // lambdy, kosztem kopii danych. Akceptowalne — projekt nie jest
    // ograniczony pamięciowo (patrz notatki Etapu 1).
    return [all_params, model_fn, V_data, I_meas, sigma, free_idx, N_points]
           (const std::vector<double>& free_values) -> double
    {
        const std::vector<double> full =
            buildFullParams(free_values, all_params, free_idx);
        return computeChiSquared(full.data(), V_data.data(), I_meas.data(),
                                  sigma.data(), N_points, model_fn);
    };
}
```

---

## 5. `nelder_mead.hpp` — interfejs klasy `SANelderMead`

```cpp
// src/solver/nelder_mead.hpp
#pragma once
#include "solver_types.hpp"
#include <functional>
#include <string>
#include <vector>

/// Rdzeń solvera Nelder-Mead (Etap 2 — bez SA).
///
/// Solver jest MODEL-AGNOSTYCZNY: nie wie nic o diodach ani IV. Przyjmuje
/// dowolną ObjectiveFn (free_values -> wartość skalarna do minimalizacji).
/// Powiązanie z konkretnym modelem fizycznym (np. evaluateDiodeIV4 + dane
/// pomiarowe) odbywa się NA ZEWNĄTRZ tej klasy — patrz diode_objective.hpp.
///
/// WAŻNE (Etap 3 preview — patrz "Decyzje architektoniczne" w planie):
/// gałąź "reflection odrzucona" w step() zawiera oznaczony komentarzem punkt,
/// w którym Etap 3 wstawi kryterium akceptacji Metropolis (SA). Przy T=0
/// (stan Etapu 2) zachowanie jest identyczne z klasycznym NM — to jest
/// CELOWA właściwość architektury, nie przypadek.
class SANelderMead {
public:
    /// free_values (len = N_free) -> wartość do minimalizacji.
    /// Kontrakt: NIGDY nie rzuca wyjątku; zwraca +∞ dla wejść niefizycznych/
    /// niepoprawnych (patrz computeChiSquared z Etapu 1 jako wzorzec).
    using ObjectiveFn = std::function<double(const std::vector<double>&)>;

    /// all_params      — WSZYSTKIE parametry (free+fixed) w stałej kolejności,
    ///                    TEJ SAMEJ co użyta przy budowie objective_fn
    /// objective_fn    — patrz wyżej; typowo budowane przez makeDiodeIVObjective()
    /// alpha,gamma,rho,sigma — współczynniki NM: reflection/expansion/contraction/shrink
    /// degenerate_tol  — próg detekcji degeneracji simpleksu (odległość euklidesowa);
    ///                    musi być wyraźnie mniejszy niż 5% typowego zakresu parametru,
    ///                    inaczej solver zacznie się od natychmiastowego restartu
    ///
    /// Rzuca std::invalid_argument gdy:
    ///   - brak wolnych parametrów (wszystkie free=false)
    ///   - dla któregoś wolnego parametru: min >= max
    ///   - dla któregoś wolnego parametru: value poza [min,max]
    ///   - degenerate_tol <= 0
    SANelderMead(
        std::vector<FitParam> all_params,
        ObjectiveFn           objective_fn,
        double alpha = 1.0,
        double gamma = 2.0,
        double rho   = 0.5,
        double sigma = 0.5,
        double degenerate_tol = 1e-12
    );

    /// Inicjalizuje simpleks (N_free+1 wierzchołków) wokół aktualnych wartości
    /// FitParam::value (z all_params) dla wolnych parametrów. Musi być
    /// wywołane raz, przed pierwszym step().
    void initSimplex();

    /// Wykonuje DOKŁADNIE jeden krok algorytmu. Zwraca typ wykonanej operacji.
    /// Zwiększa iteration o 1 — zawsze, niezależnie od typu operacji (w tym Restart).
    /// Rzuca std::logic_error gdy wywołane przed initSimplex().
    StepType step();

    /// Wykonuje step() w pętli aż do zbieżności (chi2_best < chi2_tol) lub
    /// osiągnięcia max_iter. Oba kryteria konfigurowalne per wywołanie.
    FitResult runUntilConvergence(int max_iter, double chi2_tol);

    // ─── Odczyt stanu ────────────────────────────────────────────────────
    const SimplexState& state()     const noexcept { return state_; }
    int                  iteration() const noexcept { return state_.iteration; }
    double bestChiSquared() const noexcept { return state_.chi2_values[state_.best_idx]; }

    /// TYLKO wolne parametry, w kolejności free_indices. Zgodne z FitResult::best_params.
    std::vector<double> bestParams() const { return state_.vertices[state_.best_idx]; }

    /// PEŁNY wektor parametrów (free+fixed), w kolejności all_params.
    /// Wygodne dla UI/logowania — FitResult::best_params zawiera tylko wolne.
    std::vector<double> bestFullParams() const;

private:
    std::vector<FitParam> all_params_;
    std::vector<int>      free_indices_;
    ObjectiveFn            objective_fn_;

    double alpha_, gamma_, rho_, sigma_;
    double degenerate_tol_;

    std::vector<double> free_min_;   // cache, len = N_free
    std::vector<double> free_max_;   // cache, len = N_free

    SimplexState state_;

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
};

// ─── Operacje geometryczne simpleksu — wolne funkcje ─────────────────────────
// Wydzielone z klasy celowo: testowalne w izolacji bez budowania całego solvera.

std::vector<double> reflectPoint(
    const std::vector<double>& centroid, const std::vector<double>& worst, double alpha);

std::vector<double> expandPoint(
    const std::vector<double>& centroid, const std::vector<double>& x_r, double gamma);

std::vector<double> contractPoint(
    const std::vector<double>& centroid, const std::vector<double>& worst, double rho);
```

---

## 6. `nelder_mead.cpp` — pełna implementacja

```cpp
// src/solver/nelder_mead.cpp
#include "nelder_mead.hpp"
#include "param_utils.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace {
    constexpr double kInf = std::numeric_limits<double>::infinity();
}

// ─── Operacje geometryczne ───────────────────────────────────────────────────

std::vector<double> reflectPoint(
    const std::vector<double>& centroid, const std::vector<double>& worst, double alpha)
{
    std::vector<double> result(centroid.size());
    for (size_t i = 0; i < centroid.size(); ++i)
        result[i] = centroid[i] + alpha * (centroid[i] - worst[i]);
    return result;
}

std::vector<double> expandPoint(
    const std::vector<double>& centroid, const std::vector<double>& x_r, double gamma)
{
    std::vector<double> result(centroid.size());
    for (size_t i = 0; i < centroid.size(); ++i)
        result[i] = centroid[i] + gamma * (x_r[i] - centroid[i]);
    return result;
}

std::vector<double> contractPoint(
    const std::vector<double>& centroid, const std::vector<double>& worst, double rho)
{
    std::vector<double> result(centroid.size());
    for (size_t i = 0; i < centroid.size(); ++i)
        result[i] = centroid[i] + rho * (worst[i] - centroid[i]);
    return result;
}

// ─── Konstruktor ─────────────────────────────────────────────────────────────

SANelderMead::SANelderMead(
    std::vector<FitParam> all_params,
    ObjectiveFn            objective_fn,
    double alpha, double gamma, double rho, double sigma,
    double degenerate_tol)
    : all_params_(std::move(all_params))
    , objective_fn_(std::move(objective_fn))
    , alpha_(alpha), gamma_(gamma), rho_(rho), sigma_(sigma)
    , degenerate_tol_(degenerate_tol)
{
    if (degenerate_tol_ <= 0.0)
        throw std::invalid_argument("SANelderMead: degenerate_tol musi być > 0");

    free_indices_ = freeIndices(all_params_);
    if (free_indices_.empty())
        throw std::invalid_argument("SANelderMead: brak wolnych parametrów (free=true)");

    extractFreeBounds(all_params_, free_indices_, free_min_, free_max_);

    for (size_t j = 0; j < free_indices_.size(); ++j) {
        const FitParam& p = all_params_[free_indices_[j]];
        if (!(p.min < p.max))
            throw std::invalid_argument(
                "SANelderMead: min >= max dla wolnego parametru '" + p.name + "'");
        if (p.value < p.min || p.value > p.max)
            throw std::invalid_argument(
                "SANelderMead: wartość startowa poza granicami dla parametru '" + p.name + "'");
    }
}

// ─── Inicjalizacja simpleksu ─────────────────────────────────────────────────

void SANelderMead::initSimplex()
{
    initSimplexAround(extractFreeValues(all_params_, free_indices_));
}

void SANelderMead::initSimplexAround(const std::vector<double>& start)
{
    const int N_free = static_cast<int>(free_indices_.size());

    state_.vertices.assign(N_free + 1, start);
    state_.chi2_values.assign(N_free + 1, 0.0);

    // Wierzchołek 0 = punkt startowy bez przesunięcia.
    // Wierzchołki 1..N_free = start z przesunięciem o 5% zakresu w JEDNYM wymiarze.
    for (int i = 1; i <= N_free; ++i) {
        const int dim = i - 1;
        const double range = free_max_[dim] - free_min_[dim];
        const double delta = 0.05 * range;

        double candidate = start[dim] + delta;
        if (candidate > free_max_[dim])
            candidate = start[dim] - delta;   // odbij, gdy +delta przekracza górną granicę

        state_.vertices[i][dim] = candidate;
        clipToBounds(state_.vertices[i]);     // siatka bezpieczeństwa
    }

    for (int i = 0; i <= N_free; ++i)
        state_.chi2_values[i] = evaluateVertex(state_.vertices[i]);

    int best, second_worst, worst;
    sortIndices(best, second_worst, worst);
    state_.best_idx  = best;
    state_.worst_idx = worst;
}

// ─── Krok algorytmu ──────────────────────────────────────────────────────────

StepType SANelderMead::step()
{
    if (state_.vertices.empty())
        throw std::logic_error("SANelderMead::step() wywołane przed initSimplex()");

    if (isDegenerate()) {
        restart();
        state_.iteration++;
        return StepType::Restart;
    }

    int best, second_worst, worst;
    sortIndices(best, second_worst, worst);

    const double f_best         = state_.chi2_values[best];
    const double f_second_worst = state_.chi2_values[second_worst];
    const double f_worst        = state_.chi2_values[worst];

    const std::vector<double> centroid = computeCentroidExcluding(worst);

    // ── Reflection ────────────────────────────────────────────────────────
    std::vector<double> x_r = reflectPoint(centroid, state_.vertices[worst], alpha_);
    clipToBounds(x_r);
    const double f_r = evaluateVertex(x_r);

    StepType applied;

    if (f_r < f_best) {
        // ── Spróbuj Expansion ───────────────────────────────────────────
        std::vector<double> x_e = expandPoint(centroid, x_r, gamma_);
        clipToBounds(x_e);
        const double f_e = evaluateVertex(x_e);

        if (f_e < f_r) {
            state_.vertices[worst]    = x_e;
            state_.chi2_values[worst] = f_e;
            applied = StepType::Expansion;
        } else {
            state_.vertices[worst]    = x_r;
            state_.chi2_values[worst] = f_r;
            applied = StepType::Reflection;
        }
    }
    else if (f_r < f_second_worst) {
        state_.vertices[worst]    = x_r;
        state_.chi2_values[worst] = f_r;
        applied = StepType::Reflection;
    }
    else {
        // f_r >= f_second_worst → reflection nie wystarczająco dobra do przyjęcia
        // wprost. Cała ta gałąź (zarówno f_second_worst<=f_r<f_worst jak i
        // f_r>=f_worst) prowadzi do TEJ SAMEJ formuły kontrakcji — patrz
        // sekcję "Decyzje architektoniczne" (C) w dokumencie planu Etapu 2.
        //
        // ETAP 3 HOOK: dokładnie w pod-przypadku f_r >= f_worst Etap 3 wstawi
        // tutaj próbę akceptacji Metropolis: P = exp(-(f_r-f_worst)/T_current),
        // PRZED wywołaniem kontrakcji poniżej. Gdy T_current==0 (stan Etapu 2)
        // → P==0 zawsze → kod kontrakcji/shrink poniżej bez zmian.

        std::vector<double> x_c = contractPoint(centroid, state_.vertices[worst], rho_);
        clipToBounds(x_c);
        const double f_c = evaluateVertex(x_c);

        if (f_c < f_worst) {
            state_.vertices[worst]    = x_c;
            state_.chi2_values[worst] = f_c;
            applied = StepType::Contraction;
        } else {
            shrinkSimplex(best);
            applied = StepType::Shrink;
        }
    }

    int new_best, new_second_worst, new_worst;
    sortIndices(new_best, new_second_worst, new_worst);
    state_.best_idx  = new_best;
    state_.worst_idx = new_worst;
    state_.iteration++;

    return applied;
}

// ─── Pętla zbieżności ────────────────────────────────────────────────────────

FitResult SANelderMead::runUntilConvergence(int max_iter, double chi2_tol)
{
    for (int it = 0; it < max_iter; ++it) {
        step();
        if (state_.chi2_values[state_.best_idx] < chi2_tol)
            return buildResult(true, "tol");
    }
    return buildResult(false, "max_iter");
}

FitResult SANelderMead::buildResult(bool converged, const std::string& stop_reason) const
{
    FitResult r;
    r.best_params = state_.vertices[state_.best_idx];
    r.chi2_min    = state_.chi2_values[state_.best_idx];
    r.delta_chi2  = 0.0;   // brak zewnętrznej referencji w Etapie 2 — patrz pułapki
    r.iterations  = state_.iteration;
    r.converged   = converged;
    r.stop_reason = stop_reason;
    return r;
}

std::vector<double> SANelderMead::bestFullParams() const
{
    return buildFullParams(bestParams(), all_params_, free_indices_);
}

// ─── Pomocnicze prywatne ──────────────────────────────────────────────────────

void SANelderMead::sortIndices(int& best, int& second_worst, int& worst) const
{
    const int n = static_cast<int>(state_.chi2_values.size());   // = N_free+1, zawsze >= 2
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [this](int a, int b) {
        return state_.chi2_values[a] < state_.chi2_values[b];
    });
    best         = order.front();
    worst        = order.back();
    second_worst = order[n - 2];   // bezpieczne: n>=2 (konstruktor wymaga N_free>=1)
}

std::vector<double> SANelderMead::computeCentroidExcluding(int exclude_idx) const
{
    const int N_free = static_cast<int>(free_indices_.size());
    std::vector<double> centroid(N_free, 0.0);
    int count = 0;
    for (size_t i = 0; i < state_.vertices.size(); ++i) {
        if (static_cast<int>(i) == exclude_idx) continue;
        for (int j = 0; j < N_free; ++j)
            centroid[j] += state_.vertices[i][j];
        ++count;
    }
    for (int j = 0; j < N_free; ++j)
        centroid[j] /= count;
    return centroid;
}

void SANelderMead::shrinkSimplex(int best_idx)
{
    const std::vector<double> best_vertex = state_.vertices[best_idx];   // kopia — best się nie zmienia
    for (size_t i = 0; i < state_.vertices.size(); ++i) {
        if (static_cast<int>(i) == best_idx) continue;
        for (size_t j = 0; j < state_.vertices[i].size(); ++j)
            state_.vertices[i][j] = best_vertex[j] + sigma_ * (state_.vertices[i][j] - best_vertex[j]);

        clipToBounds(state_.vertices[i]);
        state_.chi2_values[i] = evaluateVertex(state_.vertices[i]);
    }
}

bool SANelderMead::isDegenerate() const
{
    double max_dist_sq = 0.0;
    const size_t n = state_.vertices.size();
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            double d2 = 0.0;
            for (size_t k = 0; k < state_.vertices[i].size(); ++k) {
                const double d = state_.vertices[i][k] - state_.vertices[j][k];
                d2 += d * d;
            }
            max_dist_sq = std::max(max_dist_sq, d2);
        }
    }
    return std::sqrt(max_dist_sq) < degenerate_tol_;
}

void SANelderMead::restart()
{
    const std::vector<double> best_point = state_.vertices[state_.best_idx];
    initSimplexAround(best_point);
}

void SANelderMead::clipToBounds(std::vector<double>& v) const
{
    for (size_t i = 0; i < v.size(); ++i) {
        if (std::isnan(v[i])) continue;   // NaN zostaje — złapie je withinBounds()
        v[i] = std::clamp(v[i], free_min_[i], free_max_[i]);
    }
}

bool SANelderMead::withinBounds(const std::vector<double>& v) const
{
    for (size_t i = 0; i < v.size(); ++i) {
        if (std::isnan(v[i])) return false;
        if (v[i] < free_min_[i] || v[i] > free_max_[i]) return false;
    }
    return true;
}

double SANelderMead::evaluateVertex(const std::vector<double>& free_values) const
{
    // Defensywny double-check — patrz pułapki Etapu 1: po clipToBounds
    // powinno zawsze być true, ale NaN przechodzi przez std::clamp w
    // sposób niezdefiniowany (porównania z NaN zawsze false).
    if (!withinBounds(free_values))
        return kInf;
    return objective_fn_(free_values);
}
```

---

## 7. Weryfikacja i testy

Siedem funkcji testowych, od najprostszej (geometria) do najbardziej
złożonej (odtwarzanie parametrów IV). Każda mapuje się na konkretne kryterium
z `experiment-plan.md` — patrz tabela w sekcji 11.

### 7.1 Test geometrii simpleksu (jednostkowy, bez solvera)

```cpp
#include "../solver/nelder_mead.hpp"
#include <cassert>
#include <cstdio>

static void testSimplexGeometry()
{
    // centroid=(0,0), worst=(2,2)
    const std::vector<double> centroid = { 0.0, 0.0 };
    const std::vector<double> worst    = { 2.0, 2.0 };

    // Reflection: x_r = centroid + 1.0*(centroid-worst) = (-2,-2)
    const auto x_r = reflectPoint(centroid, worst, 1.0);
    assert(x_r[0] == -2.0 && x_r[1] == -2.0);

    // Expansion: x_e = centroid + 2.0*(x_r-centroid) = (-4,-4)
    const auto x_e = expandPoint(centroid, x_r, 2.0);
    assert(x_e[0] == -4.0 && x_e[1] == -4.0);

    // Contraction: x_c = centroid + 0.5*(worst-centroid) = (1,1)
    const auto x_c = contractPoint(centroid, worst, 0.5);
    assert(x_c[0] == 1.0 && x_c[1] == 1.0);

    fprintf(stdout, "[Simplex geometry] PASS\n");
}
```

### 7.2 Test minimalizacji funkcji kwadratowej

```cpp
#include "../solver/nelder_mead.hpp"
#include "../solver/solver_types.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

static void testQuadraticMinimization()
{
    // f(x) = Σ(xᵢ - cᵢ)², minimum w x = c, N = 5
    const std::vector<double> c = { 1.0, -2.0, 3.0, 0.5, -0.5 };
    const int N = static_cast<int>(c.size());

    std::vector<FitParam> params;
    for (int i = 0; i < N; ++i)
        params.push_back(FitParam(
            "x" + std::to_string(i), /*value*/ 0.0, /*min*/ -10.0, /*max*/ 10.0, true));

    SANelderMead::ObjectiveFn objective = [c](const std::vector<double>& x) -> double {
        double sum = 0.0;
        for (size_t i = 0; i < x.size(); ++i) {
            const double d = x[i] - c[i];
            sum += d * d;
        }
        return sum;
    };

    SANelderMead solver(params, objective);
    solver.initSimplex();

    const FitResult result = solver.runUntilConvergence(/*max_iter*/ 1000, /*chi2_tol*/ 1e-10);

    fprintf(stdout, "[Quadratic] chi2=%.3e  iter=%d  converged=%d\n",
            result.chi2_min, result.iterations, result.converged);

    assert(result.converged && "Quadratic test nie zbiegł w 1000 iteracji");
    assert(result.chi2_min < 1e-10);

    for (int i = 0; i < N; ++i) {
        const double err = std::abs(result.best_params[i] - c[i]);
        if (err > 1e-4) {
            fprintf(stderr, "[Quadratic FAIL] dim %d: got=%.6g exp=%.6g err=%.3e\n",
                    i, result.best_params[i], c[i], err);
            assert(false);
        }
    }

    fprintf(stdout, "[Quadratic] PASS\n");
}
```

### 7.3 Test odtwarzania parametrów IV

```cpp
#include "../solver/diode_model.hpp"
#include "../solver/diode_objective.hpp"
#include "../solver/nelder_mead.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

static void testIVParameterRecovery()
{
    // Parametry "prawdziwe" — fizycznie sensowne.
    const double true_params[] = { 1.2e-10, 1.45, 0.08, 1800.0 };  // I0,A,Rs,Rsh

    const int N_pts = 40;
    std::vector<double> V_data(N_pts), I_meas(N_pts), sigma(N_pts);
    for (int i = 0; i < N_pts; ++i) {
        V_data[i] = -0.4 + i * (1.0 / (N_pts - 1));   // -0.4V .. 0.6V
        I_meas[i] = evaluateDiodeIV4(V_data[i], true_params);
        sigma[i]  = 1e-9;
        assert(!std::isnan(I_meas[i]) && "Generacja danych syntetycznych dała NaN");
    }

    // Punkt startowy — WĄSKIE granice wokół true_params, nie pełny zakres
    // fizyczny. Powód: solver działa w skali liniowej (decyzja projektowa
    // ustalona w Etapie 1), więc 5% perturbacja na zakresie obejmującym
    // kilka rzędów wielkości (np. I0 ∈ [1e-13,1e-6]) byłaby nieproporcjonalna
    // do samej wartości I0. Patrz pułapki, pozycja 2.
    std::vector<FitParam> params = {
        FitParam("I0",  3.0e-10, 1e-11, 1e-8,   true),
        FitParam("A",   1.8,     0.8,   2.5,    true),
        FitParam("Rs",  0.3,     0.0,   1.0,    true),
        FitParam("Rsh", 800.0,   100.0, 5000.0, true),
    };

    auto model_fn = [](double V, const double* p) { return evaluateDiodeIV4(V, p); };
    SANelderMead::ObjectiveFn objective =
        makeDiodeIVObjective(params, model_fn, V_data, I_meas, sigma);

    SANelderMead solver(params, objective);
    solver.initSimplex();

    const FitResult result = solver.runUntilConvergence(/*max_iter*/ 5000, /*chi2_tol*/ 1e-18);

    fprintf(stdout, "[IV recovery] chi2=%.3e  iter=%d  stop=%s\n",
            result.chi2_min, result.iterations, result.stop_reason.c_str());

    static const char* names[] = { "I0", "A", "Rs", "Rsh" };
    bool all_ok = true;
    for (int i = 0; i < 4; ++i) {
        const double rel_err = std::abs(result.best_params[i] - true_params[i]) / std::abs(true_params[i]);
        fprintf(stdout, "  %-4s true=%.6e  fit=%.6e  rel_err=%.4f%%\n",
                names[i], true_params[i], result.best_params[i], rel_err * 100.0);
        if (rel_err >= 0.01) all_ok = false;
    }

    assert(all_ok && "Odtworzenie parametrów IV: co najmniej jeden parametr poza 1% tolerancji");
    fprintf(stdout, "[IV recovery] PASS\n");
}
```

### 7.4 Testy odporności na NaN i wyjście poza granice

```cpp
#include "../solver/nelder_mead.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

static void testNaNAndBoundsRobustness()
{
    // Funkcja celu z "dziurą": dla x[0]<0 zwraca +inf (symuluje NaN z modelu IV).
    // Minimum faktyczne w x[0]=1, poza dziurą.
    SANelderMead::ObjectiveFn objective = [](const std::vector<double>& x) -> double {
        if (x[0] < 0.0) return std::numeric_limits<double>::infinity();
        const double d = x[0] - 1.0;
        return d * d;
    };

    std::vector<FitParam> params = { FitParam("x0", -5.0, -10.0, 10.0, true) };  // start w dziurze

    SANelderMead solver(params, objective);
    solver.initSimplex();   // pierwszy wierzchołek ma chi2=+inf

    const FitResult result = solver.runUntilConvergence(2000, 1e-12);

    fprintf(stdout, "[NaN robustness] chi2=%.3e  best_x=%.6f  iter=%d\n",
            result.chi2_min, result.best_params[0], result.iterations);

    assert(std::isfinite(result.chi2_min) && "Solver utknął na +inf — nie wyszedł z niepoprawnego obszaru");
    assert(std::abs(result.best_params[0] - 1.0) < 1e-3);

    fprintf(stdout, "[NaN robustness] PASS\n");
}

static void testAllVerticesInvalidAtInit()
{
    // Skrajny przypadek: WSZYSTKIE wierzchołki początkowe nieważne (+inf).
    // Solver MUSI zatrzymać się po max_iter — bez crasha, bez nieskończonej pętli.
    SANelderMead::ObjectiveFn always_inf = [](const std::vector<double>&) {
        return std::numeric_limits<double>::infinity();
    };

    std::vector<FitParam> params = { FitParam("x0", 0.0, -1.0, 1.0, true) };
    SANelderMead solver(params, always_inf);
    solver.initSimplex();

    const FitResult result = solver.runUntilConvergence(100, 1e-12);

    assert(result.stop_reason == "max_iter");
    assert(result.iterations == 100);
    assert(std::isinf(result.chi2_min));

    fprintf(stdout, "[All-invalid edge case] PASS (brak crasha, brak nieskończonej pętli)\n");
}
```

### 7.5 Test degeneracji i restartu

```cpp
#include "../solver/nelder_mead.hpp"
#include <cassert>
#include <cstdio>

static void testDegenerateRestart()
{
    // Konfiguracja CELOWO wymuszająca degenerację: degenerate_tol duży
    // względem skali problemu, chi2_tol praktycznie nieosiągalny → simpleks
    // zbiega do bardzo małego rozmiaru i wyzwala restart, zanim chi2 spadnie
    // do chi2_tol. To gwarantuje co najmniej jeden StepType::Restart.
    std::vector<FitParam> params = { FitParam("x0", 1.0, -5.0, 5.0, true) };

    SANelderMead::ObjectiveFn obj = [](const std::vector<double>& x) {
        return x[0] * x[0];   // minimum w x=0
    };

    // degenerate_tol=1e-2 — duży względem [-5,5] i względem precyzji, do
    // jakiej NM normalnie zbiega
    SANelderMead solver(params, obj, /*alpha*/1.0, /*gamma*/2.0, /*rho*/0.5, /*sigma*/0.5,
                         /*degenerate_tol*/1e-2);
    solver.initSimplex();

    int restart_count = 0;
    const int kSteps = 500;
    for (int i = 0; i < kSteps; ++i)
        if (solver.step() == StepType::Restart) ++restart_count;

    fprintf(stdout, "[Degenerate restart] restart_count=%d / %d steps, iteration=%d\n",
            restart_count, kSteps, solver.iteration());

    assert(restart_count > 0 && "Degenerate restart nigdy się nie wyzwolił — sprawdź isDegenerate()");
    assert(solver.iteration() == kSteps && "Każde step() musi zwiększać iteration o 1, w tym Restart");

    fprintf(stdout, "[Degenerate restart] PASS\n");
}
```

### 7.6 Test determinizmu

```cpp
#include "../solver/nelder_mead.hpp"
#include <cassert>
#include <cstdio>
#include <string>

static void testDeterminism()
{
    auto buildAndRun = []() -> FitResult {
        const std::vector<double> c = { 2.0, -1.0, 0.0 };
        std::vector<FitParam> params;
        for (int i = 0; i < 3; ++i)
            params.push_back(FitParam("x" + std::to_string(i), 5.0, -10.0, 10.0, true));

        SANelderMead::ObjectiveFn obj = [c](const std::vector<double>& x) {
            double s = 0.0;
            for (size_t i = 0; i < x.size(); ++i) { const double d = x[i]-c[i]; s += d*d; }
            return s;
        };

        SANelderMead solver(params, obj);
        solver.initSimplex();
        return solver.runUntilConvergence(500, 1e-12);
    };

    const FitResult r1 = buildAndRun();
    const FitResult r2 = buildAndRun();

    assert(r1.iterations == r2.iterations);
    assert(r1.chi2_min == r2.chi2_min);   // bit-identyczne — brak losowości w Etapie 2
    for (size_t i = 0; i < r1.best_params.size(); ++i)
        assert(r1.best_params[i] == r2.best_params[i]);

    fprintf(stdout, "[Determinism] PASS (wyniki bit-identyczne w 2 uruchomieniach)\n");
}
```

---

## 8. Wiring testów — refaktoryzacja `app.cpp`

Etap 1 dodał 4 funkcje testowe bezpośrednio w `app.cpp`. Etap 2 dokłada
kolejnych 7 — czas je rozdzielić, zanim `app.cpp` zamieni się w nieczytelny
plik testowy.

### 8.1 `src/tests/test_stage1.hpp`

```cpp
#pragma once
void runStage1Tests();
```

`src/tests/test_stage1.cpp` — przenieś tu dosłownie 4 funkcje z Etapu 1
(`testLambertW`, `testDiodeIV4Physics`, `testChiSquaredZero`, `testEdgeCases`,
wraz z ich include'ami), opakuj w:

```cpp
void runStage1Tests()
{
    fprintf(stdout, "\n=== Stage 1 Self-Tests ===\n");
    testLambertW();
    testDiodeIV4Physics();
    testChiSquaredZero();
    testEdgeCases();
    fprintf(stdout, "=== Stage 1 PASS ===\n\n");
}
```

### 8.2 `src/tests/test_stage2.hpp`

```cpp
#pragma once
void runStage2Tests();
```

### 8.3 `src/tests/test_stage2.cpp`

```cpp
#include "test_stage2.hpp"
#include "../solver/nelder_mead.hpp"
#include "../solver/diode_model.hpp"
#include "../solver/diode_objective.hpp"
#include "../solver/solver_types.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace {
    // ── tu wklej WSZYSTKIE 7 funkcji statycznych z sekcji 7 ──
    // testSimplexGeometry, testQuadraticMinimization, testIVParameterRecovery,
    // testNaNAndBoundsRobustness, testAllVerticesInvalidAtInit,
    // testDegenerateRestart, testDeterminism
}

void runStage2Tests()
{
    fprintf(stdout, "\n=== Stage 2 Self-Tests ===\n");
    testSimplexGeometry();
    testQuadraticMinimization();
    testIVParameterRecovery();
    testNaNAndBoundsRobustness();
    testAllVerticesInvalidAtInit();
    testDegenerateRestart();
    testDeterminism();
    fprintf(stdout, "=== Stage 2 PASS ===\n\n");
}
```

### 8.4 Zaktualizowany `app.cpp`

```cpp
#include "app.hpp"

#ifndef NDEBUG
#include "tests/test_stage1.hpp"
#include "tests/test_stage2.hpp"
#endif

void appInit(AppState& appState)
{
#ifndef NDEBUG
    runStage1Tests();
    runStage2Tests();
#endif
}
```

---

## 9. CMakeLists.txt — uzupełnienie

```cmake
# ─── Solver core — Etap 1 + Etap 2 ───────────────────────────────────────────
# Rekomendacja: zmień nazwę targetu solver_stage1 -> solver_core, żeby nie
# wiązać nazwy CMake z numerem etapu (target będzie rósł dalej w Etapach 3-5).
add_library(solver_core STATIC
    src/solver/diode_model.cpp        # Etap 1
    src/solver/objective.cpp          # Etap 1
    src/solver/param_utils.cpp        # Etap 2 — NOWE
    src/solver/diode_objective.cpp    # Etap 2 — NOWE
    src/solver/nelder_mead.cpp        # Etap 2 — NOWE
)
target_include_directories(solver_core PUBLIC src/solver/)
target_link_libraries(solver_core PUBLIC lambertw_vendor)
target_compile_options(solver_core PRIVATE -Wall -Wextra -std=c++17)

# ─── Testy ────────────────────────────────────────────────────────────────────
add_library(solver_tests STATIC
    src/tests/test_stage1.cpp
    src/tests/test_stage2.cpp
)
target_link_libraries(solver_tests PRIVATE solver_core)
target_compile_options(solver_tests PRIVATE -Wall -Wextra -std=c++17)

# ─── Główna aplikacja ─────────────────────────────────────────────────────────
target_link_libraries(gpu-experiment
    PRIVATE solver_core
    PRIVATE solver_tests   # tylko jeśli appInit() je wywołuje (debug build)
    # ... pozostałe: imgui, implot, imnodes, glfw, glad
)
```

---

## 10. Kolejność implementacji (szacunek czasowy)

```
Blok 1 — Mapowanie parametrów (45m):
  [25m]  param_utils.hpp/cpp     — 4 funkcje, kompiluje się
  [20m]  ręczny test: freeIndices + buildFullParams na prostym przykładzie

Blok 2 — Glue dla modelu IV (30m):
  [20m]  diode_objective.hpp/cpp
  [10m]  ręczna weryfikacja: objective(true_params_jako_free) == 0

Blok 3 — Rdzeń NM (3h):
  [25m]  nelder_mead.hpp          — pełny interfejs + doc-comments
  [15m]  reflectPoint/expandPoint/contractPoint — wolne funkcje
  [20m]  konstruktor + walidacja
  [30m]  initSimplex/initSimplexAround
  [50m]  step()                  — najważniejsza i najbardziej złożona funkcja
  [25m]  shrinkSimplex/isDegenerate/restart
  [15m]  clipToBounds/withinBounds/evaluateVertex
  [10m]  runUntilConvergence/buildResult/bestFullParams

Blok 4 — Testy (2h):
  [10m]  testSimplexGeometry
  [20m]  testQuadraticMinimization
  [35m]  testIVParameterRecovery  — w tym dobór realistycznych granic
  [20m]  testNaNAndBoundsRobustness + testAllVerticesInvalidAtInit
  [15m]  testDeterminism
  [20m]  testDegenerateRestart

Blok 5 — Integracja (30m):
  [15m]  refaktoryzacja testów Etapu 1 do test_stage1.hpp/cpp
  [15m]  wiring w app.cpp, weryfikacja całości, uruchomienie aplikacji

Łącznie: ~6h45min netto
```

---

## 11. Checklist ukończenia Etapu 2

| #   | Wymaganie (`experiment-plan.md`)                   | Plik / funkcja                                   | Weryfikacja                                                      |
| --- | -------------------------------------------------- | ------------------------------------------------ | ---------------------------------------------------------------- |
| 1   | `initSimplex` — N+1 wierzchołków, perturbacja 5%   | `nelder_mead.cpp::initSimplexAround`             | testQuadraticMinimization                                        |
| 2   | Wierzchołki w granicach po inicjalizacji           | `initSimplexAround` + `clipToBounds`             | manualnie / assert w testach                                     |
| 3   | chi2 dla każdego wierzchołka przy inicjalizacji    | `initSimplexAround` (pętla `evaluateVertex`)     | —                                                                |
| 4   | Sortowanie + identyfikacja best/second_worst/worst | `sortIndices()`                                  | testDegenerateRestart pośrednio                                  |
| 5   | Centroid (N_free najlepszych, bez worst)           | `computeCentroidExcluding()`                     | —                                                                |
| 6   | Reflection                                         | `reflectPoint()` + `step()`                      | testSimplexGeometry, testQuadraticMinimization                   |
| 7   | Expansion (gdy `f(x_r)<f(best)`)                   | `expandPoint()` + `step()`                       | testSimplexGeometry                                              |
| 8   | Contraction (gdy reflection odrzucona)             | `contractPoint()` + `step()`                     | testSimplexGeometry                                              |
| 9   | Shrink (gdy contraction nie poprawia)              | `shrinkSimplex()` + `step()`                     | testDegenerateRestart (wymusza dużo shrinków)                    |
| 10  | Współczynniki konfigurowalne α,γ,ρ,σ               | konstruktor `SANelderMead`                       | domyślne 1.0/2.0/0.5/0.5                                         |
| 11  | Clipping do `[min,max]`                            | `clipToBounds()`                                 | —                                                                |
| 12  | Penalizacja poza granicą → `+∞`                    | `withinBounds()` + `evaluateVertex()`            | testNaNAndBoundsRobustness                                       |
| 13  | NaN z modelu → `+∞`                                | (Etap 1: `computeChiSquared`) + `evaluateVertex` | testNaNAndBoundsRobustness, testAllVerticesInvalidAtInit         |
| 14  | Brak crasha / brak nieskończonej pętli             | `runUntilConvergence` (ograniczone `max_iter`)   | testAllVerticesInvalidAtInit                                     |
| 15  | Detekcja degeneracji (`max_distance<tol`)          | `isDegenerate()`                                 | testDegenerateRestart                                            |
| 16  | Restart z perturbacją `best_point`                 | `restart()`                                      | testDegenerateRestart                                            |
| 17  | `StepType::Restart` zwracany                       | `step()`                                         | testDegenerateRestart                                            |
| 18  | `chi2_best < chi2_tol` — zbieżność                 | `runUntilConvergence`                            | testQuadraticMinimization                                        |
| 19  | `iteration >= max_iter` — limit                    | `runUntilConvergence`                            | testAllVerticesInvalidAtInit                                     |
| 20  | Oba kryteria zatrzymania konfigurowalne            | `runUntilConvergence(max_iter, chi2_tol)`        | —                                                                |
| 21  | `f(x)=Σ(xᵢ-cᵢ)² < 1e-10` w `<1000` iter, N≤5       | —                                                | **testQuadraticMinimization**                                    |
| 22  | Odtworzenie parametrów IV w `<1%`                  | —                                                | **testIVParameterRecovery**                                      |
| 23  | NaN/poza granicami nie crashuje                    | —                                                | **testNaNAndBoundsRobustness**, **testAllVerticesInvalidAtInit** |
| 24  | Deterministyczność (2 runy = identyczne)           | brak RNG w Etapie 2                              | **testDeterminism**                                              |

**Etap 2 gotowy = wszystkie 24 pozycje ✓ → można zacząć Etap 3 (SA extension).**

---

## 12. Typowe pułapki

| Problem                                                              | Przyczyna                                                                | Rozwiązanie                                                                                                                                           |
| -------------------------------------------------------------------- | ------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| `second_worst == best` dla N_free=1                                  | tylko 2 wierzchołki istnieją                                             | Nieszkodliwe — formuła `order[n-2]` naturalnie daje `best`; algorytm degraduje się sensownie do 2-gałęziowej wersji (patrz komentarz w `sortIndices`) |
| `testIVParameterRecovery` nie zbiega w 1%                            | zbyt szerokie granice → 5% perturbacja nieproporcjonalna do skali I0/Rsh | Test używa WĄSKICH, realistycznych granic wokół `true_params`, nie pełnego zakresu fizycznego — patrz sekcja 7.3                                      |
| `degenerate_tol` źle dobrany względem skali parametru                | mieszane skale (I0~1e-10 vs Rsh~1e3) z jednym globalnym progiem          | Dobieraj `degenerate_tol` do konkretnej skali problemu w testach; pełne rozwiązanie (np. tolerancja względna) poza zakresem Etapu 2                   |
| `best_idx`/`worst_idx` "nieaktualne" między krokami                  | `sortIndices` wywoływane tylko na początku `step()`                      | `step()` odświeża `best_idx`/`worst_idx` TAKŻE na końcu, po mutacji                                                                                   |
| NaN przechodzi przez `std::clamp` w nieokreślony sposób              | UB dla porównań z NaN                                                    | Explicit `isnan` check PRZED clamp w `clipToBounds`, plus redundantny `withinBounds` po clipie                                                        |
| "Nieskończona pętla" zbiegnij→zdegeneruj→restart                     | mały `degenerate_tol` względem nieosiągalnego `chi2_tol`                 | To NIE jest błąd — `max_iter` zawsze ogranicza pętlę; to oczekiwane zachowanie, świadomie wykorzystane w `testDegenerateRestart`                      |
| Pomylenie progu `second_worst` vs `worst` przy implementacji Etapu 3 | dwie różne konwencje literaturowe NM                                     | Patrz "Decyzje architektoniczne" (C) — hook SA wchodzi WEWNĄTRZ gałęzi else, przy węższym warunku `f_r>=f_worst`, nie zastępuje progu `second_worst`  |
| Lambda w `makeDiodeIVObjective` kopiuje dane przez wartość           | bezpieczeństwo cyklu życia > mikrooptymalizacja                          | Akceptowalne — projekt nie jest ograniczony pamięciowo (patrz notatki Etapu 1)                                                                        |
| Solver rzuca przy konstrukcji zamiast zwracać NaN/+∞                 | pomylenie dwóch kategorii błędów                                         | To zamierzone — patrz "Decyzje architektoniczne" (D): konfiguracja = wyjątek, dane w trakcie pracy = `+∞`                                             |