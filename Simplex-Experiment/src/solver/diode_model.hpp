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
    double V, const double *params, double T = 300.0) noexcept;

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
    double V, const double *params, double T = 300.0) noexcept;