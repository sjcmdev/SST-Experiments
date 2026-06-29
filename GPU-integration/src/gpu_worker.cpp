#include "gpu_worker.hpp"

#include <cuda_runtime_api.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

static void computeCpuReference(
    const std::vector<double> &signalA,
    const std::vector<double> &signalB,
    std::vector<double> &output,
    int steps,
    double duration)
{
    output.assign(steps, 0.0);
    const double dt = duration / static_cast<double>(steps - 1);

    for (int n = 0; n < steps; ++n)
    {
        double sum = 0.0;
        for (int k = 0; k < steps; ++k)
        {
            const int idx = n - k;
            if (idx >= 0 && idx < steps)
            {
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

std::future<AsyncConvResult> GpuWorkerThread::submitTask(GpuTask task)
{
    if (m_busy.load())
        return {};
    std::future<AsyncConvResult> fut = task.promise.get_future();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_busy.store(true);
        m_taskQueue.push(std::move(task));
    }
    m_cv.notify_one();
    return fut;
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

    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

void GpuWorkerThread::workerLoop()
{
    cudaStream_t stream = nullptr;
    cudaError_t streamError = cudaStreamCreate(&stream);
    if (streamError == cudaSuccess)
    {
        m_stream = reinterpret_cast<void *>(stream);
    }
    else
    {
        fprintf(stderr, "[GpuWorker] cudaStreamCreate failed: %s\n",
                cudaGetErrorString(streamError));
    }

    m_initialized.store(true);

    while (true)
    {
        GpuTask task;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]
                      { return !m_taskQueue.empty() || m_exitRequested.load(); });

            if (m_exitRequested.load() && m_taskQueue.empty())
            {
                break;
            }

            task = std::move(m_taskQueue.front());
            m_taskQueue.pop();
        }

        AsyncConvResult asyncResult;
        asyncResult.signalA.resize(task.N, 0.0);
        asyncResult.signalB.resize(task.N, 0.0);
        asyncResult.convOut.resize(task.N, 0.0);

        // Faza 4: uruchom cały pipeline GPU
        runPipeline(
            task.genAFunc,
            task.genBFunc,
            task.convFunc,
            task.N,
            task.dt,
            asyncResult.signalA.data(),
            asyncResult.signalB.data(),
            asyncResult.convOut.data(),
            asyncResult.info // PipelineResult
        );

        // CPU reference + walidacja (w watku GPU, nie blokuje UI)
        if (asyncResult.info.success && !task.skipCpuReference)
        {
            const int cpuSteps = std::max(2, task.cpuSteps);
            asyncResult.cpuSignalA.assign(cpuSteps, 0.0);
            asyncResult.cpuSignalB.assign(cpuSteps, 0.0);
            asyncResult.cpuConvOut.assign(cpuSteps, 0.0);

            auto cpuStart = std::chrono::high_resolution_clock::now();

            computeSignalCpu(task.graphA, asyncResult.cpuSignalA.data(), cpuSteps, task.cpuDt);

            if (task.graphB.isValid())
            {
                computeSignalCpu(task.graphB, asyncResult.cpuSignalB.data(), cpuSteps, task.cpuDt);
            }
            else
            {
                for (int i = 0; i < cpuSteps; ++i)
                {
                    const double t = static_cast<double>(i) * task.cpuDt;
                    asyncResult.cpuSignalB[i] = (t >= 0.60 && t < 0.80) ? 1.0 : 0.0;
                }
            }

            for (int n = 0; n < cpuSteps; ++n)
            {
                double sum = 0.0;
                for (int k = 0; k < cpuSteps; ++k)
                {
                    const int idx = n - k;
                    if (idx >= 0 && idx < cpuSteps)
                    {
                        sum += asyncResult.cpuSignalA[k] * asyncResult.cpuSignalB[idx];
                    }
                }
                asyncResult.cpuConvOut[n] = sum * task.cpuDt;
            }

            auto cpuEnd = std::chrono::high_resolution_clock::now();
            asyncResult.cpuReferenceMs =
                std::chrono::duration<double, std::milli>(cpuEnd - cpuStart).count();

            if (cpuSteps == task.N)
            {
                double maxErr = 0.0;
                for (int i = 0; i < task.N; ++i)
                {
                    const double err = std::fabs(asyncResult.convOut[i] - asyncResult.cpuConvOut[i]);
                    if (err > maxErr)
                    {
                        maxErr = err;
                    }
                }
                asyncResult.info.maxAbsError = static_cast<float>(maxErr);
                asyncResult.validationAvailable = true;
            }
        }
        else if (asyncResult.info.success && task.skipCpuReference)
        {
            asyncResult.info.maxAbsError = -1.0f;
            asyncResult.validationAvailable = false;
        }
        m_busy.store(false);
        task.promise.set_value(std::move(asyncResult));
    }

    if (stream)
    {
        cudaStreamDestroy(stream);
        m_stream = nullptr;
    }

    m_initialized.store(false);
}
