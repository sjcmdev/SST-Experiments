// src/solver/solver_types.hpp
#pragma once
#include <string>
#include <vector>

// ─── Harmonogramy chłodzenia SA ──────────────────────────────────────────────

/// Dostępne harmonogramy chłodzenia dla Simulated Annealing.
enum class CoolingSchedule
{
    Boltzmann, ///< T_k = T₀ / ln(1 + k)          [default — powolne, stabilne]
    Geometric, ///< T_k = T₀ · geometric_rate^k    [geometric_rate ∈ (0,1)]
    Adaptive   ///< heurystyczny — placeholder, implementacja po testach Etap 3
};

// ─── Parametr dopasowania ────────────────────────────────────────────────────

/// Jeden parametr modelu: wartość aktualna, granice, flaga wolności.
/// Solver operuje WYŁĄCZNIE na wartościach liniowych — bez wewnętrznych log/exp.
/// Transformacja log/exp (jeśli potrzebna) jest wewnątrz funkcji modelu.
struct FitParam
{
    std::string name; ///< czytelna nazwa: "I0", "A", "Rs", "Rsh", ...
    double value;     ///< aktualna wartość [skala liniowa]
    double min;       ///< dolna granica [skala liniowa]
    double max;       ///< górna granica [skala liniowa]
    bool free;        ///< true = optymalizowany; false = stały (zignorowany przez solver)

    FitParam() : value(0.0), min(0.0), max(1.0), free(true) {}

    FitParam(std::string n, double v, double lo, double hi, bool f = true)
        : name(std::move(n)), value(v), min(lo), max(hi), free(f) {}
};

// ─── Konfiguracja SA ─────────────────────────────────────────────────────────

/// Konfiguracja Simulated Annealing — temperatura i harmonogram chłodzenia.
struct SAConfig
{
    double T_initial = 1.0;                                ///< temperatura startowa
    double T_current = 1.0;                                ///< bieżąca T (modyfikowalna w runtime)
    CoolingSchedule schedule = CoolingSchedule::Boltzmann; ///< harmonogram
    double geometric_rate = 0.99;                          ///< α dla Geometric schedule
};

// ─── Stan simpleksu ──────────────────────────────────────────────────────────

/// Pełny stan simpleksu Nelder–Mead w danym momencie iteracji.
/// Kopiowany do TraceStep (state_before / state_after) — dlatego musi być tani w kopii.
/// N_free = liczba wolnych parametrów; N_vertices = N_free + 1.
struct SimplexState
{
    std::vector<std::vector<double>> vertices; ///< (N+1) × N_free; vertices[i][j] = j-ty param i-tego wierzchołka
    std::vector<double> chi2_values;           ///< chi2 per wierzchołek; len = N+1
    int best_idx = 0;                          ///< indeks wierzchołka z min chi2
    int worst_idx = 0;                         ///< indeks wierzchołka z max chi2
    std::vector<double> centroid;              ///< N_free wartości; średnia N najlepszych (bez worst)
    int iteration = 0;
    double T_current = 0.0;
};

// ─── Typ kroku simpleksu ─────────────────────────────────────────────────────

/// Typ operacji wykonanej w danym kroku NM/SA.
/// Zapisywany w TraceStep.type dla pełnej obserwowalności.
enum class StepType
{
    Reflection,  ///< x_r = centroid + α·(centroid - worst)
    Expansion,   ///< x_e = centroid + γ·(x_r - centroid),  gdy f(x_r) < f(best)
    Contraction, ///< x_c = centroid + ρ·(worst - centroid), gdy reflection odrzucona
    Shrink,      ///< xᵢ = best + σ·(xᵢ - best) dla wszystkich i ≠ best
    Restart      ///< automatyczny restart po degeneracji simpleksu
};

// ─── Krok trace ──────────────────────────────────────────────────────────────

/// Zapis jednego kroku solvera — fundament debugowania i walidacji GPU (Faza II).
/// Alokowany tylko gdy trace_enabled = true w SANelderMead.
struct TraceStep
{
    StepType type;             ///< co się stało
    SimplexState state_before; ///< kopia stanu PRZED krokiem (deep copy)
    SimplexState state_after;  ///< kopia stanu PO kroku (deep copy)
    double chi2_min;           ///< aktualne globalne minimum po kroku
    double T;                  ///< temperatura w tym kroku
    int iteration;             ///< globalny numer kroku (0-based)
};

// ─── Wynik dopasowania ───────────────────────────────────────────────────────

/// Wynik zwracany przez SANelderMead::runUntilConvergence().
struct FitResult
{
    std::vector<double> best_params; ///< tylko free params, skala liniowa, len = N_free
    double chi2_min;
    double delta_chi2; ///< chi2_min - chi2_floor; 0.0 jeśli brak referencji
    int iterations;
    bool converged;
    std::string stop_reason; ///< "tol" | "max_iter" | "degenerate" | ...
};