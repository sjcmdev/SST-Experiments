#include "test_stage4.hpp"

#include "../solver/nelder_mead.hpp"
#include "../solver/trace_utils.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
SANelderMead::ObjectiveFn squareObjective()
{
    return [](const std::vector<double>& values) {
        double sum = 0.0;
        for (double value : values)
            sum += value * value;
        return sum;
    };
}

void testTraceSizeMatchesIterations()
{
    std::vector<FitParam> params = {FitParam("x", 5.0, -10.0, 10.0, true)};
    SANelderMead solver(params, squareObjective(), 1.0, 2.0, 0.5, 0.5, 1e-12, false, SAConfig{}, 0, true);
    solver.initSimplex();

    const FitResult result = solver.runUntilConvergence(1000, 1e-10);

    assert(solver.getTraceSize() == result.iterations);
    assert(solver.getTraceSize() == solver.iteration());
    std::fprintf(stdout, "[Trace size] PASS %d\n", solver.getTraceSize());
}

void testTraceChi2BestMonotonic()
{
    std::vector<FitParam> params = {FitParam("x", -1.0, -5.0, 5.0, true)};
    SANelderMead::ObjectiveFn objective = [](const std::vector<double>& x) {
        const double left = (x[0] + 1.0) * (x[0] + 1.0);
        const double right = (x[0] - 1.0) * (x[0] - 1.0);
        return std::min(left, right) - 0.05 * x[0];
    };

    SAConfig sa;
    sa.T_initial = 5.0;
    sa.schedule = CoolingSchedule::Geometric;
    sa.geometric_rate = 0.995;

    SANelderMead solver(params, objective, 1.0, 2.0, 0.5, 0.5, 1e-9, true, sa, 7u, true);
    solver.initSimplex();
    for (int i = 0; i < 1000; ++i)
        solver.step();

    assert(std::abs(solver.getTraceStep(0).T - 5.0) < 1e-9);

    double previous = solver.getTraceStep(0).state_before.chi2_values[
        solver.getTraceStep(0).state_before.best_idx];
    for (int i = 0; i < solver.getTraceSize(); ++i)
    {
        const TraceStep& step = solver.getTraceStep(i);
        const double after = step.state_after.chi2_values[step.state_after.best_idx];
        assert(after <= previous + 1e-12);
        previous = after;
    }
    assert(solver.getSAAcceptedCount() > 0);
    std::fprintf(stdout, "[Trace chi2] PASS trace=%d sa=%d\n", solver.getTraceSize(), solver.getSAAcceptedCount());
}

void testTraceManualReview()
{
    std::vector<FitParam> params = {
        FitParam("x", 5.0, -10.0, 10.0, true),
        FitParam("y", 5.0, -10.0, 10.0, true),
    };

    SANelderMead solver(params, squareObjective(), 1.0, 2.0, 0.5, 0.5, 1e-12, false, SAConfig{}, 0, true);
    solver.initSimplex();
    for (int i = 0; i < 20; ++i)
        solver.step();

    assert(solver.getTraceSize() == 20);
    for (int i = 0; i < 20; ++i)
    {
        const TraceStep& step = solver.getTraceStep(i);
        assert(step.iteration == i + 1);
        assert(step.state_before.vertices.size() == 3);
        assert(step.state_after.vertices.size() == 3);
        assert(step.chi2_min == step.state_after.chi2_values[step.state_after.best_idx]);
        std::fprintf(stdout, "  %s\n", traceStepToString(step).c_str());
    }
    std::fprintf(stdout, "[Trace manual] PASS\n");
}

void testTraceDisabledNotSlower()
{
    auto makeParams = []() {
        return std::vector<FitParam>{
            FitParam("a", 5.0, -100.0, 100.0, true),
            FitParam("b", 5.0, -100.0, 100.0, true),
            FitParam("c", 5.0, -100.0, 100.0, true),
            FitParam("d", 5.0, -100.0, 100.0, true),
        };
    };

    constexpr int kSteps = 5000;

    SANelderMead off(makeParams(), squareObjective());
    off.initSimplex();
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kSteps; ++i)
        off.step();
    const double offMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();

    SANelderMead on(makeParams(), squareObjective(), 1.0, 2.0, 0.5, 0.5, 1e-12, false, SAConfig{}, 0, true);
    on.initSimplex();
    start = std::chrono::steady_clock::now();
    for (int i = 0; i < kSteps; ++i)
        on.step();
    const double onMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();

    assert(off.getTraceSize() == 0);
    assert(on.getTraceSize() == kSteps);
    assert(offMs <= onMs * 5.0 + 5.0);
    std::fprintf(stdout, "[Trace perf] off=%.1fms on=%.1fms PASS\n", offMs, onMs);
}

void testTraceFirstStepTemperatureCorrect()
{
    std::vector<FitParam> params = {FitParam("x", 5.0, -10.0, 10.0, true)};
    SAConfig sa;
    sa.T_initial = 17.0;
    sa.schedule = CoolingSchedule::Geometric;
    sa.geometric_rate = 0.99;

    SANelderMead solver(params, squareObjective(), 1.0, 2.0, 0.5, 0.5, 1e-12, true, sa, 5u, true);
    solver.initSimplex();
    solver.step();

    assert(std::abs(solver.getTraceStep(0).T - 17.0) < 1e-9);
    std::fprintf(stdout, "[Trace T] PASS\n");
}

void testTraceGetStepBoundsCheck()
{
    std::vector<FitParam> params = {FitParam("x", 5.0, -10.0, 10.0, true)};
    SANelderMead solver(params, squareObjective(), 1.0, 2.0, 0.5, 0.5, 1e-12, false, SAConfig{}, 0, true);
    solver.initSimplex();
    for (int i = 0; i < 5; ++i)
        solver.step();

    bool negativeOutOfRange = false;
    try
    {
        (void)solver.getTraceStep(-1);
    }
    catch (const std::out_of_range&)
    {
        negativeOutOfRange = true;
    }

    bool upperOutOfRange = false;
    try
    {
        (void)solver.getTraceStep(5);
    }
    catch (const std::out_of_range&)
    {
        upperOutOfRange = true;
    }

    (void)solver.getTraceStep(4);
    assert(negativeOutOfRange && upperOutOfRange);
    std::fprintf(stdout, "[Trace bounds] PASS\n");
}

void testClearTrace()
{
    std::vector<FitParam> params = {FitParam("x", 5.0, -10.0, 10.0, true)};
    SANelderMead solver(params, squareObjective(), 1.0, 2.0, 0.5, 0.5, 1e-12, false, SAConfig{}, 0, true);
    solver.initSimplex();
    for (int i = 0; i < 50; ++i)
        solver.step();
    assert(solver.getTraceSize() == 50);

    solver.clearTrace();
    assert(solver.getTraceSize() == 0);
    assert(solver.getTrace().capacity() == 0);

    for (int i = 0; i < 10; ++i)
        solver.step();
    assert(solver.getTraceSize() == 10);
    std::fprintf(stdout, "[Clear trace] PASS\n");
}

void testTraceStepToStringFormat()
{
    std::vector<FitParam> params = {FitParam("x", 5.0, -10.0, 10.0, true)};
    SANelderMead solver(params, squareObjective(), 1.0, 2.0, 0.5, 0.5, 1e-12, false, SAConfig{}, 0, true);
    solver.initSimplex();
    solver.step();

    const std::string text = traceStepToString(solver.getTraceStep(0));
    assert(text.find("iter=1") != std::string::npos);
    assert(text.find("T=") != std::string::npos);
    assert(text.find("chi2:") != std::string::npos);
    assert(text.find("->") != std::string::npos);
    std::fprintf(stdout, "[Trace string] \"%s\" PASS\n", text.c_str());
}

void testTraceToggleMidRun()
{
    std::vector<FitParam> params = {FitParam("x", 5.0, -10.0, 10.0, true)};
    SANelderMead solver(params, squareObjective());
    solver.initSimplex();

    for (int i = 0; i < 10; ++i)
        solver.step();
    assert(solver.getTraceSize() == 0);

    solver.setTraceEnabled(true);
    for (int i = 0; i < 15; ++i)
        solver.step();
    assert(solver.getTraceSize() == 15);

    solver.setTraceEnabled(false);
    for (int i = 0; i < 20; ++i)
        solver.step();
    assert(solver.getTraceSize() == 15);
    assert(solver.iteration() == 45);

    std::fprintf(stdout, "[Trace toggle] PASS\n");
}
}

void runStage4Tests()
{
    std::fprintf(stdout, "\n=== Stage 4 Self-Tests ===\n");
    testTraceSizeMatchesIterations();
    testTraceChi2BestMonotonic();
    testTraceManualReview();
    testTraceDisabledNotSlower();
    testTraceFirstStepTemperatureCorrect();
    testTraceGetStepBoundsCheck();
    testClearTrace();
    testTraceStepToStringFormat();
    testTraceToggleMidRun();
    std::fprintf(stdout, "=== Stage 4 PASS ===\n\n");
}
