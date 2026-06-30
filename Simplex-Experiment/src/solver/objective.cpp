// src/solver/objective.cpp
#include "objective.hpp"
#include <cmath>
#include <limits>

double computeChiSquared(
    const double *params,
    const double *V_data,
    const double *I_meas,
    const double *sigma,
    int N_points,
    std::function<double(double, const double *)> model_fn) noexcept
{
    constexpr double kInf = std::numeric_limits<double>::infinity();

    double chi2 = 0.0;

    for (int i = 0; i < N_points; ++i)
    {
        // Walidacja sigma — musi być dodatnia
        if (sigma[i] <= 0.0)
            return kInf;

        const double I_model = model_fn(V_data[i], params);

        // NaN lub Inf z modelu → kandydat niefizyczny → odrzucamy go (+∞)
        // Solver widzi +∞ i nie crashuje
        if (!std::isfinite(I_model))
            return kInf;

        const double residual = (I_meas[i] - I_model) / sigma[i];
        chi2 += residual * residual;
    }

    return chi2;
}