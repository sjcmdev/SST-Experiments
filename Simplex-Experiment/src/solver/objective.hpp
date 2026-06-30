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
    const double *params,
    const double *V_data,
    const double *I_meas,
    const double *sigma,
    int N_points,
    std::function<double(double, const double *)> model_fn) noexcept;

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