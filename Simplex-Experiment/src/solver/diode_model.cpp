// src/solver/diode_model.cpp
#include "diode_model.hpp"
#include "lambertw.hpp"
#include "phys_const.hpp"

#include <cmath>
#include <limits>

namespace
{
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
}

// ─── 4 parametry ─────────────────────────────────────────────────────────────

double evaluateDiodeIV4(double V, const double *params, double T) noexcept
{
    const double I0 = params[0];
    const double A = params[1];
    const double Rs = params[2];
    const double Rsh = params[3];

    // ── Walidacja ─────────────────────────────────────────────────────────
    if (I0 <= 0.0 || A <= 0.0 ||
        Rsh <= 0.0 || Rs < 0.0 || T <= 0.0)
        return kNaN;

    const double Vt = PhysConst::k * T; // k_B·T/q [V]
    const double AVt = A * Vt;          // A·Vt [V]

    // ── Przypadek graniczny Rs = 0 ────────────────────────────────────────
    // Unika 0·(A·Vt)/0 = 0/0 = NaN.
    // Granica analityczna: I_lw → I₀·exp(V/AVt)
    if (Rs == 0.0)
    {
        return I0 * std::exp(V / AVt) + V / Rsh;
    }

    // ── Argument Lambert W ────────────────────────────────────────────────
    // x = (I₀·Rₛ / (A·Vt)) · exp(V / (A·Vt))
    const double x = (I0 * Rs / AVt) * std::exp(V / AVt);

    // ── W₀(x) ─────────────────────────────────────────────────────────────
    const double w = lambertW(x);
    if (std::isnan(w))
        return kNaN; // x < -1/e — niefizyczne parametry

    // ── Prąd wynikowy ─────────────────────────────────────────────────────
    // I_lw = W₀(x) · (A·Vt) / Rₛ
    const double I_lw = w * AVt / Rs;

    // I = I_lw + (V − I_lw·Rₛ) / Rₛₕ
    return I_lw + (V - I_lw * Rs) / Rsh;
}

// ─── 6 parametrów ────────────────────────────────────────────────────────────

double evaluateDiodeIV6(double V, const double *params, double T) noexcept
{
    const double I0 = params[0];
    const double A = params[1];
    const double Rs = params[2];
    const double Rsh = params[3];
    const double alpha = params[4];
    const double Rsh2 = params[5];

    // ── Walidacja ─────────────────────────────────────────────────────────
    if (I0 <= 0.0 || A <= 0.0 ||
        Rsh <= 0.0 || Rs < 0.0 ||
        Rsh2 <= 0.0 || T <= 0.0)
        return kNaN;

    const double Vt = PhysConst::k * T;
    const double AVt = A * Vt;

    // ── I_lw — analogicznie do 4-param ───────────────────────────────────
    double I_lw;

    if (Rs == 0.0)
    {
        I_lw = I0 * std::exp(V / AVt);
    }
    else
    {
        const double x = (I0 * Rs / AVt) * std::exp(V / AVt);
        const double w = lambertW(x);
        if (std::isnan(w))
            return kNaN;
        I_lw = w * AVt / Rs;
    }

    // ── Napięcie na złączu ────────────────────────────────────────────────
    const double Vj = V - I_lw * Rs;

    // ── Człon nieliniowy: Vⱼ^α / Rₛₕ₂ ───────────────────────────────────
    // UWAGA: pow(Vj, alpha) = NaN gdy Vj < 0 i alpha ∉ Z.
    // To jest fizycznie możliwe w zaporze (Vj < 0). Zwracamy NaN —
    // solver potraktuje to jako +∞ i odrzuci kandydata.
    const double power_term = std::pow(Vj, alpha);
    if (std::isnan(power_term))
        return kNaN;

    return I_lw + Vj / Rsh + power_term / Rsh2;
}