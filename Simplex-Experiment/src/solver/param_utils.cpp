// src/solver/param_utils.cpp
#include "param_utils.hpp"

std::vector<int> freeIndices(const std::vector<FitParam> &all_params)
{
    std::vector<int> result;
    for (size_t i = 0; i < all_params.size(); ++i)
        if (all_params[i].free)
            result.push_back(static_cast<int>(i));
    return result;
}

std::vector<double> buildFullParams(
    const std::vector<double> &free_values,
    const std::vector<FitParam> &all_params,
    const std::vector<int> &free_indices)
{
    std::vector<double> full(all_params.size());
    for (size_t i = 0; i < all_params.size(); ++i)
        full[i] = all_params[i].value;
    for (size_t j = 0; j < free_indices.size(); ++j)
        full[free_indices[j]] = free_values[j];
    return full;
}

std::vector<double> extractFreeValues(
    const std::vector<FitParam> &all_params,
    const std::vector<int> &free_indices)
{
    std::vector<double> result(free_indices.size());
    for (size_t j = 0; j < free_indices.size(); ++j)
        result[j] = all_params[free_indices[j]].value;
    return result;
}

void extractFreeBounds(
    const std::vector<FitParam> &all_params,
    const std::vector<int> &free_indices,
    std::vector<double> &out_min,
    std::vector<double> &out_max)
{
    out_min.resize(free_indices.size());
    out_max.resize(free_indices.size());
    for (size_t j = 0; j < free_indices.size(); ++j)
    {
        out_min[j] = all_params[free_indices[j]].min;
        out_max[j] = all_params[free_indices[j]].max;
    }
}