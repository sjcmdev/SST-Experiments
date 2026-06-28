#pragma once

#include "cuda_interface.hpp"

#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

struct GpuTask {
    std::vector<double> signalA;
    std::vector<double> signalB;
    std::vector<double> cpuSignalA;
    std::vector<double> cpuSignalB;
    int N = 0;
    int cpuSteps = 0;
    double duration = 1.0;
    KernelHandle kernelFunc = nullptr;
    std::promise<AsyncConvResult> promise;

    GpuTask() = default;
    GpuTask(GpuTask&&) = default;
    GpuTask& operator=(GpuTask&&) = default;
    GpuTask(const GpuTask&) = delete;
    GpuTask& operator=(const GpuTask&) = delete;
};

class GpuWorkerThread {
public:
    GpuWorkerThread();
    ~GpuWorkerThread();

    std::future<AsyncConvResult> submit(
        const std::vector<double>& signalA,
        const std::vector<double>& signalB,
        const std::vector<double>& cpuSignalA,
        const std::vector<double>& cpuSignalB,
        int N,
        int cpuSteps,
        double duration,
        KernelHandle kernelFunc);

    bool isBusy() const;
    bool isRunning() const;
    void shutdown();

private:
    void workerLoop();

    std::thread m_thread;
    std::queue<GpuTask> m_taskQueue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_busy{false};
    std::atomic<bool> m_exitRequested{false};
    std::atomic<bool> m_initialized{false};
    void* m_stream = nullptr;
};
