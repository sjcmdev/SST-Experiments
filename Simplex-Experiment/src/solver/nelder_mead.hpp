#pragma once

#include "solver_types.hpp"

#include <functional>
#include <random>
#include <string>
#include <vector>

class SANelderMead
{
public:
    using ObjectiveFn = std::function<double(const std::vector<double>&)>;

    SANelderMead(
        std::vector<FitParam> all_params,
        ObjectiveFn objective_fn,
        double alpha = 1.0,
        double gamma = 2.0,
        double rho = 0.5,
        double sigma = 0.5,
        double degenerate_tol = 1e-12,
        bool sa_enabled = false,
        SAConfig sa_config = SAConfig{},
        unsigned int rng_seed = 0,
        bool trace_enabled = false);

    static SANelderMead withSA(
        std::vector<FitParam> all_params,
        ObjectiveFn objective_fn,
        SAConfig sa_config,
        unsigned int rng_seed);

    void initSimplex();
    StepType step();
    FitResult runUntilConvergence(int max_iter, double chi2_tol);

    const SimplexState& state() const noexcept { return state_; }
    int iteration() const noexcept { return state_.iteration; }
    double bestChiSquared() const noexcept { return state_.chi2_values[state_.best_idx]; }
    std::vector<double> bestParams() const { return state_.vertices[state_.best_idx]; }
    std::vector<double> bestFullParams() const;

    void setTemperature(double T);
    void resetCooling();
    void setCoolingSchedule(CoolingSchedule schedule);

    double getTemperature() const noexcept { return sa_config_.T_current; }
    const SAConfig& getSAConfig() const noexcept { return sa_config_; }
    bool isSAEnabled() const noexcept { return sa_enabled_; }
    int getSAAcceptedCount() const noexcept { return sa_accepted_count_; }

    void setTraceEnabled(bool enabled) noexcept { trace_enabled_ = enabled; }
    bool isTraceEnabled() const noexcept { return trace_enabled_; }
    const std::vector<TraceStep>& getTrace() const noexcept { return trace_; }
    const TraceStep& getTraceStep(int index) const;
    int getTraceSize() const noexcept { return static_cast<int>(trace_.size()); }
    void clearTrace();

private:
    std::vector<FitParam> all_params_;
    std::vector<int> free_indices_;
    ObjectiveFn objective_fn_;

    double alpha_;
    double gamma_;
    double rho_;
    double sigma_;
    double degenerate_tol_;

    std::vector<double> free_min_;
    std::vector<double> free_max_;

    SimplexState state_;

    bool sa_enabled_ = false;
    SAConfig sa_config_;
    int cooling_k_ = 0;
    std::mt19937 rng_;
    std::uniform_real_distribution<double> uniform_dist_{0.0, 1.0};
    int sa_accepted_count_ = 0;

    bool trace_enabled_ = false;
    std::vector<TraceStep> trace_;

    void initSimplexAround(const std::vector<double>& start);
    void restart();
    bool isDegenerate() const;
    void clipToBounds(std::vector<double>& value) const;
    bool withinBounds(const std::vector<double>& value) const;
    double evaluateVertex(const std::vector<double>& free_values) const;
    void sortIndices(int& best, int& second_worst, int& worst) const;
    std::vector<double> computeCentroidExcluding(int exclude_idx) const;
    void shrinkSimplex(int best_idx);
    FitResult buildResult(bool converged, const std::string& stop_reason) const;

    void requireSAEnabled(const char* method_name) const;
    void validateSAConfig() const;
    double computeBoltzmannTemperature(int k) const;
    double computeGeometricTemperature(int k) const;
    void updateCooling();
    StepType stepInternal();
};

std::vector<double> reflectPoint(
    const std::vector<double>& centroid,
    const std::vector<double>& worst,
    double alpha);

std::vector<double> expandPoint(
    const std::vector<double>& centroid,
    const std::vector<double>& reflected,
    double gamma);

std::vector<double> contractPoint(
    const std::vector<double>& centroid,
    const std::vector<double>& worst,
    double rho);
