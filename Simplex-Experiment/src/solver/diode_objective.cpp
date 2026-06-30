// src/solver/diode_objective.cpp
#include "diode_objective.hpp"
#include "param_utils.hpp"
#include "objective.hpp"

#include <stdexcept>

SANelderMead::ObjectiveFn makeDiodeIVObjective(
    std::vector<FitParam> all_params,
    std::function<double(double, const double *)> model_fn,
    std::vector<double> V_data,
    std::vector<double> I_meas,
    std::vector<double> sigma)
{
    if (V_data.size() != I_meas.size() || V_data.size() != sigma.size())
        throw std::invalid_argument(
            "makeDiodeIVObjective: V_data, I_meas, sigma muszą mieć tę samą długość");

    const auto free_idx = freeIndices(all_params);
    const int N_points = static_cast<int>(V_data.size());

    // Capture-by-value: bezpieczne przy dowolnym czasie życia wynikowej
    // lambdy, kosztem kopii danych. Akceptowalne — projekt nie jest
    // ograniczony pamięciowo (patrz notatki Etapu 1).
    return [all_params, model_fn, V_data, I_meas, sigma, free_idx, N_points](const std::vector<double> &free_values) -> double
    {
        const std::vector<double> full =
            buildFullParams(free_values, all_params, free_idx);
        return computeChiSquared(full.data(), V_data.data(), I_meas.data(),
                                 sigma.data(), N_points, model_fn);
    };
}