#include "nelder_mead.hpp"

#include "param_utils.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace
{
constexpr double kInf = std::numeric_limits<double>::infinity();
}

std::vector<double> reflectPoint(
    const std::vector<double>& centroid,
    const std::vector<double>& worst,
    double alpha)
{
    std::vector<double> result(centroid.size());
    for (size_t i = 0; i < centroid.size(); ++i)
        result[i] = centroid[i] + alpha * (centroid[i] - worst[i]);
    return result;
}

std::vector<double> expandPoint(
    const std::vector<double>& centroid,
    const std::vector<double>& reflected,
    double gamma)
{
    std::vector<double> result(centroid.size());
    for (size_t i = 0; i < centroid.size(); ++i)
        result[i] = centroid[i] + gamma * (reflected[i] - centroid[i]);
    return result;
}

std::vector<double> contractPoint(
    const std::vector<double>& centroid,
    const std::vector<double>& worst,
    double rho)
{
    std::vector<double> result(centroid.size());
    for (size_t i = 0; i < centroid.size(); ++i)
        result[i] = centroid[i] + rho * (worst[i] - centroid[i]);
    return result;
}

SANelderMead::SANelderMead(
    std::vector<FitParam> all_params,
    ObjectiveFn objective_fn,
    double alpha,
    double gamma,
    double rho,
    double sigma,
    double degenerate_tol,
    bool sa_enabled,
    SAConfig sa_config,
    unsigned int rng_seed,
    bool trace_enabled)
    : all_params_(std::move(all_params))
    , objective_fn_(std::move(objective_fn))
    , alpha_(alpha)
    , gamma_(gamma)
    , rho_(rho)
    , sigma_(sigma)
    , degenerate_tol_(degenerate_tol)
    , sa_enabled_(sa_enabled)
    , sa_config_(sa_config)
    , rng_(rng_seed)
    , trace_enabled_(trace_enabled)
{
    if (degenerate_tol_ <= 0.0)
        throw std::invalid_argument("SANelderMead: degenerate_tol must be > 0");

    free_indices_ = freeIndices(all_params_);
    if (free_indices_.empty())
        throw std::invalid_argument("SANelderMead: no free parameters");

    extractFreeBounds(all_params_, free_indices_, free_min_, free_max_);

    for (size_t j = 0; j < free_indices_.size(); ++j)
    {
        const FitParam& param = all_params_[free_indices_[j]];
        if (!(param.min < param.max))
            throw std::invalid_argument("SANelderMead: min >= max for free parameter '" + param.name + "'");
        if (param.value < param.min || param.value > param.max)
            throw std::invalid_argument("SANelderMead: start value outside bounds for parameter '" + param.name + "'");
    }

    if (sa_enabled_)
    {
        validateSAConfig();
        resetCooling();
    }
    else
    {
        sa_config_.T_current = 0.0;
        state_.T_current = 0.0;
    }
}

SANelderMead SANelderMead::withSA(
    std::vector<FitParam> all_params,
    ObjectiveFn objective_fn,
    SAConfig sa_config,
    unsigned int rng_seed)
{
    return SANelderMead(std::move(all_params), std::move(objective_fn), 1.0, 2.0, 0.5, 0.5, 1e-12, true, sa_config, rng_seed);
}

void SANelderMead::initSimplex()
{
    initSimplexAround(extractFreeValues(all_params_, free_indices_));
}

void SANelderMead::initSimplexAround(const std::vector<double>& start)
{
    const int freeCount = static_cast<int>(free_indices_.size());

    state_.vertices.assign(freeCount + 1, start);
    state_.chi2_values.assign(freeCount + 1, 0.0);

    for (int i = 1; i <= freeCount; ++i)
    {
        const int dim = i - 1;
        const double range = free_max_[dim] - free_min_[dim];
        const double delta = 0.05 * range;

        double candidate = start[dim] + delta;
        if (candidate > free_max_[dim])
            candidate = start[dim] - delta;

        state_.vertices[i][dim] = candidate;
        clipToBounds(state_.vertices[i]);
    }

    for (int i = 0; i <= freeCount; ++i)
        state_.chi2_values[i] = evaluateVertex(state_.vertices[i]);

    int best = 0;
    int secondWorst = 0;
    int worst = 0;
    sortIndices(best, secondWorst, worst);
    state_.best_idx = best;
    state_.worst_idx = worst;
    state_.centroid = computeCentroidExcluding(worst);
    state_.T_current = sa_config_.T_current;
}

StepType SANelderMead::step()
{
    if (!trace_enabled_)
        return stepInternal();

    const SimplexState stateBefore = state_;
    const StepType type = stepInternal();

    TraceStep traceStep;
    traceStep.type = type;
    traceStep.state_before = stateBefore;
    traceStep.state_after = state_;
    traceStep.chi2_min = traceStep.state_after.chi2_values[traceStep.state_after.best_idx];
    traceStep.T = stateBefore.T_current;
    traceStep.iteration = state_.iteration;
    trace_.push_back(std::move(traceStep));

    return type;
}

StepType SANelderMead::stepInternal()
{
    if (state_.vertices.empty())
        throw std::logic_error("SANelderMead::step called before initSimplex");

    if (isDegenerate())
    {
        restart();
        ++state_.iteration;
        updateCooling();
        return StepType::Restart;
    }

    int best = 0;
    int secondWorst = 0;
    int worst = 0;
    sortIndices(best, secondWorst, worst);

    const double fBest = state_.chi2_values[best];
    const double fSecondWorst = state_.chi2_values[secondWorst];
    const double fWorst = state_.chi2_values[worst];

    const std::vector<double> centroid = computeCentroidExcluding(worst);
    state_.centroid = centroid;

    std::vector<double> reflected = reflectPoint(centroid, state_.vertices[worst], alpha_);
    clipToBounds(reflected);
    const double fReflected = evaluateVertex(reflected);

    StepType applied = StepType::Reflection;

    if (fReflected < fBest)
    {
        std::vector<double> expanded = expandPoint(centroid, reflected, gamma_);
        clipToBounds(expanded);
        const double fExpanded = evaluateVertex(expanded);

        if (fExpanded < fReflected)
        {
            state_.vertices[worst] = expanded;
            state_.chi2_values[worst] = fExpanded;
            applied = StepType::Expansion;
        }
        else
        {
            state_.vertices[worst] = reflected;
            state_.chi2_values[worst] = fReflected;
            applied = StepType::Reflection;
        }
    }
    else if (fReflected < fSecondWorst)
    {
        state_.vertices[worst] = reflected;
        state_.chi2_values[worst] = fReflected;
        applied = StepType::Reflection;
    }
    else
    {
        bool acceptedBySA = false;
        if (sa_enabled_ && fReflected >= fWorst && sa_config_.T_current > 0.0)
        {
            const double delta = fReflected - fWorst;
            if (std::isfinite(delta) && delta >= 0.0)
            {
                const double probability = std::exp(-delta / sa_config_.T_current);
                if (uniform_dist_(rng_) < probability)
                {
                    state_.vertices[worst] = reflected;
                    state_.chi2_values[worst] = fReflected;
                    ++sa_accepted_count_;
                    applied = StepType::Reflection;
                    acceptedBySA = true;
                }
            }
        }

        if (!acceptedBySA)
        {
            std::vector<double> contracted = contractPoint(centroid, state_.vertices[worst], rho_);
            clipToBounds(contracted);
            const double fContracted = evaluateVertex(contracted);

            if (fContracted < fWorst)
            {
                state_.vertices[worst] = contracted;
                state_.chi2_values[worst] = fContracted;
                applied = StepType::Contraction;
            }
            else
            {
                shrinkSimplex(best);
                applied = StepType::Shrink;
            }
        }
    }

    int newBest = 0;
    int newSecondWorst = 0;
    int newWorst = 0;
    sortIndices(newBest, newSecondWorst, newWorst);
    state_.best_idx = newBest;
    state_.worst_idx = newWorst;
    state_.centroid = computeCentroidExcluding(newWorst);
    ++state_.iteration;
    updateCooling();

    return applied;
}

FitResult SANelderMead::runUntilConvergence(int max_iter, double chi2_tol)
{
    for (int iter = 0; iter < max_iter; ++iter)
    {
        step();
        if (state_.chi2_values[state_.best_idx] < chi2_tol)
            return buildResult(true, "tol");
    }
    return buildResult(false, "max_iter");
}

void SANelderMead::setTemperature(double temperature)
{
    requireSAEnabled("setTemperature");
    if (temperature < 0.0)
        throw std::invalid_argument("SANelderMead::setTemperature: temperature must be >= 0");
    sa_config_.T_current = temperature;
    state_.T_current = temperature;
}

void SANelderMead::resetCooling()
{
    requireSAEnabled("resetCooling");
    validateSAConfig();
    cooling_k_ = 0;
    sa_config_.T_current = sa_config_.T_initial;
    state_.T_current = sa_config_.T_current;
}

void SANelderMead::setCoolingSchedule(CoolingSchedule schedule)
{
    requireSAEnabled("setCoolingSchedule");
    sa_config_.schedule = schedule;
    validateSAConfig();
}

const TraceStep& SANelderMead::getTraceStep(int index) const
{
    if (index < 0 || index >= static_cast<int>(trace_.size()))
        throw std::out_of_range("SANelderMead::getTraceStep: index out of range");
    return trace_[static_cast<size_t>(index)];
}

void SANelderMead::clearTrace()
{
    trace_.clear();
    trace_.shrink_to_fit();
}

FitResult SANelderMead::buildResult(bool converged, const std::string& stop_reason) const
{
    FitResult result;
    result.best_params = state_.vertices[state_.best_idx];
    result.chi2_min = state_.chi2_values[state_.best_idx];
    result.delta_chi2 = 0.0;
    result.iterations = state_.iteration;
    result.converged = converged;
    result.stop_reason = stop_reason;
    return result;
}

std::vector<double> SANelderMead::bestFullParams() const
{
    return buildFullParams(bestParams(), all_params_, free_indices_);
}

void SANelderMead::sortIndices(int& best, int& second_worst, int& worst) const
{
    const int count = static_cast<int>(state_.chi2_values.size());
    std::vector<int> order(count);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [this](int a, int b) {
        return state_.chi2_values[a] < state_.chi2_values[b];
    });
    best = order.front();
    worst = order.back();
    second_worst = order[count - 2];
}

std::vector<double> SANelderMead::computeCentroidExcluding(int exclude_idx) const
{
    const int freeCount = static_cast<int>(free_indices_.size());
    std::vector<double> centroid(freeCount, 0.0);
    int count = 0;
    for (size_t i = 0; i < state_.vertices.size(); ++i)
    {
        if (static_cast<int>(i) == exclude_idx)
            continue;
        for (int j = 0; j < freeCount; ++j)
            centroid[j] += state_.vertices[i][j];
        ++count;
    }
    for (int j = 0; j < freeCount; ++j)
        centroid[j] /= static_cast<double>(count);
    return centroid;
}

void SANelderMead::shrinkSimplex(int best_idx)
{
    const std::vector<double> bestVertex = state_.vertices[best_idx];
    for (size_t i = 0; i < state_.vertices.size(); ++i)
    {
        if (static_cast<int>(i) == best_idx)
            continue;
        for (size_t j = 0; j < state_.vertices[i].size(); ++j)
            state_.vertices[i][j] = bestVertex[j] + sigma_ * (state_.vertices[i][j] - bestVertex[j]);

        clipToBounds(state_.vertices[i]);
        state_.chi2_values[i] = evaluateVertex(state_.vertices[i]);
    }
}

bool SANelderMead::isDegenerate() const
{
    double maxDistanceSquared = 0.0;
    for (size_t i = 0; i < state_.vertices.size(); ++i)
    {
        for (size_t j = i + 1; j < state_.vertices.size(); ++j)
        {
            double distanceSquared = 0.0;
            for (size_t k = 0; k < state_.vertices[i].size(); ++k)
            {
                const double delta = state_.vertices[i][k] - state_.vertices[j][k];
                distanceSquared += delta * delta;
            }
            maxDistanceSquared = std::max(maxDistanceSquared, distanceSquared);
        }
    }
    return std::sqrt(maxDistanceSquared) < degenerate_tol_;
}

void SANelderMead::restart()
{
    const std::vector<double> bestPoint = state_.vertices[state_.best_idx];
    initSimplexAround(bestPoint);
}

void SANelderMead::clipToBounds(std::vector<double>& value) const
{
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (std::isnan(value[i]))
            continue;
        value[i] = std::clamp(value[i], free_min_[i], free_max_[i]);
    }
}

bool SANelderMead::withinBounds(const std::vector<double>& value) const
{
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (std::isnan(value[i]))
            return false;
        if (value[i] < free_min_[i] || value[i] > free_max_[i])
            return false;
    }
    return true;
}

double SANelderMead::evaluateVertex(const std::vector<double>& free_values) const
{
    if (!withinBounds(free_values))
        return kInf;
    const double value = objective_fn_(free_values);
    return std::isfinite(value) ? value : kInf;
}

void SANelderMead::requireSAEnabled(const char* method_name) const
{
    if (!sa_enabled_)
        throw std::logic_error(std::string("SANelderMead::") + method_name + " called while SA is disabled");
}

void SANelderMead::validateSAConfig() const
{
    if (sa_config_.T_initial < 0.0)
        throw std::invalid_argument("SANelderMead: SA T_initial must be >= 0");
    if (sa_config_.schedule == CoolingSchedule::Geometric &&
        !(sa_config_.geometric_rate > 0.0 && sa_config_.geometric_rate < 1.0))
        throw std::invalid_argument("SANelderMead: geometric_rate must be in (0, 1)");
}

double SANelderMead::computeBoltzmannTemperature(int k) const
{
    return sa_config_.T_initial / std::log(1.0 + static_cast<double>(k));
}

double SANelderMead::computeGeometricTemperature(int k) const
{
    return sa_config_.T_initial * std::pow(sa_config_.geometric_rate, static_cast<double>(k));
}

void SANelderMead::updateCooling()
{
    if (!sa_enabled_)
    {
        state_.T_current = 0.0;
        return;
    }

    if (sa_config_.T_current <= 0.0)
    {
        state_.T_current = sa_config_.T_current;
        return;
    }

    ++cooling_k_;

    switch (sa_config_.schedule)
    {
    case CoolingSchedule::Boltzmann:
        sa_config_.T_current = computeBoltzmannTemperature(cooling_k_);
        break;
    case CoolingSchedule::Geometric:
        sa_config_.T_current = computeGeometricTemperature(cooling_k_);
        break;
    case CoolingSchedule::Adaptive:
        sa_config_.T_current = computeBoltzmannTemperature(cooling_k_);
        break;
    }

    state_.T_current = sa_config_.T_current;
}
