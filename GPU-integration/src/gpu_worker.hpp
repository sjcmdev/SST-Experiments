#pragma once

#include "cuda_interface.hpp"
#include "signal_graph.hpp"
#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

struct GpuTask
{
    // Faza 4: żadnych wektorów sygnałów — generowane na GPU
    int N;
    double dt; // dt = 1.0 / (N - 1)
    int cpuSteps;
    double cpuDt;

    KernelHandle genAFunc; // z signalKernelMgr.getFunction("generateSignalA")
    KernelHandle genBFunc; // z signalKernelMgr.getFunction("generateSignalB")
    KernelHandle convFunc; // z convKernelMgr.getFunction("convolution")

    // CPU referencja — do walidacji
    // Przechowujemy kopię grafów (żeby liczyć CPU reference w wątku)
    NodeGraph graphA;
    NodeGraph graphB;

    std::promise<AsyncConvResult> promise;

    GpuTask() = default;
    GpuTask(GpuTask &&) = default;
    GpuTask &operator=(GpuTask &&) = default;
    GpuTask(const GpuTask &) = delete;
    GpuTask &operator=(const GpuTask &) = delete;
};

class GpuWorkerThread {
public:
    GpuWorkerThread();
    ~GpuWorkerThread();

    std::future<AsyncConvResult> submitTask(GpuTask task);

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
