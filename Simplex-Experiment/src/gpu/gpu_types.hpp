#pragma once

enum class GpuModelType : int
{
    Diode4P = 0,
    Diode6P = 1
};

struct GpuParamLayout
{
    static constexpr int MAX_PARAMS = 8;
    int n_total = 0;
    int n_free = 0;
    int free_to_total[MAX_PARAMS] = {};
    double fixed_values[MAX_PARAMS] = {};
    double min_bounds[MAX_PARAMS] = {};
    double max_bounds[MAX_PARAMS] = {};
    double T = 300.0;
    int dof = 1;
};

struct GpuNmConfig
{
    double alpha = 1.0;
    double gamma = 2.0;
    double rho = 0.5;
    double sigma_shrink = 0.5;
    double degenerate_tol = 1e-12;
    int max_iter = 5000;
    double reduced_chi2_tol = 1.0;
    bool sa_enabled = false;
    double sa_T_initial = 0.0;
    double sa_geometric_rate = 0.99;
};

struct GpuMcResult
{
    static constexpr int MAX_FREE = 8;
    double best_free_params[MAX_FREE] = {};
    double chi2_min = 0.0;
    double reduced_chi2_min = 0.0;
    double ref_reduced_chi2 = 0.0;
    double delta_reduced_chi2 = 0.0;
    int iterations = 0;
    int converged = 0;
    int n_free = 0;
};

struct GpuSimplexStepResult
{
    static constexpr int MAX_FREE = 8;
    static constexpr int MAX_VERTICES = 9;
    double vertices[MAX_VERTICES][MAX_FREE] = {};
    double chi2[MAX_VERTICES] = {};
    int n_free = 0;
    int n_vertices = 0;
    int best_idx = 0;
    int worst_idx = 0;
    int step_type = 0;
    int iteration = 0;
};

