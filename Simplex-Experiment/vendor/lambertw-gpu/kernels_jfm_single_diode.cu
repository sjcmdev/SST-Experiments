// kernels/jfm_single_diode.cu
//
// Jawne (closed-form) rozwiązanie I(V) dla modelu jednodiodowego (5P)
// przy użyciu funkcji Lambert W.
//
// Ten plik NIE jest kompilowany przez system budowania (Premake5/MSVC).
// Jest wczytywany jako tekst w runtime przez loadKernelSourceFromFile()
// i kompilowany przez NVRTC wewnątrz KernelManager::compile().
//
// Wymaga: header injection patch z Faza 6 (faza6_lambertw_gpu_integracja.md)
// — #include poniżej jest rozwiązywany przez nvrtcCreateProgram(headerSrcs=...),
// NIE przez preprocesor systemu plików.
//
// Model:
//   I(V) = (Iph + I0 - V/Rsh) / (1 + Rs/Rsh)  -  (n*Vt/Rs) * W0(arg)
//
//   arg  = (I0*Rs / (n*Vt)) * exp( (Rs*(Iph+I0) + V) / (n*Vt*(1+Rs/Rsh)) )
//
// Parametry:
//   Iph  [A]   — prąd fotogenerowany
//   I0   [A]   — prąd nasycenia diody
//   n    [-]   — współczynnik idealności
//   Vt   [V]   — napięcie termiczne (kT/q, ≈0.02585 V przy 300K)
//   Rs   [Ω]   — rezystancja szeregowa
//   Rsh  [Ω]   — rezystancja bocznikowa

#include "lambert_w_device.cuh"

/**
 * Batch I(V) — dla danego zestawu napięć V[], oblicz odpowiadające prądy I[].
 * Jeden wątek = jeden punkt pomiarowy.
 */
extern "C" __global__ void jfm_iv_single_diode(
    const double* __restrict__ V,
    double*       __restrict__ I,
    double Iph, double I0, double n, double Vt, double Rs, double Rsh,
    int N)
{
    int idx = blockDim.x * blockIdx.x + threadIdx.x;
    if (idx >= N) return;

    double v     = V[idx];
    double denom = 1.0 + Rs / Rsh;
    double arg   = (I0 * Rs / (n * Vt))
                 * exp((Rs * (Iph + I0) + v) / (n * Vt * denom));

    I[idx] = (Iph + I0 - v / Rsh) / denom - (n * Vt / Rs) * LambertW0_d(arg);
}

/**
 * Residuals = I_measured - I_model, dla LM fittingu na GPU.
 * Batch po punktach pomiarowych jednej krzywej I-V.
 */
extern "C" __global__ void jfm_residuals_single_diode(
    const double* __restrict__ V,
    const double* __restrict__ I_meas,
    double*       __restrict__ residuals,
    double Iph, double I0, double n, double Vt, double Rs, double Rsh,
    int N)
{
    int idx = blockDim.x * blockIdx.x + threadIdx.x;
    if (idx >= N) return;

    double v       = V[idx];
    double denom   = 1.0 + Rs / Rsh;
    double arg     = (I0 * Rs / (n * Vt))
                   * exp((Rs * (Iph + I0) + v) / (n * Vt * denom));
    double I_model = (Iph + I0 - v / Rsh) / denom
                   - (n * Vt / Rs) * LambertW0_d(arg);

    residuals[idx] = I_meas[idx] - I_model;
}

/**
 * Wariant Monte Carlo: P zestawów parametrów × N punktów napięcia.
 * Layout: params[p*6 + {0..5}] = {Iph, I0, n, Vt, Rs, Rsh} dla próby p.
 * Output: I_out[p*N + i] = prąd dla próby p, punktu napięcia i.
 *
 * Przydatne dla Monte Carlo Δχ² (zgodnie z notatką w PROJECT_CONTEXT.md:
 * "Monte Carlo Δχ² liczone względem krzywej idealnej, zgodnie z tw. Wilksa").
 */
extern "C" __global__ void jfm_mc_single_diode(
    const double* __restrict__ V,        // [N] — wspólna siatka napięć
    const double* __restrict__ params,   // [P*6] — {Iph,I0,n,Vt,Rs,Rsh} per próbka
    double*       __restrict__ I_out,    // [P*N] — wynik
    int N, int P)
{
    int idx = blockDim.x * blockIdx.x + threadIdx.x;
    int total = N * P;
    if (idx >= total) return;

    int p = idx / N;   // indeks próbki MC
    int i = idx % N;   // indeks punktu napięcia

    double Iph = params[p*6 + 0];
    double I0  = params[p*6 + 1];
    double n   = params[p*6 + 2];
    double Vt  = params[p*6 + 3];
    double Rs  = params[p*6 + 4];
    double Rsh = params[p*6 + 5];

    double v     = V[i];
    double denom = 1.0 + Rs / Rsh;
    double arg   = (I0 * Rs / (n * Vt))
                 * exp((Rs * (Iph + I0) + v) / (n * Vt * denom));

    I_out[idx] = (Iph + I0 - v / Rsh) / denom - (n * Vt / Rs) * LambertW0_d(arg);
}
