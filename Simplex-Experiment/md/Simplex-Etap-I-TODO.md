# Plan Implementacji — Etap 1 (Szczegółowy)
## Struktury danych, LambertW, Model IV, χ²

> **Środowisko:** C++17, istniejący codebase: `app.hpp/cpp`, `gui.hpp/cpp`, `main.cpp`
> **Dostarczone:** `LambertW.h/cc` (Veberic, `utl::LambertW<0>`), `Horner.h`, formuły IV
> **Cel:** Działający, testowalny kod. Żadnych skoków — każdy krok kompiluje.

---

## 1. Drzewo plików do stworzenia

```
projekt/
├── src/
│   ├── app.hpp / app.cpp          ← ISTNIEJĄCE — nie modyfikować (poza appInit debug)
│   ├── gui.hpp / gui.cpp          ← ISTNIEJĄCE
│   ├── main.cpp                   ← ISTNIEJĄCE
│   └── solver/
│       ├── phys_const.hpp         ← NOWY: stałe k, q, Vt — sekcja 2
│       ├── solver_types.hpp       ← NOWY: FitParam, SAConfig, SimplexState, ... — sekcja 3
│       ├── lambertw.hpp           ← NOWY: bezpieczny wrapper utl::LambertW<0> — sekcja 4
│       ├── diode_model.hpp        ← NOWY: deklaracje evaluateDiodeIV4/6 — sekcja 5
│       ├── diode_model.cpp        ← NOWY: implementacje
│       ├── objective.hpp          ← NOWY: computeChiSquared, computeDeltaChiSquared — sekcja 6
│       └── objective.cpp          ← NOWY: implementacje
└── vendor/
    └── lambertw/
        ├── LambertW.h             ← SKOPIOWANY z uploadu bez zmian
        ├── LambertW.cc            ← SKOPIOWANY z uploadu bez zmian
        └── Horner.h               ← SKOPIOWANY z uploadu bez zmian
```

**Decyzja LambertW:** Używamy wyłącznie `utl::LambertW<0>` (Veberic, `LambertW.cc`).
`FukushimaLambertW.h/.cc` zachowujemy jako backup — nie linkujemy w Etapie 1.

**Ważne:** `LambertW.cc` ma `#include "Horner.h"` — oba pliki muszą być w tym samym
katalogu (`vendor/lambertw/`). Kompilator musi dostać `-I vendor/lambertw/`.

---

## 2. `phys_const.hpp` — stałe fizyczne

```cpp
// src/solver/phys_const.hpp
#pragma once

/// Stałe fizyczne dla modelu IV diody.
namespace PhysConst {

    /// Stała Boltzmanna [J/K]
    static constexpr double k_B = 1.380649e-23;

    /// Ładunek elementarny [C]
    static constexpr double q = 1.602176634e-19;

    /// k = k_B / q [V/K]
    ///
    /// UWAGA: w dostarczonych formułach modelu IV 'k' oznacza k_B/q, nie k_B.
    /// Sprawdzenie jednostek: A * k * T = A * (k_B/q) * T = A * Vt [V] ✓
    /// Przy T=300K: k * 300 = 8.617e-5 * 300 ≈ 0.025852 V = Vt
    static constexpr double k = k_B / q;   // ≈ 8.617333e-5 V/K

    /// Temperatura referencyjna [K]
    static constexpr double T_ref = 300.0;

    /// Napięcie termiczne przy T_ref: Vt = k_B·T/q [V]
    static constexpr double Vt_ref = k * T_ref;  // ≈ 0.025852 V
}
```

### Dlaczego k = k_B/q, nie k_B?

Formuła: `x = (I0 * Rs / (A * k * T)) * exp(V / (A * k * T))`

Dla `x` być bezwymiarowym argumentem W₀, mianownik `A * k * T` musi mieć jednostki
woltu (jak V i I₀·Rₛ). Przy `k = k_B/q`:

```
A * k * T = A * (8.617e-5 V/K) * 300 K = A * 0.02585 V = A * Vt  ✓
```

Przy `k = k_B` (J/K) wynik miałby jednostki J, nie V. ✗

---

## 3. `solver_types.hpp` — wszystkie struktury i enumy

Bezpośrednia implementacja API z `god-file.md` sekcja 8.1.
Wszystkie domyślne wartości odpowiadają eksperymentowi.

```cpp
// src/solver/solver_types.hpp
#pragma once
#include <string>
#include <vector>

// ─── Harmonogramy chłodzenia SA ──────────────────────────────────────────────

/// Dostępne harmonogramy chłodzenia dla Simulated Annealing.
enum class CoolingSchedule {
    Boltzmann,   ///< T_k = T₀ / ln(1 + k)          [default — powolne, stabilne]
    Geometric,   ///< T_k = T₀ · geometric_rate^k    [geometric_rate ∈ (0,1)]
    Adaptive     ///< heurystyczny — placeholder, implementacja po testach Etap 3
};

// ─── Parametr dopasowania ────────────────────────────────────────────────────

/// Jeden parametr modelu: wartość aktualna, granice, flaga wolności.
/// Solver operuje WYŁĄCZNIE na wartościach liniowych — bez wewnętrznych log/exp.
/// Transformacja log/exp (jeśli potrzebna) jest wewnątrz funkcji modelu.
struct FitParam {
    std::string name;    ///< czytelna nazwa: "I0", "A", "Rs", "Rsh", ...
    double      value;   ///< aktualna wartość [skala liniowa]
    double      min;     ///< dolna granica [skala liniowa]
    double      max;     ///< górna granica [skala liniowa]
    bool        free;    ///< true = optymalizowany; false = stały (zignorowany przez solver)

    FitParam() : value(0.0), min(0.0), max(1.0), free(true) {}

    FitParam(std::string n, double v, double lo, double hi, bool f = true)
        : name(std::move(n)), value(v), min(lo), max(hi), free(f) {}
};

// ─── Konfiguracja SA ─────────────────────────────────────────────────────────

/// Konfiguracja Simulated Annealing — temperatura i harmonogram chłodzenia.
struct SAConfig {
    double          T_initial      = 1.0;                        ///< temperatura startowa
    double          T_current      = 1.0;                        ///< bieżąca T (modyfikowalna w runtime)
    CoolingSchedule schedule       = CoolingSchedule::Boltzmann; ///< harmonogram
    double          geometric_rate = 0.99;                       ///< α dla Geometric schedule
};

// ─── Stan simpleksu ──────────────────────────────────────────────────────────

/// Pełny stan simpleksu Nelder–Mead w danym momencie iteracji.
/// Kopiowany do TraceStep (state_before / state_after) — dlatego musi być tani w kopii.
/// N_free = liczba wolnych parametrów; N_vertices = N_free + 1.
struct SimplexState {
    std::vector<std::vector<double>> vertices;    ///< (N+1) × N_free; vertices[i][j] = j-ty param i-tego wierzchołka
    std::vector<double>              chi2_values; ///< chi2 per wierzchołek; len = N+1
    int                              best_idx  = 0; ///< indeks wierzchołka z min chi2
    int                              worst_idx = 0; ///< indeks wierzchołka z max chi2
    std::vector<double>              centroid;    ///< N_free wartości; średnia N najlepszych (bez worst)
    int                              iteration = 0;
    double                           T_current = 0.0;
};

// ─── Typ kroku simpleksu ─────────────────────────────────────────────────────

/// Typ operacji wykonanej w danym kroku NM/SA.
/// Zapisywany w TraceStep.type dla pełnej obserwowalności.
enum class StepType {
    Reflection,    ///< x_r = centroid + α·(centroid - worst)
    Expansion,     ///< x_e = centroid + γ·(x_r - centroid),  gdy f(x_r) < f(best)
    Contraction,   ///< x_c = centroid + ρ·(worst - centroid), gdy reflection odrzucona
    Shrink,        ///< xᵢ = best + σ·(xᵢ - best) dla wszystkich i ≠ best
    Restart        ///< automatyczny restart po degeneracji simpleksu
};

// ─── Krok trace ──────────────────────────────────────────────────────────────

/// Zapis jednego kroku solvera — fundament debugowania i walidacji GPU (Faza II).
/// Alokowany tylko gdy trace_enabled = true w SANelderMead.
struct TraceStep {
    StepType     type;          ///< co się stało
    SimplexState state_before;  ///< kopia stanu PRZED krokiem (deep copy)
    SimplexState state_after;   ///< kopia stanu PO kroku (deep copy)
    double       chi2_min;      ///< aktualne globalne minimum po kroku
    double       T;             ///< temperatura w tym kroku
    int          iteration;     ///< globalny numer kroku (0-based)
};

// ─── Wynik dopasowania ───────────────────────────────────────────────────────

/// Wynik zwracany przez SANelderMead::runUntilConvergence().
struct FitResult {
    std::vector<double> best_params;  ///< tylko free params, skala liniowa, len = N_free
    double              chi2_min;
    double              delta_chi2;   ///< chi2_min - chi2_floor; 0.0 jeśli brak referencji
    int                 iterations;
    bool                converged;
    std::string         stop_reason;  ///< "tol" | "max_iter" | "degenerate" | ...
};
```

---

## 4. Integracja LambertW

### 4.1 Skopiowanie plików vendor

Skopiuj pliki do `vendor/lambertw/` bez żadnych modyfikacji:

| Plik źródłowy | Cel                           |
| ------------- | ----------------------------- |
| `LambertW.h`  | `vendor/lambertw/LambertW.h`  |
| `LambertW.cc` | `vendor/lambertw/LambertW.cc` |
| `Horner.h`    | `vendor/lambertw/Horner.h`    |

`LambertW.cc` zawiera `#include "Horner.h"` — wymaga, żeby `Horner.h`
był widoczny przez `-I vendor/lambertw/` (lub ścieżkę względną).

### 4.2 Co robi `utl::LambertW<0>` wewnętrznie

Implementacja Veberic używa selektora zakresów (makro `Y5`):

| Zakres x                  | Metoda startowa                                               | Refinement |
| ------------------------- | ------------------------------------------------------------- | ---------- |
| x bliskie -1/e (< -0.367) | `BranchPointExpansion<8>` — szereg wokół punktu rozgałęzienia | 1× Halley  |
| x ∈ (-0.311, 1.38)        | `Pade<0,1>` — aproksymacja Padégo                             | 1× Halley  |
| x ∈ (1.38, 236)           | `Pade<0,2>` — aproksymacja Padégo                             | 1× Halley  |
| x > 236                   | `AsymptoticExpansion<5>` — rozwinięcie asymptotyczne          | 1× Halley  |

Jeden krok Halleya po aproksymacji startowej daje precyzję bliską maszynowej (~15–16 cyfr).
Krok Halleya: `w' = w - (wew - x) · w1 / (ew · w1² - (w+2)(wew-x)/2)`

### 4.3 `lambertw.hpp` — bezpieczny wrapper

```cpp
// src/solver/lambertw.hpp
#pragma once
#include "../../vendor/lambertw/LambertW.h"
#include <cmath>
#include <limits>

/// W₀(x) — główna gałąź funkcji Lamberta W.
/// Implementacja bazuje na Veberic (utl), iteracja Halleya — ~15-16 cyfr dokładności.
///
/// Precondition: x >= -1/e ≈ -0.367879441...
/// Postcondition: W₀(x) · exp(W₀(x)) = x
///
/// Zwraca quiet_NaN() gdy:
///   - x < -1/e (poza dziedziną W₀)
///   - x jest NaN wejściowym
/// Nie rzuca wyjątków. Nie crashuje.
[[nodiscard]] inline double lambertW(double x) noexcept {
    // -1/e z pełną precyzją double (17 cyfr znaczących)
    static constexpr double kMinusInvE = -0.36787944117144232159;
    if (std::isnan(x) || x < kMinusInvE)
        return std::numeric_limits<double>::quiet_NaN();
    return utl::LambertW<0>(x);
}
```

**Dlaczego wrapper, a nie `utl::LambertW<0>` bezpośrednio?**

`utl::LambertW<0>` dla `x < -1/e` wypisuje do `stderr` i zwraca NaN — ale nie ma
gwarantowanego kontraktu `noexcept`. Wrapper: (a) nie wypisuje nic, (b) jest `noexcept`,
(c) ma jasną dokumentację, (d) izoluje zależność od implementacji vendora.

---

## 5. Model IV — `diode_model.hpp` + `diode_model.cpp`

### 5.1 Analiza formuły (4 parametry)

Dostarczona formuła:
```cpp
double x    = (I0 * Rs / (A * k * T)) * exp(V / (A * k * T));
double I_lw = utl::LambertW<0>(x);
I_lw *= (A * k * T) / Rs;
I    = I_lw + (V - I_lw * Rs) / Rsh;
```

Podstawienie: `A * k * T = A * Vt` gdzie `Vt = PhysConst::k * T`:

```
x    = (I₀ · Rₛ / (A·Vt)) · exp(V / (A·Vt))
I_lw = W₀(x) · (A·Vt) / Rₛ
I    = I_lw + (V − I_lw · Rₛ) / Rₛₕ
```

**Przypadek degeneracyjny Rs = 0:**

Gdy Rₛ → 0: `x → 0`, `W₀(0) = 0`, ale `I_lw = 0 · (A·Vt) / 0` → 0/0 → NaN.

Granica analityczna: `W₀(x) ≈ x` dla `x → 0`, więc:

```
I_lw = W₀(x) · (A·Vt)/Rₛ ≈ x · (A·Vt)/Rₛ
     = [I₀·Rₛ/(A·Vt) · exp(V/(A·Vt))] · (A·Vt)/Rₛ
     = I₀ · exp(V / (A·Vt))
```

Obsłużone przez specjalną gałąź: `if (Rs == 0.0) return I0*exp(V/AVt) + V/Rsh;`

### 5.2 `diode_model.hpp`

```cpp
// src/solver/diode_model.hpp
#pragma once

/// Modele I(V) ciemnej charakterystyki (dark IV) z jawną formułą Lambert W.
/// Parametry ZAWSZE w skali liniowej — solver nie widzi transformacji log/exp.

// ─── Model 4-parametrowy ─────────────────────────────────────────────────────

/// Oblicza I(V) dla jednodidodowego modelu z rezystancjami Rₛ i Rₛₕ.
///
/// params[0] = I₀  — prąd nasycenia [A],         typowo 1e-15 … 1e-6
/// params[1] = A   — współczynnik idealności [-], typowo 0.5 … 3.0
/// params[2] = Rₛ  — rezystancja szeregowa [Ω],  typowo 0 … 10
/// params[3] = Rₛₕ — rezystancja bocznikowa [Ω], typowo 10 … 1e6
/// T               — temperatura [K], default 300.0
///
/// Zwraca NaN gdy: I₀≤0, A≤0, Rₛₕ≤0, Rₛ<0, T≤0, lub argument W₀ < -1/e.
/// Nie rzuca wyjątków. Nie crashuje dla żadnych wartości parametrów.
[[nodiscard]] double evaluateDiodeIV4(
    double V, const double* params, double T = 300.0) noexcept;

// ─── Model 6-parametrowy ─────────────────────────────────────────────────────

/// Rozszerzenie 4-param o nieliniowy człon bocznikowy: (Vⱼ)^α / Rₛₕ₂
/// gdzie Vⱼ = V − I_lw · Rₛ (napięcie na złączu).
///
/// params[0..3] = jak w modelu 4-param
/// params[4] = α    — wykładnik potęgowy [-]
/// params[5] = Rₛₕ₂ — rezystancja bocznikowa 2 [Ω]
///
/// UWAGA: pow(Vⱼ, α) jest NaN gdy Vⱼ < 0 i α jest niecałkowite.
/// W takim przypadku zwraca NaN (traktowane przez solver jako +∞).
[[nodiscard]] double evaluateDiodeIV6(
    double V, const double* params, double T = 300.0) noexcept;
```

### 5.3 `diode_model.cpp`

```cpp
// src/solver/diode_model.cpp
#include "diode_model.hpp"
#include "lambertw.hpp"
#include "phys_const.hpp"

#include <cmath>
#include <limits>

namespace {
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
}

// ─── 4 parametry ─────────────────────────────────────────────────────────────

double evaluateDiodeIV4(double V, const double* params, double T) noexcept
{
    const double I0  = params[0];
    const double A   = params[1];
    const double Rs  = params[2];
    const double Rsh = params[3];

    // ── Walidacja ─────────────────────────────────────────────────────────
    if (I0  <= 0.0 || A   <= 0.0 ||
        Rsh <= 0.0 || Rs  <  0.0 || T <= 0.0)
        return kNaN;

    const double Vt  = PhysConst::k * T;   // k_B·T/q [V]
    const double AVt = A * Vt;             // A·Vt [V]

    // ── Przypadek graniczny Rs = 0 ────────────────────────────────────────
    // Unika 0·(A·Vt)/0 = 0/0 = NaN.
    // Granica analityczna: I_lw → I₀·exp(V/AVt)
    if (Rs == 0.0) {
        return I0 * std::exp(V / AVt) + V / Rsh;
    }

    // ── Argument Lambert W ────────────────────────────────────────────────
    // x = (I₀·Rₛ / (A·Vt)) · exp(V / (A·Vt))
    const double x = (I0 * Rs / AVt) * std::exp(V / AVt);

    // ── W₀(x) ─────────────────────────────────────────────────────────────
    const double w = lambertW(x);
    if (std::isnan(w)) return kNaN;   // x < -1/e — niefizyczne parametry

    // ── Prąd wynikowy ─────────────────────────────────────────────────────
    // I_lw = W₀(x) · (A·Vt) / Rₛ
    const double I_lw = w * AVt / Rs;

    // I = I_lw + (V − I_lw·Rₛ) / Rₛₕ
    return I_lw + (V - I_lw * Rs) / Rsh;
}

// ─── 6 parametrów ────────────────────────────────────────────────────────────

double evaluateDiodeIV6(double V, const double* params, double T) noexcept
{
    const double I0    = params[0];
    const double A     = params[1];
    const double Rs    = params[2];
    const double Rsh   = params[3];
    const double alpha = params[4];
    const double Rsh2  = params[5];

    // ── Walidacja ─────────────────────────────────────────────────────────
    if (I0   <= 0.0 || A    <= 0.0 ||
        Rsh  <= 0.0 || Rs   <  0.0 ||
        Rsh2 <= 0.0 || T    <= 0.0)
        return kNaN;

    const double Vt  = PhysConst::k * T;
    const double AVt = A * Vt;

    // ── I_lw — analogicznie do 4-param ───────────────────────────────────
    double I_lw;

    if (Rs == 0.0) {
        I_lw = I0 * std::exp(V / AVt);
    } else {
        const double x = (I0 * Rs / AVt) * std::exp(V / AVt);
        const double w = lambertW(x);
        if (std::isnan(w)) return kNaN;
        I_lw = w * AVt / Rs;
    }

    // ── Napięcie na złączu ────────────────────────────────────────────────
    const double Vj = V - I_lw * Rs;

    // ── Człon nieliniowy: Vⱼ^α / Rₛₕ₂ ───────────────────────────────────
    // UWAGA: pow(Vj, alpha) = NaN gdy Vj < 0 i alpha ∉ Z.
    // To jest fizycznie możliwe w zaporze (Vj < 0). Zwracamy NaN —
    // solver potraktuje to jako +∞ i odrzuci kandydata.
    const double power_term = std::pow(Vj, alpha);
    if (std::isnan(power_term)) return kNaN;

    return I_lw + Vj / Rsh + power_term / Rsh2;
}
```

---

## 6. Funkcja celu χ² — `objective.hpp` + `objective.cpp`

### 6.1 `objective.hpp`

```cpp
// src/solver/objective.hpp
#pragma once
#include <functional>

/// χ²(p) = Σᵢ [(I_meas(Vᵢ) − I_model(Vᵢ, p))² / σᵢ²]
///
/// Parametry:
///   params   — wskaźnik na wektor parametrów (przekazywany do model_fn)
///   V_data   — napięcia pomiarowe [V], len = N_points
///   I_meas   — prądy pomiarowe [A], len = N_points
///   sigma    — niepewności pomiarowe [A], len = N_points; musi być > 0
///   N_points — liczba punktów danych
///   model_fn — I_model(V, params) — dowolna funkcja modelu
///
/// Zwraca +∞ gdy:
///   - którykolwiek I_model(Vᵢ) = NaN lub ±∞
///   - którykolwiek sigma[i] ≤ 0
/// Nie rzuca wyjątków.
[[nodiscard]] double computeChiSquared(
    const double*                                   params,
    const double*                                   V_data,
    const double*                                   I_meas,
    const double*                                   sigma,
    int                                             N_points,
    std::function<double(double, const double*)>    model_fn
) noexcept;

/// Δχ²(p) = χ²(p) − χ²_min
///
/// Zastosowania:
///   - kryterium zatrzymania: Δχ² < ε
///   - przedziały ufności: Δχ²=1 → 1σ, Δχ²=4 → 2σ
[[nodiscard]] inline double computeDeltaChiSquared(
    double chi2, double chi2_min) noexcept
{
    return chi2 - chi2_min;
}
```

### 6.2 `objective.cpp`

```cpp
// src/solver/objective.cpp
#include "objective.hpp"
#include <cmath>
#include <limits>

double computeChiSquared(
    const double*                                params,
    const double*                                V_data,
    const double*                                I_meas,
    const double*                                sigma,
    int                                          N_points,
    std::function<double(double, const double*)> model_fn
) noexcept
{
    constexpr double kInf = std::numeric_limits<double>::infinity();

    double chi2 = 0.0;

    for (int i = 0; i < N_points; ++i) {
        // Walidacja sigma — musi być dodatnia
        if (sigma[i] <= 0.0) return kInf;

        const double I_model = model_fn(V_data[i], params);

        // NaN lub Inf z modelu → kandydat niefizyczny → odrzucamy go (+∞)
        // Solver widzi +∞ i nie crashuje
        if (!std::isfinite(I_model)) return kInf;

        const double residual = (I_meas[i] - I_model) / sigma[i];
        chi2 += residual * residual;
    }

    return chi2;
}
```

---

## 7. Weryfikacja i testy

Testy umieszczamy w `app.cpp::appInit` za `#ifndef NDEBUG`.
Każdy test jest niezależny — failed assert wskazuje dokładnie co się psuje.

### 7.1 Test LambertW — znane wartości

```cpp
// Wklej do app.cpp lub osobnego test_stage1.cpp
#include "solver/lambertw.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

static void testLambertW()
{
    // Wartości referencyjne z scipy.special.lambertw(x, 0).real
    struct Case { double x, expected, tol; };
    static const Case cases[] = {
        // Punkt trywialny
        { 0.0,          0.0,                    1e-15 },
        // Stała Omega: W(1) = 0.5671432904097838...
        { 1.0,          0.5671432904097838,      1e-13 },
        // W(e) = 1
        { M_E,          1.0,                    1e-13 },
        // Punkt rozgałęzienia: W(-1/e) = -1
        { -1.0/M_E,    -1.0,                    1e-10 },
        // Bardzo małe x: W(x) ≈ x
        { 1e-10,        1e-10,                  1e-20 },
        // Typowy zakres modelu IV: x ≈ 1e-4
        { 1e-4,         9.9990001499975e-5,     1e-17 },
        // x = 0.01
        { 0.01,         0.009901473843595671,   1e-15 },
        // x duże: W(100) ≈ 3.3856...
        { 100.0,        3.3856301402835985,     1e-12 },
        // x bardzo duże: W(1e6) ≈ 11.383...
        { 1e6,          11.383133649802337,     1e-10 },
    };

    int failed = 0;
    for (const auto& c : cases) {
        double result = lambertW(c.x);
        double err = std::abs(result - c.expected);
        if (err > c.tol) {
            fprintf(stderr, "[LambertW FAIL] x=%.6g: got=%.17g, exp=%.17g, err=%.3e, tol=%.3e\n",
                    c.x, result, c.expected, err, c.tol);
            ++failed;
        }
    }

    // Edge cases — muszą zwrócić NaN, nie crash
    assert(std::isnan(lambertW(-0.4)));
    assert(std::isnan(lambertW(-1.0)));
    assert(std::isnan(lambertW(-1e6)));
    assert(std::isnan(lambertW(std::numeric_limits<double>::quiet_NaN())));
    // x = 0 → dokładnie 0
    assert(lambertW(0.0) == 0.0);

    fprintf(stdout, "[LambertW] %s (%d/%zu passed)\n",
            failed == 0 ? "PASS" : "FAIL",
            (int)(sizeof(cases)/sizeof(cases[0])) - failed,
            sizeof(cases)/sizeof(cases[0]));
    assert(failed == 0);
}
```

### 7.2 Test modelu IV — sanity fizyczny

```cpp
#include "solver/diode_model.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

static void testDiodeIV4Physics()
{
    // Parametry referencyjne — fizycznie sensowne
    // I0=1nA, A=1.5, Rs=0.1Ω, Rsh=1kΩ
    const double params[] = { 1e-9, 1.5, 0.1, 1000.0 };

    // Siatka od -0.5V do +0.7V
    const double V_arr[] = { -0.5, -0.3, -0.1, 0.0, 0.1, 0.3, 0.5, 0.6, 0.7 };
    const int N = 9;

    fprintf(stdout, "\n[DiodeIV4] V [V]  ->  I [A]\n");
    double I_prev = evaluateDiodeIV4(V_arr[0], params);

    for (int i = 0; i < N; ++i) {
        double I = evaluateDiodeIV4(V_arr[i], params);
        fprintf(stdout, "  V=%+5.2f  ->  I = %+.6e\n", V_arr[i], I);

        // Kryterium 1: brak NaN
        assert(!std::isnan(I) && "evaluateDiodeIV4 returned NaN for valid params");

        // Kryterium 2: monotoniczność (prąd rośnie z napięciem)
        if (i > 0) {
            assert(I > I_prev && "evaluateDiodeIV4: I(V) not monotonically increasing");
        }
        I_prev = I;
    }

    // Kryterium 3: przy V=0 prąd ≈ 0 (bardzo małe I0)
    double I_zero = evaluateDiodeIV4(0.0, params);
    assert(std::abs(I_zero) < 1e-6 && "I(V=0) should be near zero");

    fprintf(stdout, "[DiodeIV4] Physics sanity: PASS\n");
}
```

### 7.3 Test chi2 = 0 — roundtrip syntetyczny

```cpp
#include "solver/objective.hpp"
#include "solver/diode_model.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

static void testChiSquaredZero()
{
    // Parametry "prawdziwe"
    const double true_params[] = { 1e-10, 1.5, 0.05, 2000.0 };
    const int N = 30;

    // Generacja syntetycznych danych z TEGO SAMEGO modelu
    double V_data[N], I_meas[N], sigma[N];
    for (int i = 0; i < N; ++i) {
        V_data[i] = -0.5 + i * (1.2 / (N - 1));  // od -0.5V do +0.7V
        I_meas[i] = evaluateDiodeIV4(V_data[i], true_params);
        assert(!std::isnan(I_meas[i]));
        sigma[i] = 1e-6;  // 1 μA niepewność (wartość nie ma znaczenia dla chi2=0)
    }

    // chi2 z tymi samymi parametrami → 0 (jedyna różnica to błąd numeryczny)
    auto model_fn = [](double V, const double* p) {
        return evaluateDiodeIV4(V, p);
    };

    double chi2 = computeChiSquared(true_params, V_data, I_meas, sigma, N, model_fn);
    fprintf(stdout, "[chi2 roundtrip] chi2 = %.3e  (limit: < 1e-20)\n", chi2);
    // Kryterium z experiment-plan.md: tolerancja numeryczna < 1e-20
    assert(chi2 < 1e-20 && "chi2 != 0 for identical model/data — numerical issue");

    fprintf(stdout, "[chi2 roundtrip] PASS\n");
}
```

### 7.4 Testy edge case

```cpp
static void testEdgeCases()
{
    // ── Model IV — niepoprawne parametry → NaN, bez crash ────────────────
    struct BadCase { const char* name; double params[4]; };
    static const BadCase bad[] = {
        { "I0 < 0",    { -1e-10, 1.5,   0.1, 1000.0 } },
        { "I0 = 0",    {  0.0,   1.5,   0.1, 1000.0 } },
        { "A < 0",     {  1e-10, -1.0,  0.1, 1000.0 } },
        { "A = 0",     {  1e-10,  0.0,  0.1, 1000.0 } },
        { "Rsh < 0",   {  1e-10,  1.5,  0.1, -100.0 } },
        { "Rs < 0",    {  1e-10,  1.5, -0.1, 1000.0 } },
    };

    for (const auto& c : bad) {
        double I = evaluateDiodeIV4(0.5, c.params);
        if (!std::isnan(I)) {
            fprintf(stderr, "[EdgeCase FAIL] %s: expected NaN, got %.6g\n", c.name, I);
            assert(false);
        }
    }

    // ── Rs = 0 → nie NaN, fizycznie sensowne ──────────────────────────────
    const double p_rs0[] = { 1e-10, 1.5, 0.0, 1000.0 };
    double I_rs0 = evaluateDiodeIV4(0.5, p_rs0);
    assert(!std::isnan(I_rs0) && I_rs0 > 0.0);

    // ── Bardzo duże V (overflow w exp → x duże → W daje sensowny wynik) ──
    const double p_ok[] = { 1e-10, 1.5, 0.1, 1000.0 };
    double I_big = evaluateDiodeIV4(10.0, p_ok);   // 10V — bardzo duże dla diody
    // Może zwrócić NaN (nasycenie numeryyczne) — ale nie crash
    (void)I_big;

    // ── chi2: NaN z modelu → zwraca +∞, nie crash ─────────────────────────
    auto nan_model = [](double, const double*) {
        return std::numeric_limits<double>::quiet_NaN();
    };
    const double dummy[] = { 0.0 };
    const double V2[]    = { 0.0, 0.1 };
    const double I2[]    = { 0.0, 0.0 };
    const double s2[]    = { 1.0, 1.0 };
    double chi2_nan = computeChiSquared(dummy, V2, I2, s2, 2, nan_model);
    assert(std::isinf(chi2_nan) && chi2_nan > 0.0);

    // ── chi2: sigma = 0 → +∞ ──────────────────────────────────────────────
    const double s_zero[] = { 1.0, 0.0 };  // drugi sigma = 0
    double chi2_sigma = computeChiSquared(p_ok, V2, I2, s_zero, 2,
        [](double V, const double* p){ return evaluateDiodeIV4(V, p); });
    assert(std::isinf(chi2_sigma) && chi2_sigma > 0.0);

    fprintf(stdout, "[EdgeCases] PASS\n");
}
```

### 7.5 Uruchomienie testów w `app.cpp`

```cpp
// app.cpp
#ifndef NDEBUG
// Deklaracje testów (lub include osobnego nagłówka)
static void testLambertW();
static void testDiodeIV4Physics();
static void testChiSquaredZero();
static void testEdgeCases();
#endif

void appInit(AppState& appState)
{
#ifndef NDEBUG
    fprintf(stdout, "\n=== Stage 1 Self-Tests ===\n");
    testLambertW();
    testDiodeIV4Physics();
    testChiSquaredZero();
    testEdgeCases();
    fprintf(stdout, "=== Stage 1 PASS ===\n\n");
#endif
}
```

---

## 8. System budowania — CMakeLists.txt (uzupełnienie)

```cmake
# ─── Vendor: LambertW (Veberic) ──────────────────────────────────────────────
add_library(lambertw_vendor STATIC
    vendor/lambertw/LambertW.cc
)
target_include_directories(lambertw_vendor PUBLIC
    vendor/lambertw/           # LambertW.h + Horner.h widoczne dla includerów
)
# LambertW.cc sam includuje Horner.h przez "" — wymaga tego samego katalogu
target_compile_options(lambertw_vendor PRIVATE -Wno-unused-function)

# ─── Solver — Etap 1 ─────────────────────────────────────────────────────────
add_library(solver_stage1 STATIC
    src/solver/diode_model.cpp
    src/solver/objective.cpp
)
target_include_directories(solver_stage1 PUBLIC
    src/solver/                # lambertw.hpp, phys_const.hpp, solver_types.hpp
)
target_link_libraries(solver_stage1
    PUBLIC lambertw_vendor     # PUBLIC bo lambertw.hpp includuje LambertW.h
)
target_compile_options(solver_stage1 PRIVATE
    -Wall -Wextra -std=c++17
)

# ─── Główna aplikacja ─────────────────────────────────────────────────────────
target_link_libraries(gpu-experiment
    PRIVATE solver_stage1
    # ... pozostałe: imgui, implot, imnodes, glfw, glad
)
```

**Uwaga `#include` w `lambertw.hpp`:**
Ścieżka `../../vendor/lambertw/LambertW.h` jest relatywna do `src/solver/`.
Alternatywnie użyj `target_include_directories` z `vendor/lambertw/`
i zmień include na `<LambertW.h>` — zależy od konwencji projektu.

---

## 9. Kolejność implementacji (szacunek czasowy)

```
Blok 1 — Fundamenty (1.5h):
  [30m]  phys_const.hpp      — napisz + sprawdź jednostki (k * 300 ≈ 0.02585 V)
  [20m]  solver_types.hpp    — wszystkie struktury, kompilacja bez ostrzeżeń
  [20m]  vendor/lambertw/    — skopiuj 3 pliki, sprawdź że LambertW.cc kompiluje
  [20m]  lambertw.hpp        — wrapper, ręczny test: lambertW(1.0) == 0.567...
  [10m]  CMakeLists.txt      — lambertw_vendor + solver_stage1, sprawdź linkowanie

Blok 2 — Model IV (1.5h):
  [30m]  diode_model.hpp     — deklaracje z doc-comments
  [40m]  diode_model.cpp     — implementacja evaluateDiodeIV4 + edge cases
  [20m]  evaluateDiodeIV6    — dopisz na bazie 4-param (10 linii różnicy)

Blok 3 — Chi2 (30m):
  [20m]  objective.hpp/cpp   — computeChiSquared + computeDeltaChiSquared
  [10m]  weryfikacja: chi2 == 0 ręcznie na 3 punktach

Blok 4 — Testy (1h):
  [30m]  testLambertW        — 9 wartości referencyjnych + NaN
  [20m]  testDiodeIV4Physics — siatka V, monotoniczność
  [10m]  testChiSquaredZero  — syntetyczny roundtrip
  [10m]  testEdgeCases       — złe parametry, sigma=0, NaN model
  [10m]  fix ewentualnych problemów, uruchom aplikację, sprawdź stdout

Łącznie: ~4.5h netto
```

---

## 10. Checklist ukończenia Etapu 1

| #   | Element                                  | Plik               | Kryterium           |
| --- | ---------------------------------------- | ------------------ | ------------------- |
| 1   | `CoolingSchedule` enum (3 wartości)      | `solver_types.hpp` | kompiluje           |
| 2   | `FitParam` struct + defaulty             | `solver_types.hpp` | kompiluje           |
| 3   | `SAConfig` struct + defaulty             | `solver_types.hpp` | kompiluje           |
| 4   | `SimplexState` struct                    | `solver_types.hpp` | (N+1)×N_free layout |
| 5   | `StepType` enum (5 wartości)             | `solver_types.hpp` | kompiluje           |
| 6   | `TraceStep` struct                       | `solver_types.hpp` | before/after state  |
| 7   | `FitResult` struct                       | `solver_types.hpp` | stop_reason string  |
| 8   | `lambertW(0.0)` == 0.0                   | `lambertw.hpp`     | dokładnie           |
| 9   | `lambertW(-1/e)` ≈ -1                    | `lambertw.hpp`     | tol < 1e-10         |
| 10  | `lambertW(-0.4)` → NaN                   | `lambertw.hpp`     | nie crash           |
| 11  | 9 wartości vs scipy tol < 1e-10          | test               | wszystkie pass      |
| 12  | `evaluateDiodeIV4` — Rs=0                | `diode_model.cpp`  | nie NaN, > 0        |
| 13  | `evaluateDiodeIV4` — I0≤0                | `diode_model.cpp`  | NaN                 |
| 14  | `evaluateDiodeIV4` — A≤0                 | `diode_model.cpp`  | NaN                 |
| 15  | `evaluateDiodeIV4` — Rsh≤0               | `diode_model.cpp`  | NaN                 |
| 16  | `evaluateDiodeIV4` — Rs<0                | `diode_model.cpp`  | NaN                 |
| 17  | I(V) monotonicznie rośnie                | test               | fizyczna sanity     |
| 18  | `evaluateDiodeIV6` — Vj<0, α frac        | `diode_model.cpp`  | NaN (nie crash)     |
| 19  | `computeChiSquared` roundtrip = 0        | `objective.cpp`    | < 1e-20             |
| 20  | `computeChiSquared` NaN → +∞             | `objective.cpp`    | `isinf` i dodatnie  |
| 21  | `computeChiSquared` sigma=0 → +∞         | `objective.cpp`    | `isinf` i dodatnie  |
| 22  | `computeDeltaChiSquared`                 | `objective.hpp`    | inline, poprawne    |
| 23  | Self-testy w `appInit` — wszystkie PASS  | `app.cpp`          | brak `assert fail`  |
| 24  | Kompilacja `-Wall -Wextra` bez ostrzeżeń | CMake              | zero warnings       |
| 25  | Aplikacja startuje i wyświetla GUI       | `main.cpp`         | ImGui renderuje     |

**Etap 1 gotowy = wszystkie 25 pozycji ✓ → można zacząć Etap 2 (inicjalizacja simpleksu).**

---

## 11. Typowe pułapki

| Problem                                 | Przyczyna                            | Rozwiązanie                                                              |
| --------------------------------------- | ------------------------------------ | ------------------------------------------------------------------------ |
| `LambertW.cc` nie kompiluje             | Horner.h nie znaleziony              | Oba pliki w tym samym katalogu, `-I vendor/lambertw/`                    |
| `lambertW(-1/e)` ≠ -1 z tol 1e-10       | `utl::LambertW<0>` near branch point | Punkt rozgałęzienia jest pokryty przez `BranchPointExpansion` — OK       |
| `evaluateDiodeIV4(0, rs0_params)` = NaN | Rs=0 → 0/0                           | Gałąź `if (Rs == 0.0)` — implementuj przed głównym wzorem                |
| chi2 roundtrip ≠ 0                      | Różna temperatura T                  | Upewnij się że `evaluateDiodeIV4` dostaje to samo T w generacji i teście |
| `pow(Vj, alpha)` crash                  | Domyślna obsługa błędów FPU          | Sprawdź `std::isnan(power_term)` po `std::pow`                           |
| `assert` w release build ignorowany     | `NDEBUG` zdefiniowany                | Używaj `#ifndef NDEBUG` na bloku testowym                                |