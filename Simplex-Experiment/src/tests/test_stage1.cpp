#include "test_stage1.hpp"

#include "../solver/diode_model.hpp"
#include "../solver/lambertw.hpp"
#include "../solver/objective.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
constexpr double kE = 2.71828182845904523536;
constexpr double kMinusInvE = -0.36787944117144232159;

bool approxEqual(double value, double expected, double tolerance) noexcept
{
    return std::abs(value - expected) <= tolerance;
}

void testLambertW()
{
    struct Case
    {
        double input;
        double expected;
        double tolerance;
    };

    const Case cases[] = {
        {0.0, 0.0, 1e-15},
        {1.0, 0.56714329040978387299, 1e-12},
        {kE, 1.0, 1e-12},
        {kMinusInvE, -1.0, 1e-10},
        {1e-10, 9.999999999e-11, 1e-20},
        {1e-4, 9.999000149973338e-5, 1e-16},
        {0.01, 0.009901473843595012, 1e-15},
        {100.0, 3.38563014029005, 1e-12},
        {1e6, 11.3833580861401, 1e-11},
    };

    for (const Case& testCase : cases)
        assert(approxEqual(lambertW(testCase.input), testCase.expected, testCase.tolerance));

    assert(std::isnan(lambertW(kMinusInvE - 1e-12)));
    assert(std::isnan(lambertW(std::numeric_limits<double>::quiet_NaN())));
}

void testDiodeIV4Physics()
{
    const double params[] = {1e-9, 1.5, 0.1, 1000.0};
    const double voltages[] = {-0.5, -0.3, -0.1, 0.0, 0.1, 0.3, 0.5, 0.6, 0.7};

    double previous = -std::numeric_limits<double>::infinity();
    for (double voltage : voltages)
    {
        const double current = evaluateDiodeIV4(voltage, params);
        assert(std::isfinite(current));
        assert(current > previous);
        previous = current;
    }

    assert(std::abs(evaluateDiodeIV4(0.0, params)) < 1e-6);
}

void testChiSquaredZero()
{
    const double trueParams[] = {1e-10, 1.5, 0.05, 2000.0};
    constexpr int kPointCount = 30;
    double voltages[kPointCount] = {};
    double currents[kPointCount] = {};
    double sigma[kPointCount] = {};

    for (int i = 0; i < kPointCount; ++i)
    {
        voltages[i] = -0.2 + 0.9 * static_cast<double>(i) / static_cast<double>(kPointCount - 1);
        currents[i] = evaluateDiodeIV4(voltages[i], trueParams);
        sigma[i] = 1e-6;
    }

    const double chi2 = computeChiSquared(
        trueParams,
        voltages,
        currents,
        sigma,
        kPointCount,
        [](double voltage, const double* params) { return evaluateDiodeIV4(voltage, params); });

    assert(chi2 < 1e-20);
}

void testEdgeCases()
{
    const double invalidParams[] = {-1e-9, 1.5, 0.1, 1000.0};
    assert(std::isnan(evaluateDiodeIV4(0.5, invalidParams)));

    const double rsZeroParams[] = {1e-10, 1.5, 0.0, 1000.0};
    const double rsZeroCurrent = evaluateDiodeIV4(0.5, rsZeroParams);
    assert(std::isfinite(rsZeroCurrent));
    assert(rsZeroCurrent > 0.0);

    const double highVoltageCurrent = evaluateDiodeIV4(10.0, rsZeroParams);
    assert(std::isfinite(highVoltageCurrent) || std::isinf(highVoltageCurrent));

    const double validParams[] = {1e-10, 1.5, 0.05, 2000.0};
    const double voltages[] = {0.0};
    const double currents[] = {0.0};
    const double sigma[] = {1e-6};
    const double zeroSigma[] = {0.0};

    const double nanChi2 = computeChiSquared(
        validParams,
        voltages,
        currents,
        sigma,
        1,
        [](double, const double*) { return std::numeric_limits<double>::quiet_NaN(); });
    assert(std::isinf(nanChi2));

    const double badSigmaChi2 = computeChiSquared(
        validParams,
        voltages,
        currents,
        zeroSigma,
        1,
        [](double voltage, const double* params) { return evaluateDiodeIV4(voltage, params); });
    assert(std::isinf(badSigmaChi2));
}
}

void runStage1Tests()
{
    std::fprintf(stdout, "\n=== Stage 1 Self-Tests ===\n");
    testLambertW();
    testDiodeIV4Physics();
    testChiSquaredZero();
    testEdgeCases();
    std::fprintf(stdout, "=== Stage 1 PASS ===\n\n");
}
