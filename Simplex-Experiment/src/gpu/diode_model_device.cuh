#pragma once

#include "lambert_w_device.cuh"

#define GPU_PHYS_K 8.617333262145179e-5

__device__ __forceinline__ double gpu_nan()
{
    return __longlong_as_double(0x7FF8000000000000ULL);
}

__device__ __forceinline__ double evaluateDiodeIV4_d(double voltage, const double* params, double temperature)
{
    const double I0 = params[0];
    const double A = params[1];
    const double Rs = params[2];
    const double Rsh = params[3];

    if (I0 <= 0.0 || A <= 0.0 || Rsh <= 0.0 || Rs < 0.0 || temperature <= 0.0)
        return gpu_nan();

    const double Vt = GPU_PHYS_K * temperature;
    const double AVt = A * Vt;
    if (Rs == 0.0)
        return I0 * exp(voltage / AVt) + voltage / Rsh;

    const double x = (I0 * Rs / AVt) * exp(voltage / AVt);
    const double w = LambertW0_d(x);
    if (isnan(w))
        return gpu_nan();

    const double I_lw = w * AVt / Rs;
    return I_lw + (voltage - I_lw * Rs) / Rsh;
}

__device__ __forceinline__ double evaluateDiodeIV6_d(double voltage, const double* params, double temperature)
{
    const double I0 = params[0];
    const double A = params[1];
    const double Rs = params[2];
    const double Rsh = params[3];
    const double alpha = params[4];
    const double Rsh2 = params[5];

    if (I0 <= 0.0 || A <= 0.0 || Rsh <= 0.0 || Rs < 0.0 || Rsh2 <= 0.0 || temperature <= 0.0)
        return gpu_nan();

    const double Vt = GPU_PHYS_K * temperature;
    const double AVt = A * Vt;
    double I_lw = 0.0;
    if (Rs == 0.0)
    {
        I_lw = I0 * exp(voltage / AVt);
    }
    else
    {
        const double x = (I0 * Rs / AVt) * exp(voltage / AVt);
        const double w = LambertW0_d(x);
        if (isnan(w))
            return gpu_nan();
        I_lw = w * AVt / Rs;
    }

    const double Vj = voltage - I_lw * Rs;
    const double power_term = pow(Vj, alpha);
    if (isnan(power_term))
        return gpu_nan();
    return I_lw + Vj / Rsh + power_term / Rsh2;
}

#undef GPU_PHYS_K

