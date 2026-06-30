// src/solver/phys_const.hpp
#pragma once

/// Stałe fizyczne dla modelu IV diody.
namespace PhysConst
{

    /// Stała Boltzmanna [J/K]
    static constexpr double k_B = 1.380649e-23;

    /// Ładunek elementarny [C]
    static constexpr double q = 1.602176634e-19;

    /// k = k_B / q [V/K]
    ///
    /// UWAGA: w dostarczonych formułach modelu IV 'k' oznacza k_B/q, nie k_B.
    /// Sprawdzenie jednostek: A * k * T = A * (k_B/q) * T = A * Vt [V] ✓
    /// Przy T=300K: k * 300 = 8.617e-5 * 300 ≈ 0.025852 V = Vt
    static constexpr double k = k_B / q; // ≈ 8.617333e-5 V/K

    /// Temperatura referencyjna [K]
    static constexpr double T_ref = 300.0;

    /// Napięcie termiczne przy T_ref: Vt = k_B·T/q [V]
    static constexpr double Vt_ref = k * T_ref; // ≈ 0.025852 V
}