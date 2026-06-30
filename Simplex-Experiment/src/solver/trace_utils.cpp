#include "trace_utils.hpp"

#include <cstdio>

const char* stepTypeToString(StepType type)
{
    switch (type)
    {
    case StepType::Reflection:
        return "Reflection";
    case StepType::Expansion:
        return "Expansion";
    case StepType::Contraction:
        return "Contraction";
    case StepType::Shrink:
        return "Shrink";
    case StepType::Restart:
        return "Restart";
    }
    return "Unknown";
}

std::string traceStepToString(const TraceStep& step)
{
    const double chi2Before =
        step.state_before.chi2_values[step.state_before.best_idx];
    const double chi2After =
        step.state_after.chi2_values[step.state_after.best_idx];

    char buffer[256];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "[iter=%d T=%.3f %s] chi2: %.3f -> %.3f",
        step.iteration,
        step.T,
        stepTypeToString(step.type),
        chi2Before,
        chi2After);
    return std::string(buffer);
}
