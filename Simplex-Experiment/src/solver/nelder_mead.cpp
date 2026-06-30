// src/solver/nelder_mead.cpp
#include "nelder_mead.hpp"
#include "param_utils.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace
{
    constexpr double kInf = std::numeric_limits<double>::infinity();
}

// ─── Operacje geometryczne ───────────────────────────────────────────────────

std::vector<double> reflectPoint(
    const std::vector<double> &centroid, const std::vector<double> &worst, double alpha)
{
    std::vector<double> result(centroid.size());
    for (size_t i = 0; i < centroid.size(); ++i)
        result[i] = centroid[i] + alpha * (centroid[i] - worst[i]);
    return result;
}

std::vector<double> expandPoint(
    const std::vector<double> &centroid, const std::vector<double> &x_r, double gamma)
{
    std::vector<double> result(centroid.size());
    for (size_t i = 0; i < centroid.size(); ++i)
        result[i] = centroid[i] + gamma * (x_r[i] - centroid[i]);
    return result;
}

std::vector<double> contractPoint(
    const std::vector<double> &centroid, const std::vector<double> &worst, double rho)
{
    std::vector<double> result(centroid.size());
    for (size_t i = 0; i < centroid.size(); ++i)
        result[i] = centroid[i] + rho * (worst[i] - centroid[i]);
    return result;
}

// ─── Konstruktor ─────────────────────────────────────────────────────────────

SANelderMead::SANelderMead(
    std::vector<FitParam> all_params,
    ObjectiveFn objective_fn,
    double alpha, double gamma, double rho, double sigma,
    double degenerate_tol)
    : all_params_(std::move(all_params)), objective_fn_(std::move(objective_fn)), alpha_(alpha), gamma_(gamma), rho_(rho), sigma_(sigma), degenerate_tol_(degenerate_tol)
{
    if (degenerate_tol_ <= 0.0)
        throw std::invalid_argument("SANelderMead: degenerate_tol musi być > 0");

    free_indices_ = freeIndices(all_params_);
    if (free_indices_.empty())
        throw std::invalid_argument("SANelderMead: brak wolnych parametrów (free=true)");

    extractFreeBounds(all_params_, free_indices_, free_min_, free_max_);

    for (size_t j = 0; j < free_indices_.size(); ++j)
    {
        const FitParam &p = all_params_[free_indices_[j]];
        if (!(p.min < p.max))
            throw std::invalid_argument(
                "SANelderMead: min >= max dla wolnego parametru '" + p.name + "'");
        if (p.value < p.min || p.value > p.max)
            throw std::invalid_argument(
                "SANelderMead: wartość startowa poza granicami dla parametru '" + p.name + "'");
    }
}

// ─── Inicjalizacja simpleksu ─────────────────────────────────────────────────

void SANelderMead::initSimplex()
{
    initSimplexAround(extractFreeValues(all_params_, free_indices_));
}

void SANelderMead::initSimplexAround(const std::vector<double> &start)
{
    const int N_free = static_cast<int>(free_indices_.size());

    state_.vertices.assign(N_free + 1, start);
    state_.chi2_values.assign(N_free + 1, 0.0);

    // Wierzchołek 0 = punkt startowy bez przesunięcia.
    // Wierzchołki 1..N_free = start z przesunięciem o 5% zakresu w JEDNYM wymiarze.
    for (int i = 1; i <= N_free; ++i)
    {
        const int dim = i - 1;
        const double range = free_max_[dim] - free_min_[dim];
        const double delta = 0.05 * range;

        double candidate = start[dim] + delta;
        if (candidate > free_max_[dim])
            candidate = start[dim] - delta; // odbij, gdy +delta przekracza górną granicę

        state_.vertices[i][dim] = candidate;
        clipToBounds(state_.vertices[i]); // siatka bezpieczeństwa
    }

    for (int i = 0; i <= N_free; ++i)
        state_.chi2_values[i] = evaluateVertex(state_.vertices[i]);

    int best, second_worst, worst;
    sortIndices(best, second_worst, worst);
    state_.best_idx = best;
    state_.worst_idx = worst;
}

// ─── Krok algorytmu ──────────────────────────────────────────────────────────

StepType SANelderMead::step()
{
    if (state_.vertices.empty())
        throw std::logic_error("SANelderMead::step() wywołane przed initSimplex()");

    if (isDegenerate())
    {
        restart();
        state_.iteration++;
        return StepType::Restart;
    }

    int best, second_worst, worst;
    sortIndices(best, second_worst, worst);

    const double f_best = state_.chi2_values[best];
    const double f_second_worst = state_.chi2_values[second_worst];
    const double f_worst = state_.chi2_values[worst];

    const std::vector<double> centroid = computeCentroidExcluding(worst);

    // ── Reflection ────────────────────────────────────────────────────────
    std::vector<double> x_r = reflectPoint(centroid, state_.vertices[worst], alpha_);
    clipToBounds(x_r);
    const double f_r = evaluateVertex(x_r);

    StepType applied;

    if (f_r < f_best)
    {
        // ── Spróbuj Expansion ───────────────────────────────────────────
        std::vector<double> x_e = expandPoint(centroid, x_r, gamma_);
        clipToBounds(x_e);
        const double f_e = evaluateVertex(x_e);

        if (f_e < f_r)
        {
            state_.vertices[worst] = x_e;
            state_.chi2_values[worst] = f_e;
            applied = StepType::Expansion;
        }
        else
        {
            state_.vertices[worst] = x_r;
            state_.chi2_values[worst] = f_r;
            applied = StepType::Reflection;
        }
    }
    else if (f_r < f_second_worst)
    {
        state_.vertices[worst] = x_r;
        state_.chi2_values[worst] = f_r;
        applied = StepType::Reflection;
    }
    else
    {
        // f_r >= f_second_worst → reflection nie wystarczająco dobra do przyjęcia
        // wprost. Cała ta gałąź (zarówno f_second_worst<=f_r<f_worst jak i
        // f_r>=f_worst) prowadzi do TEJ SAMEJ formuły kontrakcji — patrz
        // sekcję "Decyzje architektoniczne" (C) w dokumencie planu Etapu 2.
        //
        // ETAP 3 HOOK: dokładnie w pod-przypadku f_r >= f_worst Etap 3 wstawi
        // tutaj próbę akceptacji Metropolis: P = exp(-(f_r-f_worst)/T_current),
        // PRZED wywołaniem kontrakcji poniżej. Gdy T_current==0 (stan Etapu 2)
        // → P==0 zawsze → kod kontrakcji/shrink poniżej bez zmian.

        std::vector<double> x_c = contractPoint(centroid, state_.vertices[worst], rho_);
        clipToBounds(x_c);
        const double f_c = evaluateVertex(x_c);

        if (f_c < f_worst)
        {
            state_.vertices[worst] = x_c;
            state_.chi2_values[worst] = f_c;
            applied = StepType::Contraction;
        }
        else
        {
            shrinkSimplex(best);
            applied = StepType::Shrink;
        }
    }

    int new_best, new_second_worst, new_worst;
    sortIndices(new_best, new_second_worst, new_worst);
    state_.best_idx = new_best;
    state_.worst_idx = new_worst;
    state_.iteration++;

    return applied;
}

// ─── Pętla zbieżności ────────────────────────────────────────────────────────

FitResult SANelderMead::runUntilConvergence(int max_iter, double chi2_tol)
{
    for (int it = 0; it < max_iter; ++it)
    {
        step();
        if (state_.chi2_values[state_.best_idx] < chi2_tol)
            return buildResult(true, "tol");
    }
    return buildResult(false, "max_iter");
}

FitResult SANelderMead::buildResult(bool converged, const std::string &stop_reason) const
{
    FitResult r;
    r.best_params = state_.vertices[state_.best_idx];
    r.chi2_min = state_.chi2_values[state_.best_idx];
    r.delta_chi2 = 0.0; // brak zewnętrznej referencji w Etapie 2 — patrz pułapki
    r.iterations = state_.iteration;
    r.converged = converged;
    r.stop_reason = stop_reason;
    return r;
}

std::vector<double> SANelderMead::bestFullParams() const
{
    return buildFullParams(bestParams(), all_params_, free_indices_);
}

// ─── Pomocnicze prywatne ──────────────────────────────────────────────────────

void SANelderMead::sortIndices(int &best, int &second_worst, int &worst) const
{
    const int n = static_cast<int>(state_.chi2_values.size()); // = N_free+1, zawsze >= 2
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [this](int a, int b)
              { return state_.chi2_values[a] < state_.chi2_values[b]; });
    best = order.front();
    worst = order.back();
    second_worst = order[n - 2]; // bezpieczne: n>=2 (konstruktor wymaga N_free>=1)
}

std::vector<double> SANelderMead::computeCentroidExcluding(int exclude_idx) const
{
    const int N_free = static_cast<int>(free_indices_.size());
    std::vector<double> centroid(N_free, 0.0);
    int count = 0;
    for (size_t i = 0; i < state_.vertices.size(); ++i)
    {
        if (static_cast<int>(i) == exclude_idx)
            continue;
        for (int j = 0; j < N_free; ++j)
            centroid[j] += state_.vertices[i][j];
        ++count;
    }
    for (int j = 0; j < N_free; ++j)
        centroid[j] /= count;
    return centroid;
}

void SANelderMead::shrinkSimplex(int best_idx)
{
    const std::vector<double> best_vertex = state_.vertices[best_idx]; // kopia — best się nie zmienia
    for (size_t i = 0; i < state_.vertices.size(); ++i)
    {
        if (static_cast<int>(i) == best_idx)
            continue;
        for (size_t j = 0; j < state_.vertices[i].size(); ++j)
            state_.vertices[i][j] = best_vertex[j] + sigma_ * (state_.vertices[i][j] - best_vertex[j]);

        clipToBounds(state_.vertices[i]);
        state_.chi2_values[i] = evaluateVertex(state_.vertices[i]);
    }
}

bool SANelderMead::isDegenerate() const
{
    double max_dist_sq = 0.0;
    const size_t n = state_.vertices.size();
    for (size_t i = 0; i < n; ++i)
    {
        for (size_t j = i + 1; j < n; ++j)
        {
            double d2 = 0.0;
            for (size_t k = 0; k < state_.vertices[i].size(); ++k)
            {
                const double d = state_.vertices[i][k] - state_.vertices[j][k];
                d2 += d * d;
            }
            max_dist_sq = std::max(max_dist_sq, d2);
        }
    }
    return std::sqrt(max_dist_sq) < degenerate_tol_;
}

void SANelderMead::restart()
{
    const std::vector<double> best_point = state_.vertices[state_.best_idx];
    initSimplexAround(best_point);
}

void SANelderMead::clipToBounds(std::vector<double> &v) const
{
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (std::isnan(v[i]))
            continue; // NaN zostaje — złapie je withinBounds()
        v[i] = std::clamp(v[i], free_min_[i], free_max_[i]);
    }
}

bool SANelderMead::withinBounds(const std::vector<double> &v) const
{
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (std::isnan(v[i]))
            return false;
        if (v[i] < free_min_[i] || v[i] > free_max_[i])
            return false;
    }
    return true;
}

double SANelderMead::evaluateVertex(const std::vector<double> &free_values) const
{
    // Defensywny double-check — patrz pułapki Etapu 1: po clipToBounds
    // powinno zawsze być true, ale NaN przechodzi przez std::clamp w
    // sposób niezdefiniowany (porównania z NaN zawsze false).
    if (!withinBounds(free_values))
        return kInf;
    const double value = objective_fn_(free_values);
    return std::isfinite(value) ? value : kInf;
}
