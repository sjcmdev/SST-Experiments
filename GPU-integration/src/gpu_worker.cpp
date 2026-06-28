#include "gpu_worker.hpp"

#include <cuda_runtime_api.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

static void computeCpuReference(
    const std::vector<double>& signalA,
    const std::vector<double>& signalB,
    std::vector<double>& output,
    int steps,
    double duration)
{
    output.assign(steps, 0.0);
    const double dt = duration / static_cast<double>(steps - 1);

    for (int n = 0; n < steps; ++n) {
        double sum = 0.0;
        for (int k = 0; k < steps; ++k) {
            const int idx = n - k;
            if (idx >= 0 && idx < steps) {
                sum += signalA[k] * signalB[idx];
            }
        }
        output[n] = sum * dt;
    }
}

GpuWorkerThread::GpuWorkerThread()
{
    m_thread = std::thread(&GpuWorkerThread::workerLoop, this);
}

GpuWorkerThread::~GpuWorkerThread()
{
    shutdown();
}

std::future<AsyncConvResult> GpuWorkerThread::submit(
    const std::vector<double>& signalA,
    const std::vector<double>& signalB,
    const std::vector<double>& cpuSignalA,
    const std::vector<double>& cpuSignalB,
    int N,
    int cpuSteps,
    double duration,
    KernelHandle kernelFunc)
{
    if (m_busy.load() || m_exitRequested.load()) {
        fprintf(stderr, "[GpuWorker] submit rejected - worker busy or stopping\n");
        return std::future<AsyncConvResult>{};
    }

    GpuTask task;
    task.signalA = signalA;
    task.signalB = signalB;
    task.cpuSignalA = cpuSignalA;
    task.cpuSignalB = cpuSignalB;
    task.N = N;
    task.cpuSteps = cpuSteps;
    task.duration = duration;
    task.kernelFunc = kernelFunc;

    std::future<AsyncConvResult> future = task.promise.get_future();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_busy.store(true);
        m_taskQueue.push(std::move(task));
    }
    m_cv.notify_one();

    return future;
}

bool GpuWorkerThread::isBusy() const
{
    return m_busy.load();
}

bool GpuWorkerThread::isRunning() const
{
    return m_initialized.load() && !m_exitRequested.load();
}

void GpuWorkerThread::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_exitRequested.store(true);
    }
    m_cv.notify_all();

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void GpuWorkerThread::workerLoop()
{
    cudaStream_t stream = nullptr;
    cudaError_t streamError = cudaStreamCreate(&stream);
    if (streamError == cudaSuccess) {
        m_stream = reinterpret_cast<void*>(stream);
    } else {
        fprintf(stderr, "[GpuWorker] cudaStreamCreate failed: %s\n",
                cudaGetErrorString(streamError));
    }

    m_initialized.store(true);

    while (true) {
        GpuTask task;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] {
                return !m_taskQueue.empty() || m_exitRequested.load();
            });

            if (m_exitRequested.load() && m_taskQueue.empty()) {
                break;
            }

            task = std::move(m_taskQueue.front());
            m_taskQueue.pop();
        }

        AsyncConvResult asyncResult;
        asyncResult.gpuOutput.assign(task.N, 0.0);

        auto cpuStart = std::chrono::steady_clock::now();
        computeCpuReference(
            task.cpuSignalA,
            task.cpuSignalB,
            asyncResult.cpuOutput,
            task.cpuSteps,
            task.duration);
        auto cpuEnd = std::chrono::steady_clock::now();

        const double gpuDt = task.duration / static_cast<double>(task.N - 1);
        initCudaDriver();
        runConvolution(
            task.signalA.data(),
            task.signalB.data(),
            asyncResult.gpuOutput.data(),
            task.N,
            gpuDt,
            task.kernelFunc,
            asyncResult.info);

        asyncResult.info.cpuReferenceMs =
            std::chrono::duration<double, std::milli>(cpuEnd - cpuStart).count();

        if (asyncResult.info.success && task.cpuSteps == task.N) {
            double maxError = 0.0;
            for (int i = 0; i < task.N; ++i) {
                maxError = std::max(maxError,
                    std::abs(asyncResult.gpuOutput[i] - asyncResult.cpuOutput[i]));
            }
            asyncResult.validationAvailable = true;
            asyncResult.info.maxAbsError = maxError;
            asyncResult.info.validationPassed = (maxError < 1e-9);
        }

        m_busy.store(false);
        task.promise.set_value(std::move(asyncResult));
    }

    if (stream) {
        cudaStreamDestroy(stream);
        m_stream = nullptr;
    }

    m_initialized.store(false);
    fprintf(stdout, "[GpuWorker] Worker stopped.\n");
}
