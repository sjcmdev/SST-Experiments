# Plan Implementacji — Etap 3 (Szczegółowy)
## SA Extension + harmonogramy chłodzenia

> **Założenie (zgodnie z poleceniem):** Etap 1 i Etap 2 zakończyły się
> dokładnie tak, jak zaprojektowano w poprzednich planach. Wszystkie pliki,
> sygnatury i zachowania opisane tam (`solver_types.hpp`, `lambertw.hpp`,
> `diode_model.hpp/cpp`, `objective.hpp/cpp`, `param_utils.hpp/cpp`,
> `diode_objective.hpp/cpp`, `nelder_mead.hpp/cpp` w wersji z Etapu 2) są
> traktowane jako dane wejściowe tego etapu.
>
> **Cel:** Integracja Simulated Annealing jako modyfikacji kryterium
> akceptacji. Temperatura kontrolowana w runtime. Weryfikacja na przypadku
> z lokalnym minimum.

---

## 1. Pliki — co się zmienia, co zostaje bez zmian

```
src/solver/
├── phys_const.hpp              ← BEZ ZMIAN
├── solver_types.hpp            ← BEZ ZMIAN — SAConfig i CoolingSchedule już
│                                   istnieją z Etapu 1 i pasują 1:1 (sekcja 3)
├── lambertw.hpp                ← BEZ ZMIAN
├── diode_model.hpp / .cpp      ← BEZ ZMIAN
├── objective.hpp / .cpp        ← BEZ ZMIAN
├── param_utils.hpp / .cpp      ← BEZ ZMIAN
├── diode_objective.hpp / .cpp  ← BEZ ZMIAN
│
└── nelder_mead.hpp / .cpp      ← MODYFIKOWANE — jedyne pliki tego etapu

src/tests/
├── test_stage1.hpp / .cpp      ← BEZ ZMIAN
├── test_stage2.hpp / .cpp      ← BEZ ZMIAN
└── test_stage3.hpp / .cpp      ← NOWY — sekcja 7
```

To jest rzadki przypadek w tym projekcie: **żaden nowy plik produkcyjny**.
Powód jest zarazem decyzją architektoniczną zapisaną już w notatkach projektu:
*"SA jest zintegrowane z NM, nie jest osobne — stochastyczna akceptacja
działa na każdym kroku simpleksu; to jeden algorytm"*. SA nie jest osobną
klasą czy strategią doczepianą z zewnątrz — jest rozszerzeniem `step()`
dokładnie w punkcie, który Etap 2 świadomie zostawił oznaczony jako
`ETAP 3 HOOK`.

---

## 2. Decyzje architektoniczne Etapu 3

Sześć decyzji, które determinują kształt tego etapu — każda wynika wprost
z napięcia między literą `experiment-plan.md` a koniecznością zachowania
100% kompatybilności wstecznej z Etapem 2.

### A. Jeden konstruktor, nowe parametry **na końcu** listy

Etap 2 ma w swoich testach wywołania pozycyjne sięgające aż do
`degenerate_tol`, np.:

```cpp
SANelderMead solver(params, obj, /*alpha*/1.0, /*gamma*/2.0, /*rho*/0.5,
                     /*sigma*/0.5, /*degenerate_tol*/1e-2);
```

Gdyby nowe parametry SA trafiły gdziekolwiek **przed** `alpha`, ten kod
przestałby się kompilować (przesunięcie pozycji) albo gorzej — skompilowałby
się z błędnym typem niejawnie skonwertowanym. Dlatego `sa_enabled`,
`sa_config`, `rng_seed` są dopisane **za** `degenerate_tol`, wszystkie
z domyślnymi wartościami. Każde wywołanie konstruktora z Etapu 2 — z dowolną
liczbą jawnych argumentów — kompiluje się i działa **bez żadnej zmiany**.

Druga pułapka: domyślny `SAConfig{}` ma `T_initial=1.0` (Etap 1). Gdyby
sam fakt podania `SAConfig` włączał SA, każdy, kto przez pomyłkę poda pusty
`SAConfig{}`, dostałby losowość tam, gdzie się jej nie spodziewa. Dlatego
jest osobna, jawna flaga `bool sa_enabled = false` — SA jest wyłączone
**niezależnie** od tego, co zawiera `sa_config`, dopóki ktoś świadomie nie
ustawi `sa_enabled=true`.

Dla wygody dochodzi też `static SANelderMead withSA(...)` — fabryka
pokrywająca najczęstszy przypadek (SA z domyślnymi współczynnikami NM), żeby
testy i kod użytkownika nie musiały wypisywać `1.0,2.0,0.5,0.5,1e-12` za
każdym razem.

### B. Licznik chłodzenia `k`: start od 0, inkrementacja PRZED obliczeniem T

`T_k = T_initial / ln(1+k)` ma osobliwość przy `k=0` (`ln(1)=0` →
dzielenie przez zero). Rozwiązanie: `cooling_k_` startuje od `0`
(ustawiane przez `resetCooling()`), ale jest **inkrementowane przed**
obliczeniem nowej temperatury — pierwsza aktualizacja po pierwszym kroku
zawsze używa `k=1`, nigdy `k=0`. Ta sama konwencja obowiązuje też dla
Geometric (dla spójności, nie dlatego, że Geometric tego wymaga — `T_initial
· rate⁰` byłoby tu poprawne matematycznie, ale używanie dwóch różnych
konwencji dla dwóch harmonogramów byłoby źródłem pomyłek).

Konsekwencja uboczna dla Boltzmann: `T_1 = T_initial/ln(2) ≈ 1.443·T_initial`
— temperatura **chwilowo rośnie** powyżej `T_initial` po pierwszym kroku,
zanim zacznie maleć od `k=2` w dół. To akceptowalna właściwość tego
konkretnego wzoru, nie błąd — testy (sekcja 6) sprawdzają monotoniczność
sekwencji `T_1..T_100`, nie porównują jej do `T_initial` jako punktu
odniesienia.

### C. RNG całkowicie pomijany, gdy `T_current <= 0`

```cpp
if (sa_enabled_ && f_r >= f_worst && sa_config_.T_current > 0.0) {
    ...
    const double roll = uniform_dist_(rng_);   // RNG dotykany TYLKO tutaj
    ...
}
```

Warunek `T_current > 0.0` jest sprawdzany **przed** wywołaniem `rng_()`. To
nie jest mikrooptymalizacja — to gwarancja, że gdy temperatura wynosi zero,
generator liczb losowych jest w ogóle nietknięty, więc zachowanie jest
deterministyczne **z definicji kodu**, a nie dlatego, że akurat
`exp(-Δf/0)` matematycznie wychodzi zero. Dzięki temu kryterium ukończenia
*"przy T=0: zachowanie identyczne jak czysty NM"* i *"setTemperature(0)
podczas działania → kolejne kroki deterministyczne"* są spełnione przez
konstrukcję, nie przez przypadek numeryczny.

### D. Korekta: "monotonicznie nierosnąca", nie "niemalejąca"

`experiment-plan.md` w kryteriach ukończenia mówi: *"Boltzmann: temperatura
jest monotonicznie **niemalejąca**"*. To sprzeczne z samym wzorem —
`T_initial / ln(1+k)` jest **malejące** dla `k≥1` (`ln(1+k)` rośnie, więc
odwrotność maleje), co zresztą jest jedynym sensownym zachowaniem dla
harmonogramu **chłodzenia**. Przyjmuję to za literówkę w dokumencie
źródłowym (powinno być "nierosnąca" — non-increasing) i tak właśnie
implementuję oraz testuję (sekcja 6.4). Warto to zweryfikować z dokumentem
źródłowym, jeśli ma on jeszcze inną, nieoczywistą interpretację.

### E. Akceptacja SA gorszego punktu → nadal `StepType::Reflection`

`StepType` (Etap 1) ma pięć wartości: `Reflection, Expansion, Contraction,
Shrink, Restart` — brak dedykowanej wartości dla "SA zaakceptowało gorszy
punkt". Skoro operacja geometrycznie WCIĄŻ JEST odbiciem (`x_r`), tylko
zaakceptowanym przez inne kryterium niż zwykłe NM, zwracam
`StepType::Reflection` również w tym przypadku. Dodatkowo licznik
`getSAAcceptedCount()` pozwala odróżnić "ile razy reflection zostało
zaakceptowane mimo bycia gorszym" bez rozszerzania enuma.

To ma znaczenie dla Etapu 4: dokument wspomina, że
`state_after.chi2_values[best_idx]` może (wyjątkowo) pogorszyć się
"gdy SA zaakceptował gorszy punkt". W tej implementacji to **nie powinno
się zdarzyć** — SA podmienia wyłącznie slot `worst`, nigdy `best`, więc
`chi2_best` pozostaje monotonicznie nierosnące nawet przez akceptacje SA.
Etap 4 może to potraktować jako potwierdzony niezmiennik, a nie coś, co
trzeba dodatkowo obsługiwać.

### F. `setTemperature`/`resetCooling`/`setCoolingSchedule` rzucają, gdy SA wyłączone

Wywołanie kontroli temperatury na solverze zbudowanym z `sa_enabled=false`
prawie na pewno jest pomyłką programisty (nie ma czego kontrolować — SA nie
działa). Zgodnie z konwencją Etapu 2 ("błąd konfiguracji → wyjątek, błąd
danych w trakcie pracy → `+∞`"), te trzy metody rzucają
`std::logic_error`, zamiast cicho nic nie robić.

---

## 3. `solver_types.hpp` — potwierdzenie zgodności (zero zmian)

Stage 1 zdefiniował te struktury "na zapas" — dokładnie po to, by Etap 3
nie musiał ich dotykać. Weryfikacja, że pasują 1:1 do potrzeb tego etapu:

```cpp
enum class CoolingSchedule { Boltzmann, Geometric, Adaptive };   // ✓ dokładnie 3 harmonogramy z doc

struct SAConfig {
    double          T_initial      = 1.0;   // ✓
    double          T_current      = 1.0;   // ✓ (nadpisywane przez resetCooling() w konstruktorze)
    CoolingSchedule schedule       = CoolingSchedule::Boltzmann;  // ✓
    double          geometric_rate = 0.99;  // ✓
};
```

`SimplexState::T_current` (też z Etapu 1, dotąd nieużywane pole) zaczyna
być realnie wypełniane w `step()` — patrz sekcja 5.

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

/// Rdzeń solvera Nelder-Mead z opcjonalnym rozszerzeniem SA (Etap 3).
///
/// Solver jest MODEL-AGNOSTYCZNY — patrz Etap 2. Etap 3 dodaje:
///   - kryterium akceptacji Metropolis dla wierzchołków gorszych niż worst
///   - trzy harmonogramy chłodzenia (Boltzmann/Geometric/Adaptive)
///   - runtime'ową kontrolę temperatury (setTemperature/resetCooling)
///
/// WSTECZNA KOMPATYBILNOŚĆ Z ETAPEM 2: wszystkie nowe parametry konstruktora
/// są DOPISANE NA KOŃCU listy z domyślną wartością sa_enabled=false. Każde
/// wywołanie konstruktora z Etapu 2 (z dowolną liczbą jawnych argumentów do
/// degenerate_tol włącznie) kompiluje się i zachowuje się IDENTYCZNIE jak
/// poprzednio.
class SANelderMead {
public:
    using ObjectiveFn = std::function<double(const std::vector<double>&)>;

    /// Pełny konstruktor. Patrz "Decyzje architektoniczne" (A) w dokumencie planu.
    ///
    /// sa_enabled — czy mechanizm SA jest aktywny (domyślnie NIE — czysty NM)
    /// sa_config  — używane TYLKO gdy sa_enabled=true; T_current jest
    ///              WYMUSZANE na T_initial przy konstrukcji (patrz resetCooling)
    /// rng_seed   — seed dla std::mt19937; używany TYLKO gdy sa_enabled=true
    ///
    /// Dodatkowe wyjątki (ponad Etap 2) gdy sa_enabled=true:
    ///   - sa_config.T_initial < 0
    ///   - sa_config.schedule==Geometric i geometric_rate poza (0,1)
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
        unsigned int rng_seed = 0
    );

    /// Wygodny konstruktor: SA włączone, domyślne współczynniki NM
    /// (α=1, γ=2, ρ=0.5, σ=0.5, degenerate_tol=1e-12).
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

    // ─── Etap 3: kontrola temperatury (runtime) ────────────────────────────
    /// Modyfikuje T_current. NIE resetuje k ani T_initial.
    /// Rzuca std::logic_error gdy sa_enabled=false.
    void setTemperature(double T);

    /// Przywraca T_current=T_initial i k=0.
    /// Rzuca std::logic_error gdy sa_enabled=false.
    void resetCooling();

    /// Zmienia harmonogram — efektywne od NASTĘPNEGO kroku, bez restartu
    /// simpleksu. Rzuca std::logic_error gdy sa_enabled=false.
    void setCoolingSchedule(CoolingSchedule schedule);

    // ─── Etap 3: odczyt stanu SA ────────────────────────────────────────────
    double          getTemperature()     const noexcept { return sa_config_.T_current; }
    const SAConfig& getSAConfig()        const noexcept { return sa_config_; }
    bool            isSAEnabled()        const noexcept { return sa_enabled_; }
    int             getSAAcceptedCount() const noexcept { return sa_accepted_count_; }

private:
    std::vector<FitParam> all_params_;
    std::vector<int>      free_indices_;
    ObjectiveFn            objective_fn_;

    double alpha_, gamma_, rho_, sigma_;
    double degenerate_tol_;

    std::vector<double> free_min_;
    std::vector<double> free_max_;

    SimplexState state_;

    // ─── Etap 3: stan SA ────────────────────────────────────────────────────
    bool         sa_enabled_;
    SAConfig     sa_config_;
    int          cooling_k_ = 0;
    std::mt19937 rng_;
    std::uniform_real_distribution<double> uniform_dist_{0.0, 1.0};
    int          sa_accepted_count_ = 0;

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

    // ─── Etap 3: harmonogramy chłodzenia ────────────────────────────────────
    double computeBoltzmannTemperature(int k) const;
    double computeGeometricTemperature(int k) const;
    void   updateCooling();
};

std::vector<double> reflectPoint(
    const std::vector<double>& centroid, const std::vector<double>& worst, double alpha);
std::vector<double> expandPoint(
    const std::vector<double>& centroid, const std::vector<double>& x_r, double gamma);
std::vector<double> contractPoint(
    const std::vector<double>& centroid, const std::vector<double>& worst, double rho);
```

---

## 5. `nelder_mead.cpp` — zaktualizowana implementacja

Sekcje oznaczone *(Etap 2, bez zmian)* są wklejone dosłownie z poprzedniego
etapu — pokazane w całości, żeby plik był od razu kompilowalny i
copy-paste-ready, bez konieczności ręcznego scalania z poprzednią wersją.

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

// ─── Operacje geometryczne (Etap 2, bez zmian) ───────────────────────────────

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

// ─── Konstruktor (ROZSZERZONY o walidację i inicjalizację SA) ───────────────

SANelderMead::SANelderMead(
    std::vector<FitParam> all_params,
    ObjectiveFn            objective_fn,
    double alpha, double gamma, double rho, double sigma,
    double degenerate_tol,
    bool sa_enabled, SAConfig sa_config, unsigned int rng_seed)
    : all_params_(std::move(all_params))
    , objective_fn_(std::move(objective_fn))
    , alpha_(alpha), gamma_(gamma), rho_(rho), sigma_(sigma)
    , degenerate_tol_(degenerate_tol)
    , sa_enabled_(sa_enabled)
    , sa_config_(sa_config)
    , rng_(rng_seed)
{
    // ── Walidacja Etapu 2 (bez zmian) ───────────────────────────────────────
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

    // ── Walidacja i inicjalizacja Etapu 3 ───────────────────────────────────
    if (sa_enabled_) {
        if (sa_config_.T_initial < 0.0)
            throw std::invalid_argument("SANelderMead: SAConfig.T_initial musi być >= 0");
        if (sa_config_.schedule == CoolingSchedule::Geometric &&
            !(sa_config_.geometric_rate > 0.0 && sa_config_.geometric_rate < 1.0))
            throw std::invalid_argument("SANelderMead: geometric_rate musi być w (0,1)");

        resetCooling();   // wymusza T_current=T_initial, k=0 — niezależnie od
                           // tego, co użytkownik wpisał w sa_config.T_current
    } else {
        // SA wyłączone — normalizuj jawnie do 0, żeby getTemperature() nie
        // zwracało mylącego "1.0" (domyślne SAConfig::T_current) dla
        // solvera, który w ogóle nie używa SA.
        sa_config_.T_initial = 0.0;
        sa_config_.T_current = 0.0;
    }
}

SANelderMead SANelderMead::withSA(
    std::vector<FitParam> all_params, ObjectiveFn objective_fn,
    SAConfig sa_config, unsigned int rng_seed)
{
    return SANelderMead(std::move(all_params), std::move(objective_fn),
                         1.0, 2.0, 0.5, 0.5, 1e-12,
                         true, std::move(sa_config), rng_seed);
}

// ─── Inicjalizacja simpleksu (Etap 2, bez zmian) ─────────────────────────────

void SANelderMead::initSimplex()
{
    initSimplexAround(extractFreeValues(all_params_, free_indices_));
}

void SANelderMead::initSimplexAround(const std::vector<double>& start)
{
    const int N_free = static_cast<int>(free_indices_.size());

    state_.vertices.assign(N_free + 1, start);
    state_.chi2_values.assign(N_free + 1, 0.0);

    for (int i = 1; i <= N_free; ++i) {
        const int dim = i - 1;
        const double range = free_max_[dim] - free_min_[dim];
        const double delta = 0.05 * range;

        double candidate = start[dim] + delta;
        if (candidate > free_max_[dim])
            candidate = start[dim] - delta;

        state_.vertices[i][dim] = candidate;
        clipToBounds(state_.vertices[i]);
    }

    for (int i = 0; i <= N_free; ++i)
        state_.chi2_values[i] = evaluateVertex(state_.vertices[i]);

    int best, second_worst, worst;
    sortIndices(best, second_worst, worst);
    state_.best_idx  = best;
    state_.worst_idx = worst;
}

// ─── Krok algorytmu (ZMODYFIKOWANY — kryterium SA wstawione w hooku Etapu 2) ─

StepType SANelderMead::step()
{
    if (state_.vertices.empty())
        throw std::logic_error("SANelderMead::step() wywołane przed initSimplex()");

    if (isDegenerate()) {
        restart();
        if (sa_enabled_) updateCooling();
        state_.T_current = sa_config_.T_current;
        state_.iteration++;
        return StepType::Restart;
    }

    int best, second_worst, worst;
    sortIndices(best, second_worst, worst);

    const double f_best         = state_.chi2_values[best];
    const double f_second_worst = state_.chi2_values[second_worst];
    const double f_worst        = state_.chi2_values[worst];

    const std::vector<double> centroid = computeCentroidExcluding(worst);

    std::vector<double> x_r = reflectPoint(centroid, state_.vertices[worst], alpha_);
    clipToBounds(x_r);
    const double f_r = evaluateVertex(x_r);

    StepType applied;

    if (f_r < f_best) {
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
        // f_r >= f_second_worst. NAJPIERW kryterium SA — TYLKO w pod-przypadku
        // f_r >= f_worst (patrz "Decyzje architektoniczne" C w planie Etapu 2,
        // oraz przypomnienie w planie Etapu 3).
        bool sa_accepted = false;

        if (sa_enabled_ && f_r >= f_worst && sa_config_.T_current > 0.0) {
            const double delta_f = f_r - f_worst;   // >= 0 w tej gałęzi
            const double P = std::exp(-delta_f / sa_config_.T_current);
            const double roll = uniform_dist_(rng_);

            if (roll < P) {
                state_.vertices[worst]    = x_r;
                state_.chi2_values[worst] = f_r;
                applied = StepType::Reflection;   // SA zaakceptował gorszy
                                                   // punkt — patrz "Decyzje
                                                   // architektoniczne" (E)
                sa_accepted = true;
                ++sa_accepted_count_;
            }
        }

        if (!sa_accepted) {
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
    }

    if (sa_enabled_) updateCooling();
    state_.T_current = sa_config_.T_current;

    int new_best, new_second_worst, new_worst;
    sortIndices(new_best, new_second_worst, new_worst);
    state_.best_idx  = new_best;
    state_.worst_idx = new_worst;
    state_.iteration++;

    return applied;
}

// ─── Pętla zbieżności, buildResult, bestFullParams (Etap 2, bez zmian) ──────

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
    r.delta_chi2  = 0.0;
    r.iterations  = state_.iteration;
    r.converged   = converged;
    r.stop_reason = stop_reason;
    return r;
}

std::vector<double> SANelderMead::bestFullParams() const
{
    return buildFullParams(bestParams(), all_params_, free_indices_);
}

// ─── Etap 3: kontrola temperatury (NOWE) ──────────────────────────────────────

void SANelderMead::setTemperature(double T)
{
    if (!sa_enabled_)
        throw std::logic_error(
            "SANelderMead::setTemperature() wywołane, ale SA nie jest włączone (sa_enabled=false)");
    sa_config_.T_current = T;
}

void SANelderMead::resetCooling()
{
    if (!sa_enabled_)
        throw std::logic_error(
            "SANelderMead::resetCooling() wywołane, ale SA nie jest włączone (sa_enabled=false)");
    sa_config_.T_current = sa_config_.T_initial;
    cooling_k_ = 0;
}

void SANelderMead::setCoolingSchedule(CoolingSchedule schedule)
{
    if (!sa_enabled_)
        throw std::logic_error(
            "SANelderMead::setCoolingSchedule() wywołane, ale SA nie jest włączone (sa_enabled=false)");
    sa_config_.schedule = schedule;
}

double SANelderMead::computeBoltzmannTemperature(int k) const
{
    // k>=1 zawsze — patrz updateCooling() (cooling_k_ inkrementowany PRZED
    // wywołaniem). Unika ln(1+0)=0 (dzielenie przez zero). Patrz "Decyzje
    // architektoniczne" (B).
    return sa_config_.T_initial / std::log(1.0 + static_cast<double>(k));
}

double SANelderMead::computeGeometricTemperature(int k) const
{
    return sa_config_.T_initial * std::pow(sa_config_.geometric_rate, static_cast<double>(k));
}

void SANelderMead::updateCooling()
{
    ++cooling_k_;   // startuje od 0; pierwsza aktualizacja używa k=1

    switch (sa_config_.schedule) {
        case CoolingSchedule::Boltzmann:
            sa_config_.T_current = computeBoltzmannTemperature(cooling_k_);
            break;
        case CoolingSchedule::Geometric:
            sa_config_.T_current = computeGeometricTemperature(cooling_k_);
            break;
        case CoolingSchedule::Adaptive:
            // Placeholder (experiment-plan.md: "do doprecyzowania po
            // testach") — na razie deleguje do Boltzmann.
            sa_config_.T_current = computeBoltzmannTemperature(cooling_k_);
            break;
    }
}

// ─── Pomocnicze prywatne (Etap 2, bez zmian) ──────────────────────────────────

void SANelderMead::sortIndices(int& best, int& second_worst, int& worst) const
{
    const int n = static_cast<int>(state_.chi2_values.size());
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [this](int a, int b) {
        return state_.chi2_values[a] < state_.chi2_values[b];
    });
    best         = order.front();
    worst        = order.back();
    second_worst = order[n - 2];
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
    const std::vector<double> best_vertex = state_.vertices[best_idx];
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
        if (std::isnan(v[i])) continue;
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
    if (!withinBounds(free_values))
        return kInf;
    return objective_fn_(free_values);
}
```

---

## 6. Weryfikacja i testy

Osiem funkcji testowych. Pierwsze pięć mapują się bezpośrednio na kryteria
ukończenia z `experiment-plan.md`; pozostałe trzy to dodatkowe testy
mechanizmu, które wyłapują regresje, jakich literalne kryteria nie pokrywają
wprost (np. czy SA w ogóle "strzela", czy placeholder Adaptive faktycznie
zwraca Boltzmann).

### 6.1 T=0 → identyczne jak Etap 2

```cpp
#include "../solver/nelder_mead.hpp"
#include "../solver/solver_types.hpp"
#include <cassert>
#include <cstdio>
#include <string>

static void testZeroTemperatureMatchesStage2()
{
    const std::vector<double> c = { 1.0, -2.0, 3.0 };
    auto makeParams = [&]() {
        std::vector<FitParam> p;
        for (int i = 0; i < 3; ++i)
            p.push_back(FitParam("x" + std::to_string(i), 0.0, -10.0, 10.0, true));
        return p;
    };
    SANelderMead::ObjectiveFn obj = [c](const std::vector<double>& x) {
        double s = 0.0;
        for (size_t i = 0; i < x.size(); ++i) { const double d = x[i]-c[i]; s += d*d; }
        return s;
    };

    SANelderMead solver_nm(makeParams(), obj);   // Etap 2: sa_enabled domyślnie false
    solver_nm.initSimplex();
    const FitResult r_nm = solver_nm.runUntilConvergence(1000, 1e-10);

    SAConfig sa_zero;
    sa_zero.T_initial = 0.0;   // jedyna różnica względem domyślnego SAConfig
    auto solver_sa = SANelderMead::withSA(makeParams(), obj, sa_zero, /*seed*/ 42u);
    solver_sa.initSimplex();
    const FitResult r_sa = solver_sa.runUntilConvergence(1000, 1e-10);

    fprintf(stdout, "[T=0 match] NM: iter=%d chi2=%.6e | SA(T=0): iter=%d chi2=%.6e\n",
            r_nm.iterations, r_nm.chi2_min, r_sa.iterations, r_sa.chi2_min);

    assert(r_nm.iterations == r_sa.iterations);
    assert(r_nm.chi2_min == r_sa.chi2_min);
    for (size_t i = 0; i < r_nm.best_params.size(); ++i)
        assert(r_nm.best_params[i] == r_sa.best_params[i]);

    fprintf(stdout, "[T=0 match] PASS\n");
}
```

### 6.2 Ucieczka z lokalnego minimum

```cpp
#include "../solver/nelder_mead.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>

static void testLocalMinimumEscape()
{
    // f(x) = min((x-1)², (x+1)²) - eps·x
    // Asymetria (-eps·x) realizuje "−ε·noise" z experiment-plan.md w sposób
    // deterministycznym i analitycznie weryfikowalnym (patrz pułapki —
    // dosłowne "noise" jako losowy szum byłoby nie-powtarzalne w testach).
    //
    // Prawdziwe globalne minimum: x≈+1.025, f≈-0.0506
    // Lokalna pułapka:            x≈-0.975, f≈+0.0494
    // "Garb" rozdzielający obie miski w x=0: f(0)=1.0
    const double eps = 0.05;
    SANelderMead::ObjectiveFn obj = [eps](const std::vector<double>& x) -> double {
        const double left  = (x[0] + 1.0) * (x[0] + 1.0);
        const double right = (x[0] - 1.0) * (x[0] - 1.0);
        return std::min(left, right) - eps * x[0];
    };

    SAConfig sa;
    sa.T_initial = 5.0;                          // wartość z experiment-plan.md
    sa.schedule  = CoolingSchedule::Geometric;
    sa.geometric_rate = 0.995;                    // wolne chłodzenie — dużo prób ucieczki

    int successes = 0;
    const int kRuns = 10;
    for (int run = 0; run < kRuns; ++run) {
        std::vector<FitParam> params = { FitParam("x", -1.0, -5.0, 5.0, true) };  // start w lewej misce
        auto solver = SANelderMead::withSA(params, obj, sa, /*seed*/ 1000u + run);
        solver.initSimplex();
        const FitResult result = solver.runUntilConvergence(3000, 1e-12);

        const bool escaped = result.best_params[0] > 0.0;
        fprintf(stdout, "[Local min escape] run=%d  x=%.4f  chi2=%.4f  escaped=%d\n",
                run, result.best_params[0], result.chi2_min, escaped);
        if (escaped) ++successes;
    }

    fprintf(stdout, "[Local min escape] SA: %d/%d ucieczek do globalnego minimum\n", successes, kRuns);
    assert(successes >= 9 && "SA-NM nie uciekło z lokalnego minimum w >=9/10 uruchomień");

    // Kontrola: czysty NM (sa_enabled=false) z TEGO SAMEGO punktu startowego
    // MUSI utknąć — potwierdza, że to SA robi różnicę, nie sam kształt funkcji.
    {
        std::vector<FitParam> params = { FitParam("x", -1.0, -5.0, 5.0, true) };
        SANelderMead solver(params, obj);
        solver.initSimplex();
        const FitResult result = solver.runUntilConvergence(3000, 1e-12);
        fprintf(stdout, "[Local min escape] czysty NM: x=%.4f (oczekiwane: utyka < 0)\n",
                result.best_params[0]);
        assert(result.best_params[0] < 0.0 && "Czysty NM nieoczekiwanie uciekł — test niediagnostyczny");
    }

    fprintf(stdout, "[Local min escape] PASS\n");
}
```

### 6.3 `setTemperature(0)` w trakcie działania → determinizm

```cpp
static void testSetTemperatureZeroMidRun()
{
    auto runScenario = []() -> FitResult {
        std::vector<FitParam> params = { FitParam("x", 5.0, -10.0, 10.0, true) };
        SANelderMead::ObjectiveFn obj = [](const std::vector<double>& x) { return x[0]*x[0]; };

        SAConfig sa;
        sa.T_initial = 3.0;
        sa.schedule  = CoolingSchedule::Geometric;
        sa.geometric_rate = 0.98;

        auto solver = SANelderMead::withSA(params, obj, sa, /*seed*/ 7u);
        solver.initSimplex();

        for (int i = 0; i < 50; ++i) solver.step();    // faza stochastyczna
        solver.setTemperature(0.0);                     // wyłącz losowość
        for (int i = 0; i < 200; ++i) solver.step();    // faza deterministyczna

        FitResult r;
        r.best_params = solver.bestParams();
        r.chi2_min    = solver.bestChiSquared();
        r.iterations  = solver.iteration();
        return r;
    };

    const FitResult r1 = runScenario();
    const FitResult r2 = runScenario();

    assert(r1.iterations == r2.iterations);
    assert(r1.chi2_min == r2.chi2_min);
    for (size_t i = 0; i < r1.best_params.size(); ++i)
        assert(r1.best_params[i] == r2.best_params[i]);

    fprintf(stdout, "[setTemperature(0) determinism] PASS — 2 powtórzenia identyczne\n");
}
```

> Uwaga: ten test potwierdza powtarzalność całego scenariusza (ten sam seed
> → ten sam wynik), co jest najprostszą i dosłowną interpretacją kryterium
> z `experiment-plan.md`. Mocniejsza wersja (dowód niezależności od
> *jakiegokolwiek* stanu RNG, nie tylko tego samego seeda) wymagałaby
> zdolności "zaszczepienia" simpleksu istniejącym stanem — capability, której
> ten etap nie buduje, bo nie jest do niczego innego potrzebna. Patrz
> pułapki.

### 6.4 Boltzmann: monotoniczność

```cpp
static void testBoltzmannMonotonic()
{
    std::vector<FitParam> params = { FitParam("x", 0.0, -1.0, 1.0, true) };
    SANelderMead::ObjectiveFn obj = [](const std::vector<double>& x) { return x[0]*x[0]; };

    SAConfig sa;
    sa.T_initial = 10.0;
    sa.schedule  = CoolingSchedule::Boltzmann;

    auto solver = SANelderMead::withSA(params, obj, sa, /*seed*/ 1u);
    solver.initSimplex();

    std::vector<double> temps;
    for (int i = 0; i < 100; ++i) {
        solver.step();
        temps.push_back(solver.getTemperature());
    }

    // Sprawdzamy sekwencję T_1..T_100 (PO każdym kroku, k startuje od 1) —
    // NIE porównujemy do T_initial. Patrz "Decyzje architektoniczne" (B, D).
    bool monotonic_non_increasing = true;
    for (size_t i = 1; i < temps.size(); ++i) {
        if (temps[i] > temps[i-1] + 1e-12) {
            monotonic_non_increasing = false;
            fprintf(stderr, "[Boltzmann FAIL] T wzrosła: T[%zu]=%.6f -> T[%zu]=%.6f\n",
                    i-1, temps[i-1], i, temps[i]);
        }
    }

    fprintf(stdout, "[Boltzmann] T_1=%.4f  T_50=%.4f  T_100=%.4f\n",
            temps.front(), temps[49], temps.back());
    assert(monotonic_non_increasing && "Boltzmann: temperatura nie jest monotonicznie nierosnąca");

    fprintf(stdout, "[Boltzmann] PASS\n");
}
```

### 6.5 Geometric: stosunek `T_{k+1}/T_k`

```cpp
static void testGeometricRatio()
{
    std::vector<FitParam> params = { FitParam("x", 0.0, -1.0, 1.0, true) };
    SANelderMead::ObjectiveFn obj = [](const std::vector<double>& x) { return x[0]*x[0]; };

    SAConfig sa;
    sa.T_initial = 10.0;
    sa.schedule = CoolingSchedule::Geometric;
    sa.geometric_rate = 0.95;

    auto solver = SANelderMead::withSA(params, obj, sa, /*seed*/ 2u);
    solver.initSimplex();

    std::vector<double> temps;
    for (int i = 0; i < 100; ++i) {
        solver.step();
        temps.push_back(solver.getTemperature());
    }

    bool ok = true;
    for (size_t i = 1; i < temps.size(); ++i) {
        const double ratio = temps[i] / temps[i-1];
        if (std::abs(ratio - sa.geometric_rate) > 1e-9) {
            fprintf(stderr, "[Geometric FAIL] i=%zu: ratio=%.12f, expected=%.12f\n",
                    i, ratio, sa.geometric_rate);
            ok = false;
        }
    }

    assert(ok && "Geometric: T_{k+1}/T_k != geometric_rate dla pewnego kroku");
    fprintf(stdout, "[Geometric] PASS (stosunek T_{k+1}/T_k = %.4f dla 100 kroków)\n", sa.geometric_rate);
}
```

### 6.6 Adaptive placeholder — zgodność z Boltzmann (regresja)

```cpp
static void testAdaptivePlaceholderMatchesBoltzmann()
{
    std::vector<FitParam> params_b = { FitParam("x", 0.0, -1.0, 1.0, true) };
    std::vector<FitParam> params_a = { FitParam("x", 0.0, -1.0, 1.0, true) };
    SANelderMead::ObjectiveFn obj = [](const std::vector<double>& x) { return x[0]*x[0]; };

    SAConfig sa_b; sa_b.T_initial = 7.0; sa_b.schedule = CoolingSchedule::Boltzmann;
    SAConfig sa_a; sa_a.T_initial = 7.0; sa_a.schedule = CoolingSchedule::Adaptive;

    auto solver_b = SANelderMead::withSA(params_b, obj, sa_b, 3u);
    auto solver_a = SANelderMead::withSA(params_a, obj, sa_a, 3u);
    solver_b.initSimplex();
    solver_a.initSimplex();

    for (int i = 0; i < 10; ++i) {
        solver_b.step();
        solver_a.step();
        assert(solver_b.getTemperature() == solver_a.getTemperature());
    }

    fprintf(stdout, "[Adaptive placeholder] PASS (zgodny z Boltzmann dla 10 kroków)\n");
}
```

### 6.7 Reprodukowalność seeda

```cpp
static void testSeedReproducibility()
{
    auto runWithSeed = [](unsigned int seed) -> FitResult {
        std::vector<FitParam> params = { FitParam("x", -1.0, -5.0, 5.0, true) };
        SANelderMead::ObjectiveFn obj = [](const std::vector<double>& x) -> double {
            const double left  = (x[0]+1.0)*(x[0]+1.0);
            const double right = (x[0]-1.0)*(x[0]-1.0);
            return std::min(left, right) - 0.05*x[0];
        };
        SAConfig sa; sa.T_initial = 5.0; sa.schedule = CoolingSchedule::Geometric; sa.geometric_rate = 0.995;
        auto solver = SANelderMead::withSA(params, obj, sa, seed);
        solver.initSimplex();
        return solver.runUntilConvergence(3000, 1e-12);
    };

    const FitResult a1 = runWithSeed(123u);
    const FitResult a2 = runWithSeed(123u);   // ten sam seed
    const FitResult b  = runWithSeed(456u);   // inny seed, tylko do logu

    assert(a1.best_params[0] == a2.best_params[0]);
    assert(a1.chi2_min == a2.chi2_min);

    fprintf(stdout, "[Seed reproducibility] seed=123 (x2): x=%.6f / x=%.6f (identyczne)\n",
            a1.best_params[0], a2.best_params[0]);
    fprintf(stdout, "[Seed reproducibility] seed=456:      x=%.6f (dla porównania)\n",
            b.best_params[0]);
    fprintf(stdout, "[Seed reproducibility] PASS\n");

    // UWAGA: celowo NIE assertujemy a1 != b — różne seedy mogą (rzadko)
    // wylądować w tej samej misce. Test sprawdza tylko własność, której
    // jesteśmy pewni: ten sam seed => identyczny wynik.
}
```

### 6.8 Mechanizm SA rzeczywiście "strzela"

```cpp
static void testSAMechanismFires()
{
    // Bezpośrednia weryfikacja licznika akceptacji — nie tylko pośrednio
    // przez wynik końcowy (jak w 6.2), ale wprost: czy gałąź Metropolis w
    // ogóle kiedykolwiek zaakceptowała gorszy punkt przy wysokiej T.
    std::vector<FitParam> params = { FitParam("x", 0.0, -10.0, 10.0, true) };
    SANelderMead::ObjectiveFn obj = [](const std::vector<double>& x) { return x[0]*x[0]; };

    SAConfig sa; sa.T_initial = 50.0; sa.schedule = CoolingSchedule::Geometric; sa.geometric_rate = 0.999;
    auto solver = SANelderMead::withSA(params, obj, sa, /*seed*/ 99u);
    solver.initSimplex();

    for (int i = 0; i < 200; ++i) solver.step();

    fprintf(stdout, "[SA fires] liczba akceptacji SA w 200 krokach: %d\n", solver.getSAAcceptedCount());
    assert(solver.getSAAcceptedCount() > 0 && "Mechanizm SA nigdy nie zaakceptował gorszego punktu przy T=50");

    fprintf(stdout, "[SA fires] PASS\n");
}
```

---

## 7. Wiring testów

### 7.1 `src/tests/test_stage3.hpp`

```cpp
#pragma once
void runStage3Tests();
```

### 7.2 `src/tests/test_stage3.cpp`

```cpp
#include "test_stage3.hpp"
#include "../solver/nelder_mead.hpp"
#include "../solver/solver_types.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
    // ── tu wklej wszystkie 8 funkcji statycznych z sekcji 6 ──
}

void runStage3Tests()
{
    fprintf(stdout, "\n=== Stage 3 Self-Tests ===\n");
    testZeroTemperatureMatchesStage2();
    testLocalMinimumEscape();
    testSetTemperatureZeroMidRun();
    testBoltzmannMonotonic();
    testGeometricRatio();
    testAdaptivePlaceholderMatchesBoltzmann();
    testSeedReproducibility();
    testSAMechanismFires();
    fprintf(stdout, "=== Stage 3 PASS ===\n\n");
}
```

### 7.3 `app.cpp`

```cpp
#include "app.hpp"

#ifndef NDEBUG
#include "tests/test_stage1.hpp"
#include "tests/test_stage2.hpp"
#include "tests/test_stage3.hpp"
#endif

void appInit(AppState& appState)
{
#ifndef NDEBUG
    runStage1Tests();
    runStage2Tests();
    runStage3Tests();
#endif
}
```

---

## 8. CMakeLists.txt

Jedyna zmiana: dopisanie nowego pliku testowego. `solver_core` jest bez
zmian, bo `nelder_mead.cpp` jest modyfikowany **w miejscu**, nie dodawany
jako nowy plik.

```cmake
add_library(solver_tests STATIC
    src/tests/test_stage1.cpp
    src/tests/test_stage2.cpp
    src/tests/test_stage3.cpp   # NOWE
)
```

---

## 9. Kolejność implementacji (szacunek czasowy)

```
Blok 1 — Rozszerzenie konstruktora i pól SA (1h):
  [20m]  nowe pola prywatne (sa_enabled_, sa_config_, cooling_k_, rng_,
         uniform_dist_, sa_accepted_count_)
  [25m]  rozszerzony konstruktor + walidacja + withSA() + wymuszenie
         resetCooling()/T=0 zależnie od sa_enabled
  [15m]  kompilacja, URUCHOMIENIE testów Etapu 1+2 — MUSZĄ przejść
         bez ŻADNEJ zmiany w ich kodzie (kluczowa weryfikacja wstecznej
         kompatybilności, zanim przejdziesz dalej)

Blok 2 — Harmonogramy chłodzenia (45m):
  [15m]  computeBoltzmannTemperature/computeGeometricTemperature
  [15m]  updateCooling() + przełącznik schedule (w tym Adaptive placeholder)
  [15m]  setTemperature/resetCooling/setCoolingSchedule + guardy logic_error

Blok 3 — Kryterium SA w step() (1h):
  [30m]  wstawienie bloku Metropolis dokładnie w miejscu hooka z Etapu 2
  [15m]  wywołanie updateCooling() w OBU ścieżkach wyjścia (Restart i normalnej)
  [15m]  state_.T_current synchronizacja, ręczna weryfikacja jednego pełnego
         przebiegu step() z fprintf debug

Blok 4 — Testy (2.5h):
  [20m]  testZeroTemperatureMatchesStage2
  [30m]  testLocalMinimumEscape — w tym ewentualne dostrojenie eps/T_initial
  [20m]  testSetTemperatureZeroMidRun
  [20m]  testBoltzmannMonotonic
  [20m]  testGeometricRatio
  [15m]  testAdaptivePlaceholderMatchesBoltzmann
  [15m]  testSeedReproducibility
  [10m]  testSAMechanismFires

Blok 5 — Integracja (30m):
  [15m]  test_stage3.hpp/cpp + app.cpp wiring
  [15m]  pełne uruchomienie WSZYSTKICH trzech etapów testów razem

Łącznie: ~5h45min netto
```

---

## 10. Checklist ukończenia Etapu 3

| #   | Wymaganie (`experiment-plan.md`)                                   | Plik / funkcja                        | Weryfikacja                                  |
| --- | ------------------------------------------------------------------ | ------------------------------------- | -------------------------------------------- |
| 1   | `f(x_r)≥f(worst)` → losowanie zamiast odrzucenia                   | `step()`, blok SA                     | testSAMechanismFires, testLocalMinimumEscape |
| 2   | `P = exp(-Δf/T)`                                                   | `step()`                              | testSAMechanismFires (pośrednio)             |
| 3   | `std::mt19937` + `uniform_real_distribution(0,1)`                  | pola `rng_`, `uniform_dist_`          | —                                            |
| 4   | Seed konfigurowalny                                                | konstruktor (`rng_seed`)              | **testSeedReproducibility**                  |
| 5   | `T=0` → `P=0` zawsze                                               | `step()` (warunek `T_current>0.0`)    | **testZeroTemperatureMatchesStage2**         |
| 6   | Boltzmann: `T_k=T_initial/ln(1+k)`                                 | `computeBoltzmannTemperature`         | **testBoltzmannMonotonic**                   |
| 7   | Geometric: `T_k=T_initial·rate^k`                                  | `computeGeometricTemperature`         | **testGeometricRatio**                       |
| 8   | Adaptive placeholder = Boltzmann                                   | `updateCooling` (switch)              | testAdaptivePlaceholderMatchesBoltzmann      |
| 9   | Harmonogram wybierany przez `SAConfig::schedule`, zmiana w runtime | `setCoolingSchedule()`                | —                                            |
| 10  | `setTemperature` — modyfikuje `T_current`, NIE `k`/`T_initial`     | `setTemperature()`                    | testSetTemperatureZeroMidRun                 |
| 11  | `resetCooling` — `T_current=T_initial`, `k=0`                      | `resetCooling()`                      | wywoływane w konstruktorze                   |
| 12  | Oba wywołania bezpieczne w trakcie działania                       | proste mutacje pól, brak side-effects | —                                            |
| 13  | Przy `T=0`: identyczne jak czysty NM (deterministyczne)            | `step()` (RNG pomijany całkowicie)    | **testZeroTemperatureMatchesStage2**         |
| 14  | Lokalne minimum: NM utyka, SA(T=5.0) ucieka ≥9/10                  | `step()` + cały solver                | **testLocalMinimumEscape**                   |
| 15  | `setTemperature(0)` mid-run → kolejne kroki deterministyczne       | `step()`                              | **testSetTemperatureZeroMidRun**             |
| 16  | Boltzmann: T monotonicznie nierosnąca (korekta "niemalejąca")      | `computeBoltzmannTemperature`         | **testBoltzmannMonotonic**                   |
| 17  | Geometric: `T_{k+1}/T_k=geometric_rate`, 100 kroków                | `computeGeometricTemperature`         | **testGeometricRatio**                       |

**Etap 3 gotowy = wszystkie 17 pozycji ✓ ORAZ wszystkie testy Etapu 1+2
nadal przechodzą bez modyfikacji → można zacząć Etap 4 (debug trace).**

---

## 11. Typowe pułapki

| Problem                                                       | Przyczyna                                                                          | Rozwiązanie                                                                                                                                                                    |
| ------------------------------------------------------------- | ---------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Testy Etapu 2 przestają się kompilować/przechodzić            | nowe parametry konstruktora wstawione w środku listy, nie na końcu                 | WSZYSTKIE nowe parametry (`sa_enabled`, `sa_config`, `rng_seed`) dopisane NA KOŃCU z defaultami — patrz "Decyzje architektoniczne" (A)                                         |
| Domyślne `SAConfig{}` (`T_initial=1.0`) włącza SA "po cichu"  | użycie samego faktu podania `SAConfig` jako wyzwalacza                             | jawna flaga `sa_enabled=false` jako osobny parametr, niezależny od zawartości `SAConfig`                                                                                       |
| `ln(1+0)=0` → dzielenie przez zero w Boltzmann                | `k=0` nie jest poprawnym wejściem do wzoru                                         | `cooling_k_` inkrementowany PRZED obliczeniem T — pierwsza aktualizacja zawsze `k≥1`                                                                                           |
| Test monotoniczności Boltzmann nie przechodzi                 | doc mówi "niemalejąca", wzór matematycznie maleje                                  | przyjęta interpretacja: literówka w dokumencie źródłowym — patrz "Decyzje architektoniczne" (D); test sprawdza "nierosnąca"                                                    |
| `testLocalMinimumEscape` niestabilny (np. 6/10 zamiast ≥9/10) | `eps`/`T_initial`/`geometric_rate` źle dobrane względem wysokości "garbu"          | wartości w planie (eps=0.05, T_initial=5.0, rate=0.995) dobrane analitycznie pod garb o wysokości ≈1.0 — w razie potrzeby dostroić jeden z trzech parametrów                   |
| `setTemperature()`/`resetCooling()` rzucają wyjątkiem         | wywołane na solverze z `sa_enabled=false`                                          | to ZAMIERZONE — patrz "Decyzje architektoniczne" (F); użyj `sa_enabled=true` lub `withSA()` przy konstrukcji                                                                   |
| RNG-state "wycieka" między niezależnymi próbami testu         | współdzielony `std::mt19937` między run-ami w pętli                                | KAŻDY przebieg w `testLocalMinimumEscape` tworzy NOWY solver z NOWYM seedem — nigdy nie reużywaj jednego solvera między niezależnymi próbami                                   |
| `chi2_best` pozornie "psuje się" po akceptacji SA             | błędne założenie, że SA może podmienić `best_idx` na gorszy                        | SA podmienia WYŁĄCZNIE slot `worst` — `best` pozostaje nietknięty, więc `chi2_best` jest monotonicznie nierosnące NAWET przez akceptacje SA — ważny niezmiennik dla Etapu 4    |
| `testSetTemperatureZeroMidRun` wydaje się "za słaby"          | sprawdza tylko "ten sam seed → ten sam wynik", nie pełną niezależność od stanu RNG | to świadomy wybór zakresu — patrz uwaga pod sekcją 6.3; mocniejszy test wymagałby capability "zaszczep simpleks istniejącym stanem", niepotrzebnej nigdzie indziej w projekcie |