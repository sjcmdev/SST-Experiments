#include "test_stage2.hpp"

#include "../solver/diode_model.hpp"
#include "../solver/diode_objective.hpp"
#include "../solver/nelder_mead.hpp"
#include "../solver/solver_types.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace
{
void testSimplexGeometry()
{
    const std::vector<double> centroid = {0.0, 0.0};
    const std::vector<double> worst = {2.0, 2.0};

    const auto reflected = reflectPoint(centroid, worst, 1.0);
    assert(reflected[0] == -2.0 && reflected[1] == -2.0);

    const auto expanded = expandPoint(centroid, reflected, 2.0);
    assert(expanded[0] == -4.0 && expanded[1] == -4.0);

    const auto contracted = contractPoint(centroid, worst, 0.5);
    assert(contracted[0] == 1.0 && contracted[1] == 1.0);

    std::fprintf(stdout, "[Simplex geometry] PASS\n");
}

void testQuadraticMinimization()
{
    const std::vector<double> center = {1.0, -2.0, 3.0, 0.5, -0.5};

    std::vector<FitParam> params;
    for (int i = 0; i < static_cast<int>(center.size()); ++i)
        params.push_back(FitParam("x" + std::to_string(i), 0.0, -10.0, 10.0, true));

    SANelderMead::ObjectiveFn objective = [center](const std::vector<double>& x) -> double {
        double sum = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double delta = x[i] - center[i];
            sum += delta * delta;
        }
        return sum;
    };

    SANelderMead solver(params, objective);
    solver.initSimplex();

    const FitResult result = solver.runUntilConvergence(1000, 1e-10);

    std::fprintf(stdout, "[Quadratic] chi2=%.3e  iter=%d  converged=%d\n",
                 result.chi2_min, result.iterations, result.converged);

    assert(result.converged);
    assert(result.chi2_min < 1e-10);

    for (size_t i = 0; i < center.size(); ++i)
    {
        const double error = std::abs(result.best_params[i] - center[i]);
        if (error > 1e-4)
        {
            std::fprintf(stderr, "[Quadratic FAIL] dim %zu: got=%.6g exp=%.6g err=%.3e\n",
                         i, result.best_params[i], center[i], error);
            assert(false);
        }
    }

    std::fprintf(stdout, "[Quadratic] PASS\n");
}

void testIVParameterRecovery()
{
    const double trueParams[] = {1.2e-10, 1.45, 0.08, 1800.0};

    constexpr int kPointCount = 40;
    std::vector<double> voltages(kPointCount);
    std::vector<double> currents(kPointCount);
    std::vector<double> sigma(kPointCount);

    for (int i = 0; i < kPointCount; ++i)
    {
        voltages[i] = -0.4 + static_cast<double>(i) * (1.0 / static_cast<double>(kPointCount - 1));
        currents[i] = evaluateDiodeIV4(voltages[i], trueParams);
        sigma[i] = 1e-9;
        assert(!std::isnan(currents[i]));
    }

    std::vector<FitParam> params = {
        FitParam("I0", 3.0e-10, 1e-11, 1e-8, true),
        FitParam("A", 1.8, 0.8, 2.5, true),
        FitParam("Rs", 0.3, 0.0, 1.0, true),
        FitParam("Rsh", 800.0, 100.0, 5000.0, true),
    };

    auto modelFn = [](double voltage, const double* params) { return evaluateDiodeIV4(voltage, params); };
    SANelderMead::ObjectiveFn objective =
        makeDiodeIVObjective(params, modelFn, voltages, currents, sigma);

    SANelderMead solver(params, objective);
    solver.initSimplex();

    const FitResult result = solver.runUntilConvergence(5000, 1e-18);

    std::fprintf(stdout, "[IV recovery] chi2=%.3e  iter=%d  stop=%s\n",
                 result.chi2_min, result.iterations, result.stop_reason.c_str());

    const char* names[] = {"I0", "A", "Rs", "Rsh"};
    bool allOk = true;
    for (int i = 0; i < 4; ++i)
    {
        const double relativeError = std::abs(result.best_params[i] - trueParams[i]) / std::abs(trueParams[i]);
        std::fprintf(stdout, "  %-4s true=%.6e  fit=%.6e  rel_err=%.4f%%\n",
                     names[i], trueParams[i], result.best_params[i], relativeError * 100.0);
        if (relativeError >= 0.01)
            allOk = false;
    }

    assert(allOk);
    std::fprintf(stdout, "[IV recovery] PASS\n");
}

void testNaNAndBoundsRobustness()
{
    SANelderMead::ObjectiveFn objective = [](const std::vector<double>& x) -> double {
        if (x[0] < 0.0)
            return std::numeric_limits<double>::infinity();
        const double delta = x[0] - 1.0;
        return delta * delta;
    };

    std::vector<FitParam> params = {FitParam("x0", -0.5, -10.0, 10.0, true)};

    SANelderMead solver(params, objective);
    solver.initSimplex();

    const FitResult result = solver.runUntilConvergence(2000, 1e-12);

    std::fprintf(stdout, "[NaN robustness] chi2=%.3e  best_x=%.6f  iter=%d\n",
                 result.chi2_min, result.best_params[0], result.iterations);

    assert(std::isfinite(result.chi2_min));
    assert(std::abs(result.best_params[0] - 1.0) < 1e-3);

    std::fprintf(stdout, "[NaN robustness] PASS\n");
}

void testAllVerticesInvalidAtInit()
{
    SANelderMead::ObjectiveFn alwaysInf = [](const std::vector<double>&) {
        return std::numeric_limits<double>::infinity();
    };

    std::vector<FitParam> params = {FitParam("x0", 0.0, -1.0, 1.0, true)};
    SANelderMead solver(params, alwaysInf);
    solver.initSimplex();

    const FitResult result = solver.runUntilConvergence(100, 1e-12);

    assert(result.stop_reason == "max_iter");
    assert(result.iterations == 100);
    assert(std::isinf(result.chi2_min));

    std::fprintf(stdout, "[All-invalid edge case] PASS\n");
}

void testDegenerateRestart()
{
    std::vector<FitParam> params = {FitParam("x0", 1.0, -5.0, 5.0, true)};

    SANelderMead::ObjectiveFn objective = [](const std::vector<double>& x) {
        return x[0] * x[0];
    };

    SANelderMead solver(params, objective, 1.0, 2.0, 0.5, 0.5, 1e-2);
    solver.initSimplex();

    int restartCount = 0;
    constexpr int kSteps = 500;
    for (int i = 0; i < kSteps; ++i)
    {
        if (solver.step() == StepType::Restart)
            ++restartCount;
    }

    std::fprintf(stdout, "[Degenerate restart] restart_count=%d / %d steps, iteration=%d\n",
                 restartCount, kSteps, solver.iteration());

    assert(restartCount > 0);
    assert(solver.iteration() == kSteps);

    std::fprintf(stdout, "[Degenerate restart] PASS\n");
}

void testDeterminism()
{
    auto buildAndRun = []() -> FitResult {
        const std::vector<double> center = {2.0, -1.0, 0.0};
        std::vector<FitParam> params;
        for (int i = 0; i < 3; ++i)
            params.push_back(FitParam("x" + std::to_string(i), 5.0, -10.0, 10.0, true));

        SANelderMead::ObjectiveFn objective = [center](const std::vector<double>& x) {
            double sum = 0.0;
            for (size_t i = 0; i < x.size(); ++i)
            {
                const double delta = x[i] - center[i];
                sum += delta * delta;
            }
            return sum;
        };

        SANelderMead solver(params, objective);
        solver.initSimplex();
        return solver.runUntilConvergence(500, 1e-12);
    };

    const FitResult first = buildAndRun();
    const FitResult second = buildAndRun();

    assert(first.iterations == second.iterations);
    assert(first.chi2_min == second.chi2_min);
    for (size_t i = 0; i < first.best_params.size(); ++i)
        assert(first.best_params[i] == second.best_params[i]);

    std::fprintf(stdout, "[Determinism] PASS\n");
}
}

void runStage2Tests()
{
    std::fprintf(stdout, "\n=== Stage 2 Self-Tests ===\n");
    testSimplexGeometry();
    testQuadraticMinimization();
    testIVParameterRecovery();
    testNaNAndBoundsRobustness();
    testAllVerticesInvalidAtInit();
    testDegenerateRestart();
    testDeterminism();
    std::fprintf(stdout, "=== Stage 2 PASS ===\n\n");
}
