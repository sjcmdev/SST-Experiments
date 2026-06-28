#include "kernel_manager.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

KernelManager::KernelManager() = default;

KernelManager::~KernelManager()
{
    unloadModule();
}

void KernelManager::unloadModule()
{
    if (m_module) {
        cuModuleUnload(m_module);
    }

    m_module = nullptr;
    m_functions.clear();
    m_ready = false;
}

NvrtcCompileResult KernelManager::compile(
    const std::string& source,
    int capMajor,
    int capMinor,
    const std::vector<std::string>& expectedFunctions)
{
    NvrtcCompileResult result;

    if (source.empty()) {
        result.log = "ERROR: source string is empty";
        return result;
    }

    if (expectedFunctions.empty()) {
        result.log = "ERROR: no expected functions specified";
        return result;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    nvrtcProgram program = nullptr;
    nvrtcResult nvrtcError = nvrtcCreateProgram(
        &program,
        source.c_str(),
        "runtime_kernel.cu",
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

    for (const std::string& functionName : expectedFunctions) {
        CUfunction function = nullptr;
        driverError = cuModuleGetFunction(&function, m_module, functionName.c_str());
        if (driverError == CUDA_SUCCESS) {
            m_functions[functionName] = function;
        } else {
            const char* message = "unknown";
            cuGetErrorString(driverError, &message);
            result.log += "\n[DRIVER] missing function \"";
            result.log += functionName;
            result.log += "\": ";
            result.log += message;
        }
    }

    if (m_functions.empty()) {
        result.log += "\nNo functions loaded.";
        unloadModule();
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

KernelHandle KernelManager::getFunction(const std::string& name) const
{
    auto it = m_functions.find(name);
    if (it == m_functions.end()) {
        return nullptr;
    }

    return reinterpret_cast<KernelHandle>(it->second);
}

int KernelManager::functionCount() const
{
    return static_cast<int>(m_functions.size());
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
