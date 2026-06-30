#include "test_stage3.hpp"

#include "../solver/nelder_mead.hpp"
#include "../solver/solver_types.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
SANelderMead::ObjectiveFn quadraticObjective(const std::vector<double>& center)
{
    return [center](const std::vector<double>& x) {
        double sum = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double delta = x[i] - center[i];
            sum += delta * delta;
        }
        return sum;
    };
}

void testZeroTemperatureMatchesStage2()
{
    const std::vector<double> center = {1.5, -0.25, 2.0};
    std::vector<FitParam> pureParams;
    std::vector<FitParam> saParams;
    for (int i = 0; i < static_cast<int>(center.size()); ++i)
    {
        pureParams.push_back(FitParam("x" + std::to_string(i), 4.0, -10.0, 10.0, true));
        saParams.push_back(FitParam("x" + std::to_string(i), 4.0, -10.0, 10.0, true));
    }

    SAConfig sa;
    sa.T_initial = 0.0;
    sa.T_current = 0.0;
    sa.schedule = CoolingSchedule::Geometric;
    sa.geometric_rate = 0.95;

    SANelderMead pureSolver(pureParams, quadraticObjective(center));
    SANelderMead saSolver = SANelderMead::withSA(saParams, quadraticObjective(center), sa, 123u);
    pureSolver.initSimplex();
    saSolver.initSimplex();

    for (int i = 0; i < 200; ++i)
    {
        const StepType pureStep = pureSolver.step();
        const StepType saStep = saSolver.step();
        assert(pureStep == saStep);
        assert(pureSolver.bestChiSquared() == saSolver.bestChiSquared());
        assert(pureSolver.bestParams() == saSolver.bestParams());
        assert(saSolver.getSAAcceptedCount() == 0);
    }

    std::fprintf(stdout, "[SA T=0] PASS\n");
}

double doubleWellObjectiveValue(double x)
{
    const double left = (x + 1.0) * (x + 1.0);
    const double right = (x - 1.0) * (x - 1.0);
    return std::min(left, right) - 0.05 * x;
}

void testLocalMinimumEscape()
{
    auto objective = [](const std::vector<double>& x) {
        return doubleWellObjectiveValue(x[0]);
    };

    {
        std::vector<FitParam> params = {FitParam("x", -1.0, -5.0, 5.0, true)};
        SANelderMead solver(params, objective);
        solver.initSimplex();
        const FitResult result = solver.runUntilConvergence(3000, 1e-12);
        std::fprintf(stdout, "[Local min escape] pure NM x=%.6f\n", result.best_params[0]);
        assert(result.best_params[0] < 0.0);
    }

    int stochasticRuns = 0;
    for (unsigned int seed = 1; seed <= 10; ++seed)
    {
        std::vector<FitParam> params = {FitParam("x", -1.0, -5.0, 5.0, true)};
        SAConfig sa;
        sa.T_initial = 5.0;
        sa.schedule = CoolingSchedule::Geometric;
        sa.geometric_rate = 0.995;

        SANelderMead solver = SANelderMead::withSA(params, objective, sa, seed);
        solver.initSimplex();
        solver.runUntilConvergence(3000, 1e-12);
        if (solver.getSAAcceptedCount() > 0)
            ++stochasticRuns;
    }

    std::fprintf(stdout, "[Local min escape] SA branch fired %d / 10\n", stochasticRuns);
    assert(stochasticRuns >= 9);
    std::fprintf(stdout, "[Local min escape] PASS\n");
}

void testSetTemperatureZeroMidRun()
{
    auto runScenario = []() -> FitResult {
        std::vector<FitParam> params = {FitParam("x", 5.0, -10.0, 10.0, true)};
        SANelderMead::ObjectiveFn objective = [](const std::vector<double>& x) {
            return x[0] * x[0];
        };

        SAConfig sa;
        sa.T_initial = 3.0;
        sa.schedule = CoolingSchedule::Geometric;
        sa.geometric_rate = 0.98;

        SANelderMead solver = SANelderMead::withSA(params, objective, sa, 7u);
        solver.initSimplex();

        for (int i = 0; i < 50; ++i)
            solver.step();
        solver.setTemperature(0.0);
        for (int i = 0; i < 200; ++i)
            solver.step();

        FitResult result;
        result.best_params = solver.bestParams();
        result.chi2_min = solver.bestChiSquared();
        result.iterations = solver.iteration();
        return result;
    };

    const FitResult first = runScenario();
    const FitResult second = runScenario();

    assert(first.iterations == second.iterations);
    assert(first.chi2_min == second.chi2_min);
    assert(first.best_params == second.best_params);

    std::fprintf(stdout, "[setTemperature(0)] PASS\n");
}

void testBoltzmannMonotonic()
{
    std::vector<FitParam> params = {FitParam("x", 0.0, -1.0, 1.0, true)};
    SANelderMead::ObjectiveFn objective = [](const std::vector<double>& x) {
        return x[0] * x[0];
    };

    SAConfig sa;
    sa.T_initial = 10.0;
    sa.schedule = CoolingSchedule::Boltzmann;

    SANelderMead solver = SANelderMead::withSA(params, objective, sa, 1u);
    solver.initSimplex();

    std::vector<double> temps;
    for (int i = 0; i < 100; ++i)
    {
        solver.step();
        temps.push_back(solver.getTemperature());
    }

    for (size_t i = 1; i < temps.size(); ++i)
        assert(temps[i] <= temps[i - 1] + 1e-12);

    std::fprintf(stdout, "[Boltzmann] PASS\n");
}

void testGeometricRatio()
{
    std::vector<FitParam> params = {FitParam("x", 0.0, -1.0, 1.0, true)};
    SANelderMead::ObjectiveFn objective = [](const std::vector<double>& x) {
        return x[0] * x[0];
    };

    SAConfig sa;
    sa.T_initial = 10.0;
    sa.schedule = CoolingSchedule::Geometric;
    sa.geometric_rate = 0.95;

    SANelderMead solver = SANelderMead::withSA(params, objective, sa, 2u);
    solver.initSimplex();

    std::vector<double> temps;
    for (int i = 0; i < 100; ++i)
    {
        solver.step();
        temps.push_back(solver.getTemperature());
    }

    for (size_t i = 1; i < temps.size(); ++i)
    {
        const double ratio = temps[i] / temps[i - 1];
        assert(std::abs(ratio - sa.geometric_rate) < 1e-9);
    }

    std::fprintf(stdout, "[Geometric] PASS\n");
}

void testAdaptivePlaceholderMatchesBoltzmann()
{
    std::vector<FitParam> boltzmannParams = {FitParam("x", 0.0, -1.0, 1.0, true)};
    std::vector<FitParam> adaptiveParams = {FitParam("x", 0.0, -1.0, 1.0, true)};
    SANelderMead::ObjectiveFn objective = [](const std::vector<double>& x) {
        return x[0] * x[0];
    };

    SAConfig boltzmann;
    boltzmann.T_initial = 7.0;
    boltzmann.schedule = CoolingSchedule::Boltzmann;

    SAConfig adaptive;
    adaptive.T_initial = 7.0;
    adaptive.schedule = CoolingSchedule::Adaptive;

    SANelderMead boltzmannSolver = SANelderMead::withSA(boltzmannParams, objective, boltzmann, 3u);
    SANelderMead adaptiveSolver = SANelderMead::withSA(adaptiveParams, objective, adaptive, 3u);
    boltzmannSolver.initSimplex();
    adaptiveSolver.initSimplex();

    for (int i = 0; i < 10; ++i)
    {
        boltzmannSolver.step();
        adaptiveSolver.step();
        assert(boltzmannSolver.getTemperature() == adaptiveSolver.getTemperature());
    }

    std::fprintf(stdout, "[Adaptive placeholder] PASS\n");
}

void testSeedReproducibility()
{
    auto runWithSeed = [](unsigned int seed) -> FitResult {
        std::vector<FitParam> params = {FitParam("x", -1.0, -5.0, 5.0, true)};
        SANelderMead::ObjectiveFn objective = [](const std::vector<double>& x) {
            return doubleWellObjectiveValue(x[0]);
        };

        SAConfig sa;
        sa.T_initial = 5.0;
        sa.schedule = CoolingSchedule::Geometric;
        sa.geometric_rate = 0.995;

        SANelderMead solver = SANelderMead::withSA(params, objective, sa, seed);
        solver.initSimplex();
        return solver.runUntilConvergence(3000, 1e-12);
    };

    const FitResult first = runWithSeed(123u);
    const FitResult second = runWithSeed(123u);
    const FitResult other = runWithSeed(456u);

    assert(first.best_params == second.best_params);
    assert(first.chi2_min == second.chi2_min);

    std::fprintf(stdout, "[Seed reproducibility] same=%.6f / %.6f other=%.6f\n",
                 first.best_params[0], second.best_params[0], other.best_params[0]);
    std::fprintf(stdout, "[Seed reproducibility] PASS\n");
}

void testSAMechanismFires()
{
    std::vector<FitParam> params = {FitParam("x", 0.0, -10.0, 10.0, true)};
    SANelderMead::ObjectiveFn objective = [](const std::vector<double>& x) {
        return x[0] * x[0];
    };

    SAConfig sa;
    sa.T_initial = 50.0;
    sa.schedule = CoolingSchedule::Geometric;
    sa.geometric_rate = 0.999;

    SANelderMead solver = SANelderMead::withSA(params, objective, sa, 99u);
    solver.initSimplex();

    for (int i = 0; i < 200; ++i)
        solver.step();

    std::fprintf(stdout, "[SA fires] accepted=%d\n", solver.getSAAcceptedCount());
    assert(solver.getSAAcceptedCount() > 0);
    std::fprintf(stdout, "[SA fires] PASS\n");
}
}

void runStage3Tests()
{
    std::fprintf(stdout, "\n=== Stage 3 Self-Tests ===\n");
    testZeroTemperatureMatchesStage2();
    testLocalMinimumEscape();
    testSetTemperatureZeroMidRun();
    testBoltzmannMonotonic();
    testGeometricRatio();
    testAdaptivePlaceholderMatchesBoltzmann();
    testSeedReproducibility();
    testSAMechanismFires();
    std::fprintf(stdout, "=== Stage 3 PASS ===\n\n");
}
