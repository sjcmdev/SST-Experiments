/**
 * lambert_w_gpu_test.cu
 *
 * Standalone validation/benchmark for Vendor/LambertW/lambert_w_device.cuh.
 * NOT part of the NVRTC pipeline (no header injection needed here) — this is
 * a plain NVCC-compiled executable, in the same spirit as the existing
 * test_accuracy.cxx (same idea: sweep both branches, compare against
 * Fukushima as an independent cross-check), just GPU-batched and extended
 * with throughput measurement and a JFM single-diode demo.
 *
 * Compares GPU results against the original CPU implementations:
 *   - utl::LambertW<0>   / utl::LambertW<-1>   (Veberic, same algorithm family)
 *   - Fukushima::LambertW0 / Fukushima::LambertWm1 (independent reference)
 *
 * Build (adjust sm version to your GPU; compute_75=Turing, compute_86=Ampere,
 * compute_89=Ada — same convention as KernelManager::compile() archOpt):
 *
 *   nvcc -O2 -std=c++17
 *        -gencode arch=compute_75,code=sm_75
 *        -I Vendor/LambertW                  <- this file + LambertW.h live here
 *        -D"M_E=2.7182818284590452354"        <- same define as Vendor/LambertW/premake5.lua
 *        lambert_w_gpu_test.cu
 *        Vendor/LambertW/LambertW.cc
 *        Vendor/LambertW/FukushimaLambertW.cc
 *        -o lambert_w_gpu_test
 *
 * Full integration into the NVRTC pipeline (production JFM kernels):
 *   see faza6_lambertw_gpu_integracja.md
 *
 * Expected output:
 *   === W0  validation ===
 *   Max relative error vs CPU Veberic:    < 3e-15
 *   Max relative error vs CPU Fukushima:  < 3e-15
 *   === W-1 validation ===
 *   ...
 *   === Throughput ===
 *   W0  batch N=1048576: X.X ms  (YYY M eval/s)
 */

#include "lambert_w_device.cuh"
#include "LambertW.h"
#include "FukushimaLambertW.h"

#include <cuda_runtime.h>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <chrono>

/* ─── CUDA error check helper ────────────────────────────────────────────── */

#define CUDA_CHECK(call)                                                        \
    do {                                                                        \
        cudaError_t _e = (call);                                               \
        if (_e != cudaSuccess) {                                               \
            fprintf(stderr, "CUDA error %s:%d  %s\n",                        \
                    __FILE__, __LINE__, cudaGetErrorString(_e));               \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

/* ─── Validation kernel ──────────────────────────────────────────────────── */
/**
 * For each input x[i], compute W0 and W-1 on GPU and store:
 *   out_w0[i]  = LambertW0_d(x_w0[i])
 *   out_wm1[i] = LambertWm1_d(x_wm1[i])
 *   out_res[i] = lw__residual(x_w0[i], out_w0[i])   (internal sanity check)
 */
extern "C" __global__ void lw_validate_kernel(
    const double* __restrict__ x_w0,
    const double* __restrict__ x_wm1,
    double*       __restrict__ out_w0,
    double*       __restrict__ out_wm1,
    double*       __restrict__ out_res_w0,
    double*       __restrict__ out_res_wm1,
    int N)
{
    int i = blockDim.x * blockIdx.x + threadIdx.x;
    if (i >= N) return;

    double w0  = LambertW0_d (x_w0 [i]);
    double wm1 = LambertWm1_d(x_wm1[i]);

    out_w0  [i] = w0;
    out_wm1 [i] = wm1;
    out_res_w0 [i] = lw__residual(x_w0 [i], w0);
    out_res_wm1[i] = lw__residual(x_wm1[i], wm1);
}

/* ─── Throughput kernel (W0 only, float) ─────────────────────────────────── */
extern "C" __global__ void lw_throughput_kernel(
    const double* __restrict__ x,
    double*       __restrict__ out,
    int N)
{
    int i = blockDim.x * blockIdx.x + threadIdx.x;
    if (i >= N) return;
    out[i] = LambertW0_d(x[i]);
}

/* ─── JFM one-diode model example kernel ─────────────────────────────────── */
/**
 * Explicit I(V) solution for the single-diode (5-parameter) model:
 *
 *   I(V) = (Iph + I0 - V/Rsh) / (1 + Rs/Rsh)
 *          - (n·Vt/Rs) · W0( arg )
 *
 *   arg = (I0·Rs / (n·Vt)) · exp( (Rs·(Iph+I0) + V) / (n·Vt·(1+Rs/Rsh)) )
 *
 * Demonstration of how to embed LambertW0_d in a physics kernel.
 * For real JFM use, parameters would come from fitting (Monte Carlo, LM, etc.).
 */
extern "C" __global__ void jfm_single_diode_kernel(
    const double* __restrict__ V,
    double*       __restrict__ I,
    double Iph, double I0, double n, double Vt, double Rs, double Rsh,
    int N)
{
    int i = blockDim.x * blockIdx.x + threadIdx.x;
    if (i >= N) return;

    double v   = V[i];
    double denom = 1.0 + Rs / Rsh;
    double arg   = (I0 * Rs / (n * Vt))
                 * exp((Rs * (Iph + I0) + v) / (n * Vt * denom));

    I[i] = (Iph + I0 - v / Rsh) / denom - (n * Vt / Rs) * LambertW0_d(arg);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Host-side test driver
 * ════════════════════════════════════════════════════════════════════════════*/

static void section(const char* name) {
    printf("\n=== %s ===\n", name);
}

static double rel_err(double gpu, double ref) {
    if (ref == 0.0) return std::abs(gpu - ref);
    return std::abs(gpu - ref) / std::abs(ref);
}

int main() {
    /* ── Device info ── */
    cudaDeviceProp prop;
    CUDA_CHECK(cudaGetDeviceProperties(&prop, 0));
    printf("Device: %s  (sm_%d%d)\n", prop.name, prop.major, prop.minor);

    /* ── Build test point grids ── */
    const int N = 1 << 16;  // 65536 points

    // W0 domain: [-1/e, large)
    // Sample logarithmically on (0, 500] and linearly on [-1/e, 0)
    std::vector<double> h_x_w0(N), h_x_wm1(N);
    const double inv_e = 1.0 / std::exp(1.0);

    for (int i = 0; i < N; ++i) {
        double t = (double)i / (N - 1);
        // W0: mix negative region [-1/e, 0) with positive [0, 500]
        if (i < N / 4) {
            // Dense sampling near branch point [-1/e, -0.30]
            h_x_w0[i] = -inv_e + t * 4.0 * (-0.30 - (-inv_e));
        } else if (i < N / 2) {
            // [-0.30, 0)
            h_x_w0[i] = -0.30 + (t * 2.0 - 0.5) * 0.30;
        } else {
            // [0, 500] logarithmically
            double s = (double)(i - N/2) / (N/2 - 1);
            h_x_w0[i] = std::pow(500.0, s) - 1.0;
        }

        // W-1 domain: (-1/e, 0)   — log-uniform in |x|
        double s = (double)i / (N - 1);
        double log_x = -40.0 * s;   // |x| ∈ [1e-40, 1]
        h_x_wm1[i] = -(inv_e * std::exp(log_x));  // stays in (-1/e, 0)
    }

    /* ── GPU buffers ── */
    double *d_x_w0, *d_x_wm1, *d_w0, *d_wm1, *d_res_w0, *d_res_wm1;
    CUDA_CHECK(cudaMalloc(&d_x_w0,    N * sizeof(double)));
    CUDA_CHECK(cudaMalloc(&d_x_wm1,   N * sizeof(double)));
    CUDA_CHECK(cudaMalloc(&d_w0,      N * sizeof(double)));
    CUDA_CHECK(cudaMalloc(&d_wm1,     N * sizeof(double)));
    CUDA_CHECK(cudaMalloc(&d_res_w0,  N * sizeof(double)));
    CUDA_CHECK(cudaMalloc(&d_res_wm1, N * sizeof(double)));

    CUDA_CHECK(cudaMemcpy(d_x_w0,  h_x_w0.data(),  N*sizeof(double), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_x_wm1, h_x_wm1.data(), N*sizeof(double), cudaMemcpyHostToDevice));

    /* ── Run validation kernel ── */
    int block = 256, grid = (N + block - 1) / block;
    lw_validate_kernel<<<grid, block>>>(
        d_x_w0, d_x_wm1, d_w0, d_wm1, d_res_w0, d_res_wm1, N);
    CUDA_CHECK(cudaDeviceSynchronize());

    /* ── Download results ── */
    std::vector<double> h_w0(N), h_wm1(N), h_res_w0(N), h_res_wm1(N);
    CUDA_CHECK(cudaMemcpy(h_w0.data(),      d_w0,      N*sizeof(double), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(h_wm1.data(),     d_wm1,     N*sizeof(double), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(h_res_w0.data(),  d_res_w0,  N*sizeof(double), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(h_res_wm1.data(), d_res_wm1, N*sizeof(double), cudaMemcpyDeviceToHost));

    /* ── Compare against CPU implementations ── */
    section("W0 validation");

    double max_err_veberic_w0    = 0.0;
    double max_err_fukushima_w0  = 0.0;
    double max_res_w0            = 0.0;
    int    nan_count_w0          = 0;

    for (int i = 0; i < N; ++i) {
        double x = h_x_w0[i];
        double gpu_val = h_w0[i];

        if (std::isnan(gpu_val)) { nan_count_w0++; continue; }

        double ref_v = utl::LambertW<0>(x);
        double ref_f = Fukushima::LambertW0(x);

        if (!std::isnan(ref_v))
            max_err_veberic_w0   = std::max(max_err_veberic_w0,   rel_err(gpu_val, ref_v));
        if (!std::isnan(ref_f))
            max_err_fukushima_w0 = std::max(max_err_fukushima_w0, rel_err(gpu_val, ref_f));

        max_res_w0 = std::max(max_res_w0, h_res_w0[i]);
    }

    printf("  Max rel error vs Veberic CPU :  %.3e\n", max_err_veberic_w0);
    printf("  Max rel error vs Fukushima CPU: %.3e\n", max_err_fukushima_w0);
    printf("  Max residual |w*exp(w)-x|/|x|: %.3e\n", max_res_w0);
    printf("  NaN results (expected 0 for valid x): %d\n", nan_count_w0);

    section("W-1 validation");

    double max_err_veberic_wm1   = 0.0;
    double max_err_fukushima_wm1 = 0.0;
    double max_res_wm1           = 0.0;
    int    nan_count_wm1         = 0;

    for (int i = 0; i < N; ++i) {
        double x = h_x_wm1[i];
        double gpu_val = h_wm1[i];

        if (std::isnan(gpu_val)) { nan_count_wm1++; continue; }

        double ref_v = utl::LambertW<-1>(x);
        double ref_f = Fukushima::LambertWm1(x);

        if (!std::isnan(ref_v))
            max_err_veberic_wm1   = std::max(max_err_veberic_wm1,   rel_err(gpu_val, ref_v));
        if (!std::isnan(ref_f))
            max_err_fukushima_wm1 = std::max(max_err_fukushima_wm1, rel_err(gpu_val, ref_f));

        max_res_wm1 = std::max(max_res_wm1, h_res_wm1[i]);
    }

    printf("  Max rel error vs Veberic CPU :  %.3e\n", max_err_veberic_wm1);
    printf("  Max rel error vs Fukushima CPU: %.3e\n", max_err_fukushima_wm1);
    printf("  Max residual |w*exp(w)-x|/|x|: %.3e\n", max_res_wm1);
    printf("  NaN results (expected 0 for valid x): %d\n", nan_count_wm1);

    /* ── Specific known values (from lambertw.cxx examples) ── */
    section("Known values");

    struct { double x; int branch; double expected; } cases[] = {
        { 3.14,   0,  1.073395661239825    },
        {-0.2,    0, -0.2591711018190738   },
        { 1.0,    0,  0.5671432904097838   },
        { 0.0,    0,  0.0                  },
        {-0.2,   -1, -2.542641357773526    },
        {-0.3,   -1, -1.781337023421628    },
        {-1e-10, -1, -25.78808890543161    },   /* deep W-1, log recursion region */
        { 1e3,    0,  5.249602852401596    },   /* large x, asymptotic region      */
    };

    printf("  %-10s  %-4s  %-22s  %-22s  %s\n",
           "x", "br", "GPU", "expected", "rel_err");
    for (auto& c : cases) {
        // run single-point kernel via inline call (host can't call __device__)
        // We'll use cudaMemcpy trick: copy 1 element, run kernel, read back
        double hx = c.x, hw;
        double *dx, *dw;
        CUDA_CHECK(cudaMalloc(&dx, sizeof(double)));
        CUDA_CHECK(cudaMalloc(&dw, sizeof(double)));
        CUDA_CHECK(cudaMemcpy(dx, &hx, sizeof(double), cudaMemcpyHostToDevice));
        if (c.branch == 0) {
            lw_throughput_kernel<<<1, 1>>>(dx, dw, 1);
        } else {
            // reuse validation kernel with same x for both branches trick
            double dummy;
            double *ddummy, *ddummy2;
            CUDA_CHECK(cudaMalloc(&ddummy,  sizeof(double)));
            CUDA_CHECK(cudaMalloc(&ddummy2, sizeof(double)));
            lw_validate_kernel<<<1,1>>>(ddummy, dx, ddummy, dw, ddummy2, ddummy2, 1);
            CUDA_CHECK(cudaFree(ddummy));
            CUDA_CHECK(cudaFree(ddummy2));
        }
        CUDA_CHECK(cudaDeviceSynchronize());
        CUDA_CHECK(cudaMemcpy(&hw, dw, sizeof(double), cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaFree(dx));
        CUDA_CHECK(cudaFree(dw));

        printf("  %-10g  %-4d  %-22.15g  %-22.15g  %.2e\n",
               c.x, c.branch, hw, c.expected, rel_err(hw, c.expected));
    }

    /* ── Throughput ── */
    section("Throughput");

    const int N_thr = 1 << 20;  // 1M evaluations
    double *d_xt, *d_ot;
    CUDA_CHECK(cudaMalloc(&d_xt, N_thr * sizeof(double)));
    CUDA_CHECK(cudaMalloc(&d_ot, N_thr * sizeof(double)));

    // Fill with values spread across [0, 500]
    std::vector<double> h_xt(N_thr);
    for (int i = 0; i < N_thr; ++i)
        h_xt[i] = 500.0 * (double)i / (N_thr - 1);
    CUDA_CHECK(cudaMemcpy(d_xt, h_xt.data(), N_thr*sizeof(double), cudaMemcpyHostToDevice));

    // Warm-up
    int g2 = (N_thr + 255) / 256;
    lw_throughput_kernel<<<g2, 256>>>(d_xt, d_ot, N_thr);
    CUDA_CHECK(cudaDeviceSynchronize());

    // Timed run (average of 5)
    cudaEvent_t ev0, ev1;
    CUDA_CHECK(cudaEventCreate(&ev0));
    CUDA_CHECK(cudaEventCreate(&ev1));

    const int REPS = 5;
    CUDA_CHECK(cudaEventRecord(ev0));
    for (int r = 0; r < REPS; ++r)
        lw_throughput_kernel<<<g2, 256>>>(d_xt, d_ot, N_thr);
    CUDA_CHECK(cudaEventRecord(ev1));
    CUDA_CHECK(cudaEventSynchronize(ev1));

    float ms_total;
    CUDA_CHECK(cudaEventElapsedTime(&ms_total, ev0, ev1));
    float ms_avg = ms_total / REPS;
    double mevals_per_s = (double)N_thr / (ms_avg * 1e-3) / 1e6;

    printf("  W0  batch N=%d: %.2f ms per batch  (%.0f M eval/s)\n",
           N_thr, ms_avg, mevals_per_s);

    /* ── JFM demo ── */
    section("JFM single-diode example (solar cell)");
    {
        const int NV = 512;
        // Realistic silicon solar cell parameters
        double Iph = 8.0,     // A  (short-circuit current density)
               I0  = 1e-10,   // A  (dark saturation current)
               n   = 1.3,     //    (ideality factor)
               Vt  = 0.02585, // V  (thermal voltage at 300K)
               Rs  = 0.01,    // Ω  (series resistance)
               Rsh = 200.0;   // Ω  (shunt resistance)

        std::vector<double> h_V(NV), h_I(NV);
        double Voc_approx = n * Vt * std::log(Iph / I0 + 1.0);
        for (int i = 0; i < NV; ++i)
            h_V[i] = Voc_approx * (double)i / (NV - 1);

        double *d_V, *d_I;
        CUDA_CHECK(cudaMalloc(&d_V, NV * sizeof(double)));
        CUDA_CHECK(cudaMalloc(&d_I, NV * sizeof(double)));
        CUDA_CHECK(cudaMemcpy(d_V, h_V.data(), NV*sizeof(double), cudaMemcpyHostToDevice));

        jfm_single_diode_kernel<<<(NV+255)/256, 256>>>(
            d_V, d_I, Iph, I0, n, Vt, Rs, Rsh, NV);
        CUDA_CHECK(cudaDeviceSynchronize());
        CUDA_CHECK(cudaMemcpy(h_I.data(), d_I, NV*sizeof(double), cudaMemcpyDeviceToHost));

        printf("  Parameters: Iph=%.1fA I0=%.1e n=%.2f Vt=%.4f Rs=%.2fΩ Rsh=%.0fΩ\n",
               Iph, I0, n, Vt, Rs, Rsh);
        printf("  V_approx_oc = %.4f V\n", Voc_approx);
        printf("  I at V=0.00 V: %.6f A  (should be ≈ Iph=%.1f)\n", h_I[0],    Iph);
        printf("  I at V=0.50 V: %.6f A\n", h_I[NV/4]);
        printf("  I at V=Voc  V: %.6f A  (should be ≈ 0)\n",   h_I[NV-1]);

        CUDA_CHECK(cudaFree(d_V));
        CUDA_CHECK(cudaFree(d_I));
    }

    /* ── Cleanup ── */
    CUDA_CHECK(cudaFree(d_x_w0));   CUDA_CHECK(cudaFree(d_x_wm1));
    CUDA_CHECK(cudaFree(d_w0));     CUDA_CHECK(cudaFree(d_wm1));
    CUDA_CHECK(cudaFree(d_res_w0)); CUDA_CHECK(cudaFree(d_res_wm1));
    CUDA_CHECK(cudaFree(d_xt));     CUDA_CHECK(cudaFree(d_ot));
    CUDA_CHECK(cudaEventDestroy(ev0)); CUDA_CHECK(cudaEventDestroy(ev1));

    printf("\nDone.\n");
    return 0;
}
