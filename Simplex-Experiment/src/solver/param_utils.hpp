// src/solver/param_utils.hpp
#pragma once
#include "solver_types.hpp"
#include <vector>

/// Zwraca indeksy (w all_params) parametrów z free==true, w kolejności
/// występowania w all_params. len(wynik) = N_free.
std::vector<int> freeIndices(const std::vector<FitParam> &all_params);

/// Buduje PEŁNY wektor parametrów (len = all_params.size()), podstawiając
/// free_values pod indeksy free_indices, a pozostałe biorąc z all_params[i].value.
///
/// Precondition: free_values.size() == free_indices.size()
std::vector<double> buildFullParams(
    const std::vector<double> &free_values,
    const std::vector<FitParam> &all_params,
    const std::vector<int> &free_indices);

/// Wyciąga aktualne wartości (FitParam::value) TYLKO dla wolnych parametrów,
/// w kolejności free_indices. Użyteczne jako punkt startowy simpleksu.
std::vector<double> extractFreeValues(
    const std::vector<FitParam> &all_params,
    const std::vector<int> &free_indices);

/// Wyciąga granice [min,max] TYLKO dla wolnych parametrów, w kolejności
/// free_indices.
void extractFreeBounds(
    const std::vector<FitParam> &all_params,
    const std::vector<int> &free_indices,
    std::vector<double> &out_min,
    std::vector<double> &out_max);