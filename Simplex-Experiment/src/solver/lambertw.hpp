// src/solver/lambertw.hpp
#pragma once
#include "../../vendor/lambertw/LambertW.hpp"
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
[[nodiscard]] inline double lambertW(double x) noexcept
{
    // -1/e z pełną precyzją double (17 cyfr znaczących)
    static constexpr double kMinusInvE = -0.36787944117144232159;
    if (std::isnan(x) || x < kMinusInvE)
        return std::numeric_limits<double>::quiet_NaN();
    return utl::LambertW<0>(x);
}