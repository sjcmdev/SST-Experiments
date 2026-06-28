#include "kernel_manager.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

static constexpr const char* KERNEL_FUNC_NAME = "convolution";

KernelManager::KernelManager() = default;

KernelManager::~KernelManager()
{
    unloadModule();
}

void KernelManager::unloadModule()
{
    if (m_module) {
        cuModuleUnload(m_module);
        m_module = nullptr;
        m_function = nullptr;
        m_ready = false;
    }
}

NvrtcCompileResult KernelManager::compile(
    const std::string& source,
    int capMajor,
    int capMinor)
{
    NvrtcCompileResult result;

    if (source.empty()) {
        result.log = "ERROR: source string is empty";
        return result;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    nvrtcProgram program = nullptr;
    nvrtcResult nvrtcError = nvrtcCreateProgram(
        &program,
        source.c_str(),
        "convolution.cu",
        0,
        nullptr,
        nullptr);
    if (nvrtcError != NVRTC_SUCCESS) {
        result.log = std::string("nvrtcCreateProgram failed: ")
            + nvrtcGetErrorString(nvrtcError);
        return result;
    }

    char archOption[64];
    snprintf(archOption, sizeof(archOption),
             "--gpu-architecture=compute_%d%d", capMajor, capMinor);

    const char* options[] = {
        archOption,
        "--std=c++17"
    };

    nvrtcError = nvrtcCompileProgram(program, 2, options);

    size_t logSize = 0;
    nvrtcGetProgramLogSize(program, &logSize);
    if (logSize > 1) {
        result.log.resize(logSize - 1);
        nvrtcGetProgramLog(program, result.log.data());
    }

    if (nvrtcError != NVRTC_SUCCESS) {
        result.success = false;
        nvrtcDestroyProgram(&program);
        return result;
    }

    size_t ptxSize = 0;
    nvrtcGetPTXSize(program, &ptxSize);
    std::string ptx(ptxSize, '\0');
    nvrtcGetPTX(program, ptx.data());
    nvrtcDestroyProgram(&program);

    unloadModule();

    CUresult driverError = cuModuleLoadData(&m_module, ptx.c_str());
    if (driverError != CUDA_SUCCESS) {
        const char* message = "unknown";
        cuGetErrorString(driverError, &message);
        result.log += "\n[DRIVER] cuModuleLoadData failed: ";
        result.log += message;
        m_module = nullptr;
        return result;
    }

    driverError = cuModuleGetFunction(&m_function, m_module, KERNEL_FUNC_NAME);
    if (driverError != CUDA_SUCCESS) {
        const char* message = "unknown";
        cuGetErrorString(driverError, &message);
        result.log += "\n[DRIVER] cuModuleGetFunction(\"";
        result.log += KERNEL_FUNC_NAME;
        result.log += "\") failed: ";
        result.log += message;
        cuModuleUnload(m_module);
        m_module = nullptr;
        m_function = nullptr;
        return result;
    }

    m_ready = true;

    auto endTime = std::chrono::high_resolution_clock::now();
    result.compileTimeMs =
        std::chrono::duration<float, std::milli>(endTime - startTime).count();
    result.success = true;
    if (result.log.empty()) {
        result.log = "OK";
    }

    return result;
}

bool KernelManager::isReady() const
{
    return m_ready;
}

KernelHandle KernelManager::getFunction() const
{
    return reinterpret_cast<KernelHandle>(m_function);
}

std::string loadKernelSourceFromFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        fprintf(stderr, "[KernelManager] Cannot open: %s\n", path.c_str());
        return "";
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}
