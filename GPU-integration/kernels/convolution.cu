extern "C" __global__ void convolution(
    const double* __restrict__ A,
    const double* __restrict__ B,
    double* __restrict__ C,
    int N,
    double dt)
{
    int n = blockDim.x * blockIdx.x + threadIdx.x;
    if (n >= N) {
        return;
    }

    double sum = 0.0;
    for (int k = 0; k < N; ++k) {
        int idx = n - k;
        if (idx >= 0 && idx < N) {
            sum += A[k] * B[idx];
        }
    }

    C[n] = sum * dt;
}
