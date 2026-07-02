/**
 * lambert_w_device.cuh
 *
 * GPU implementation of Lambert W function — branches W0 (k=0) and W-1 (k=-1).
 *
 * Based on:
 *   D. Veberic, "Having Fun with Lambert W(x) Function",
 *   arXiv:1003.1628 (2010) and Comp. Phys. Comm. 183 (2012) 2622-2628.
 *
 *   Original C++ source: https://github.com/SirJamesClarkMaxwell/LambertW
 *
 * Algorithm:
 *   The Veberic method uses region-specific initial approximations
 *   (branch-point series, Padé rational approximants, asymptotic expansion,
 *   or logarithmic recursion), each refined by one Halley step.
 *   Double precision: max error < 2 ULP across the full domain.
 *
 * NVRTC compatibility:
 *   - No #include directives (uses only CUDA built-in math: exp, log, sqrt)
 *   - No static local variables (forbidden in __device__ code)
 *   - No std::* types
 *   - Safe to inject as NVRTC header string (see lambert_w_nvrtc_example.cpp)
 *
 * API:
 *   __device__ double LambertW0_d  (double x)  // W0,  domain: x ∈ [-1/e, +∞)
 *   __device__ double LambertWm1_d (double x)  // W-1, domain: x ∈ [-1/e, 0)
 *   __device__ float  LambertW0_f  (float  x)  // float via double promotion
 *   __device__ float  LambertWm1_f (float  x)
 *   __device__ double LambertW_d   (int branch, double x)  // branch dispatch
 *
 * Domain errors return NaN (IEEE 754 quiet NaN, no trap).
 *
 * SST project — SemiconductorStudio / JFM module
 * Companion to GPU-Experiment infrastructure (Fazy 1-5).
 */

#pragma once
#ifndef LAMBERT_W_DEVICE_CUH
#define LAMBERT_W_DEVICE_CUH

/* ──────────────────────────────────────────────────────────────────────────────
 * Internal constants
 * ────────────────────────────────────────────────────────────────────────────*/

#ifndef LW_M_E
#define LW_M_E 2.71828182845904523536028747135266249775724709369995
#endif

/* -1/e, lower bound of W domain */
#define LW_NEG_INV_E (-0.36787944117144232159552377016146086744581113103177)

/* ──────────────────────────────────────────────────────────────────────────────
 * IEEE 754 quiet NaN without <limits> or <cmath>
 * ────────────────────────────────────────────────────────────────────────────*/

__device__ __forceinline__ double lw__nan_d() {
    return __longlong_as_double(0x7FF8000000000000ULL);
}
__device__ __forceinline__ float lw__nan_f() {
    return __int_as_float(0x7FC00000);
}

/* ──────────────────────────────────────────────────────────────────────────────
 * Halley iteration step  (one step is sufficient after a good initial estimate)
 *
 * Halley's method for f(w) = w*exp(w) - x = 0:
 *   w_{n+1} = w_n - (w_n*e^w_n - x) / (e^w_n*(w_n+1) - (w_n+2)*(w_n*e^w_n-x)/(2*(w_n+1)))
 * ────────────────────────────────────────────────────────────────────────────*/

__device__ __forceinline__ double lw__halley(double x, double w) {
    double ew   = exp(w);
    double wew  = w * ew;
    double wewx = wew - x;
    double w1   = w + 1.0;
    return w - wewx / (ew * w1 - (w + 2.0) * wewx / (2.0 * w1));
}

/* ──────────────────────────────────────────────────────────────────────────────
 * Branch-point series   W(x) ≈ Σ c[i] * p^i,  p = ±√(2(ex+1))
 *
 * Coefficients from Veberic (2012), table 1 (= Comtet/de Bruijn series).
 * Horner evaluation from highest to lowest coefficient.
 *
 * c[0] = -1
 * c[1] =  1
 * c[2] = -1/3
 * c[3] =  11/72
 * c[4] = -43/540
 * c[5] =  769/17280
 * c[6] = -221/8505
 * c[7] =  680863/43545600
 * c[8] = -1963/235144320   ≈ -9.61689e-3
 * c[9] =  226287557/...    ≈  6.01454e-3
 * c[10]= -...              ≈ -3.81130e-3
 * ────────────────────────────────────────────────────────────────────────────*/

/* Order 8, branch 0  (p > 0) */
__device__ __forceinline__ double lw__W0_bp8(double x) {
    double p = sqrt(2.0 * (LW_M_E * x + 1.0));
    double t = -9.61689202429943171e-3;
    t = t*p +  1.56356325323339212e-2;
    t = t*p + -2.59847148736037625e-2;
    t = t*p +  4.45023148148148148e-2;
    t = t*p + -7.96296296296296296e-2;
    t = t*p +  1.52777777777777778e-1;
    t = t*p + -3.33333333333333333e-1;
    t = t*p +  1.0;
    t = t*p + -1.0;
    return t;
}

/* Order 10, branch 0  (used as Halley seed for slightly less-negative x) */
__device__ __forceinline__ double lw__W0_bp10(double x) {
    double p = sqrt(2.0 * (LW_M_E * x + 1.0));
    double t = -3.81129803489199923e-3;
    t = t*p +  6.01454325295611786e-3;
    t = t*p + -9.61689202429943171e-3;
    t = t*p +  1.56356325323339212e-2;
    t = t*p + -2.59847148736037625e-2;
    t = t*p +  4.45023148148148148e-2;
    t = t*p + -7.96296296296296296e-2;
    t = t*p +  1.52777777777777778e-1;
    t = t*p + -3.33333333333333333e-1;
    t = t*p +  1.0;
    t = t*p + -1.0;
    return t;
}

/* Order 8, branch -1  (p = -√…) */
__device__ __forceinline__ double lw__Wm1_bp8(double x) {
    double p = -sqrt(2.0 * (LW_M_E * x + 1.0));
    double t = -9.61689202429943171e-3;
    t = t*p +  1.56356325323339212e-2;
    t = t*p + -2.59847148736037625e-2;
    t = t*p +  4.45023148148148148e-2;
    t = t*p + -7.96296296296296296e-2;
    t = t*p +  1.52777777777777778e-1;
    t = t*p + -3.33333333333333333e-1;
    t = t*p +  1.0;
    t = t*p + -1.0;
    return t;
}

/* Order 4, branch -1  (coarse seed for slightly less-negative x near branch pt) */
__device__ __forceinline__ double lw__Wm1_bp4(double x) {
    double p = -sqrt(2.0 * (LW_M_E * x + 1.0));
    double t = -7.96296296296296296e-2;
    t = t*p +  1.52777777777777778e-1;
    t = t*p + -3.33333333333333333e-1;
    t = t*p +  1.0;
    t = t*p + -1.0;
    return t;
}

/* ──────────────────────────────────────────────────────────────────────────────
 * Padé rational approximants
 *
 * Each function evaluates the Padé approximant for the given (branch, index)
 * using Horner evaluation of numerator and denominator separately.
 * Coefficients from Veberic (2012), tables 2-7.
 * ────────────────────────────────────────────────────────────────────────────*/

/* Pade<0,1>  x ∈ [-0.311, 1.38]  (polynomial ratio in x, degree 4/4) */
__device__ __forceinline__ double lw__W0_pade1(double x) {
    double n = ((((0.07066247420543414   *x
                + 2.43268145305776870)  *x
                + 6.39672835731526000)  *x
                + 4.66336502583682100)  *x
                + 0.99999908757381000);
    double d = ((((1.29066601395116920  *x
                + 7.16457177541098700)  *x
                + 10.5599850889531140)  *x
                + 5.66336307375819000)  *x
                + 1.0);
    return x * n / d;
}

/* Pade<0,2>  x ∈ [1.38, 236]  (log-shifted ratio, degree 3/2) */
__device__ __forceinline__ double lw__W0_pade2(double x) {
    double y = log(0.5 * x) - 2.0;
    double n = (((6.97926967967045200e-5 *y
               + 1.71103688466158060e-2) *y
               + 1.93386077709002370e-1) *y
               + 6.66664889649979300e-1);
    double d = ((1.88060684652668000e-2  *y
               + 2.34512698271333170e-1) *y
               + 1.0);
    return 2.0 + y * n / d;
}

/* Pade<-1,4>  x ∈ [-0.289379, -0.0509]  (polynomial ratio in x, degree 4/4) */
__device__ __forceinline__ double lw__Wm1_pade4(double x) {
    double n = ((((-2793.4565508841197  *x
                - 1987.3632221106518)  *x
                +  385.7992853617571)  *x
                +  277.2362778379572)  *x
                -    7.840776922133643);
    double d = ((((280.6156995997829   *x
                + 941.9414019982657)  *x
                + 190.6442933889464)  *x
                -  63.9354049435897)  *x
                +   1.0);
    return n / d;
}

/* Pade<-1,5>  x ∈ [-0.0509, -1.318e-4]  (exp of log-shifted ratio, degree 3/3) */
__device__ __forceinline__ double lw__Wm1_pade5(double x) {
    double y = log(-x);
    double n = (((0.16415668298255184  *y
               - 3.33487392030194100) *y
               + 2.48314158600037470) *y
               + 4.17342447457487900);
    double d = (((0.03123941148737416  *y
               - 1.29616596934000760) *y
               + 4.51717849277290600) *y
               + 1.0);
    return -exp(n / d);
}

/* Pade<-1,6>  x ∈ [-1.318e-4, -6.31e-31]  (exp of log-shifted ratio, degree 4/4) */
__device__ __forceinline__ double lw__Wm1_pade6(double x) {
    double y = log(-x);
    double n = ((((2.69872432545332540e-5 *y
                - 7.69210644826734100e-3) *y
                + 2.87934617193002060e-1) *y
                - 1.52670588846470180e+0) *y
                - 5.37066926899128800e-1);
    double d = ((((3.60065021049303430e-6 *y
                - 1.55524635555914870e-3) *y
                + 8.80119468248976900e-2) *y
                - 8.97392235757558300e-1) *y
                + 1.0);
    return -exp(n / d);
}

/* Pade<-1,7>  x ∈ [-0.366079, -0.289379]  (-1 - sqrt of ratio, degree 4/4) */
__device__ __forceinline__ double lw__Wm1_pade7(double x) {
    double n = ((((988.0070769375508   *x
                + 1619.8111957356814) *x
                +  989.2017745708083) *x
                +  266.9332506485452) *x
                +   26.875022558546036);
    double d = ((((-205.50469464210596 *x
                -  270.0440832897079) *x
                -  109.554245632316)  *x
                -   11.275355431307334)*x
                +    1.0);
    return -1.0 - sqrt(n / d);
}

/* ──────────────────────────────────────────────────────────────────────────────
 * Asymptotic expansion for W0, large x  (Corless et al. 1996, de Bruijn 1981)
 *
 * W0(x) ≈ L1 + Σ_{k=0}^{5} A[k](L2) / L1^k
 *
 * where L1 = log(x), L2 = log(L1), and the coefficients A[k] are:
 *   A[0] = -L2
 *   A[1] =  L2
 *   A[2] =  L2*(L2-2)/2
 *   A[3] =  L2*(2L2²-9L2+6)/6
 *   A[4] =  L2*(3L2³-22L2²+36L2-12)/12
 *   A[5] =  L2*(12L2⁴-120L2³+420L2²-480L2+120)/60  [=B<5>(L2)]
 *
 * Evaluated via Horner in 1/L1 for numerical stability.
 * ────────────────────────────────────────────────────────────────────────────*/

__device__ __forceinline__ double lw__W0_asymptotic(double x) {
    double L1  = log(x);
    double L2  = log(L1);
    double iL1 = 1.0 / L1;
    double l2  = L2;
    double l22 = l2 * l2;
    double l23 = l22 * l2;
    double l24 = l23 * l2;
    double l25 = l24 * l2;

    /* A[k](L2), k = 5 down to 0 */
    double A5 =  l2 - 5.0*l22 + (35.0/6.0)*l23 - (25.0/12.0)*l24 + (1.0/5.0)*l25;
    double A4 = -l2 + 3.0*l22 - (11.0/6.0)*l23 + (1.0/4.0)*l24;
    double A3 =  l2 - (3.0/2.0)*l22 + (1.0/3.0)*l23;
    double A2 =  (1.0/2.0)*l22 - l2;
    double A1 =  l2;
    double A0 = -l2;

    /* Horner in iL1 */
    double s = A5;
    s = s*iL1 + A4;
    s = s*iL1 + A3;
    s = s*iL1 + A2;
    s = s*iL1 + A1;
    s = s*iL1 + A0;

    return L1 + s;
}

/* ──────────────────────────────────────────────────────────────────────────────
 * Logarithmic recursion for W-1, very small |x|  (x near 0-)
 *
 * W-1(x) → -∞ as x → 0-, handled by iterated log:
 *
 *   s₀ = log(-x)
 *   sₙ = s₀ - log(-sₙ₋₁)
 *
 * 3 iterations produce a seed accurate enough for one Halley step.
 * ────────────────────────────────────────────────────────────────────────────*/

__device__ __forceinline__ double lw__Wm1_log_recursion3(double x) {
    double s0 = log(-x);          /* log(-x), valid since x < 0 */
    double s1 = s0 - log(-s0);    /* -s0 > 0 since s0 < 0       */
    double s2 = s0 - log(-s1);
    double s3 = s0 - log(-s2);
    return s3;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API — double precision
 * ════════════════════════════════════════════════════════════════════════════*/

/**
 * LambertW0_d  —  principal branch W₀(x)
 *
 * Domain: x ∈ [-1/e, +∞),  W₀ ∈ [-1, +∞)
 * Returns NaN for x < -1/e.
 * Accuracy: ≤ 2 ULP (double precision) across the full domain.
 *
 * Region dispatch (from Veberic 2012, table 8):
 *   x < -0.367679          → branch-point series, order 8  (direct, no Halley)
 *   x ∈ [-0.367679,-0.311) → Halley( branch-point series order 10 )
 *   x ∈ [-0.311, 1.38)     → Halley( Padé [4/4] )
 *   x ∈ [1.38, 236)        → Halley( Padé log-shifted [3/2] )
 *   x ≥ 236                → Halley( asymptotic expansion, 5 terms )
 */
__device__ double LambertW0_d(double x) {
    if (x < LW_NEG_INV_E)  return lw__nan_d();

    if (x < -0.367679)
        return lw__W0_bp8(x);

    if (x < -0.311)
        return lw__halley(x, lw__W0_bp10(x));

    if (x < 1.38)
        return lw__halley(x, lw__W0_pade1(x));

    if (x < 236.0)
        return lw__halley(x, lw__W0_pade2(x));

    return lw__halley(x, lw__W0_asymptotic(x));
}

/**
 * LambertWm1_d  —  branch W₋₁(x)
 *
 * Domain: x ∈ [-1/e, 0),  W₋₁ ∈ (-∞, -1]
 * Returns NaN for x ≥ 0 or x < -1/e.
 * Accuracy: ≤ 2 ULP across the full domain.
 *
 * Region dispatch (from Veberic 2012, table 9):
 *   x < -0.367579           → branch-point series, order 8  (direct)
 *   x ∈ [-0.367579,-0.36608)→ Halley( branch-point series order 4 )
 *   x ∈ [-0.36608,-0.289379)→ Halley( Padé -1-√[4/4] )
 *   x ∈ [-0.289379,-0.0509) → Halley( Padé rational [4/4] )
 *   x ∈ [-0.0509,-1.318e-4) → Halley( Padé exp-log [3/3] )
 *   x ∈ [-1.318e-4,-6.31e-31)→ Halley( Padé exp-log [4/4] )
 *   x ∈ [-6.31e-31, 0)      → Halley( log recursion, 3 steps )
 */
__device__ double LambertWm1_d(double x) {
    if (x >= 0.0 || x < LW_NEG_INV_E) return lw__nan_d();

    if (x < -0.367579)
        return lw__Wm1_bp8(x);

    if (x < -0.366079)
        return lw__halley(x, lw__Wm1_bp4(x));

    if (x < -0.289379)
        return lw__halley(x, lw__Wm1_pade7(x));

    if (x < -0.0509)
        return lw__halley(x, lw__Wm1_pade4(x));

    if (x < -1.31826e-4)
        return lw__halley(x, lw__Wm1_pade5(x));

    if (x < -6.30957e-31)
        return lw__halley(x, lw__Wm1_pade6(x));

    return lw__halley(x, lw__Wm1_log_recursion3(x));
}

/**
 * LambertW_d  —  branch dispatch
 * branch  0 → W₀   (principal)
 * branch -1 → W₋₁
 * other      → NaN
 */
__device__ __forceinline__ double LambertW_d(int branch, double x) {
    if (branch ==  0) return LambertW0_d(x);
    if (branch == -1) return LambertWm1_d(x);
    return lw__nan_d();
}

/* ══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API — single precision  (promotes to double, then truncates)
 *
 * Rationale: the Padé coefficients were fitted for double precision.
 * Internal double promotion guarantees full float accuracy (≈7 sig. digits)
 * without maintaining a separate set of float coefficients.
 * ════════════════════════════════════════════════════════════════════════════*/

__device__ __forceinline__ float LambertW0_f(float x) {
    return (float)LambertW0_d((double)x);
}

__device__ __forceinline__ float LambertWm1_f(float x) {
    return (float)LambertWm1_d((double)x);
}

__device__ __forceinline__ float LambertW_f(int branch, float x) {
    return (float)LambertW_d(branch, (double)x);
}

/* ──────────────────────────────────────────────────────────────────────────────
 * Utility: residual check  ‖w·eʷ − x‖ / |x|   (for debugging / validation)
 * ────────────────────────────────────────────────────────────────────────────*/

__device__ __forceinline__ double lw__residual(double x, double w) {
    double r = w * exp(w) - x;
    return (x != 0.0) ? (r < 0.0 ? -r : r) / (x < 0.0 ? -x : x)
                      : (r < 0.0 ? -r : r);
}

#undef LW_M_E
#undef LW_NEG_INV_E

#endif /* LAMBERT_W_DEVICE_CUH */
