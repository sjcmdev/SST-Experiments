
cudaPath = os.getenv("CUDA_PATH")
assert(
    cudaPath ~= nil and cudaPath ~= "",
    "\n\nERROR: CUDA_PATH is not set.\n" ..
    "Install CUDA Toolkit and ensure CUDA_PATH environment variable is defined.\n" ..
    "Expected format: C:\\Program Files\\NVIDIA GPU Computing Toolkit\\CUDA\\vX.Y\n"
)

cudaGencodeOptions = table.concat({
    "-gencode arch=compute_50,code=sm_50",
    "-gencode arch=compute_52,code=sm_52",
    "-gencode arch=compute_60,code=sm_60",
    "-gencode arch=compute_61,code=sm_61",
    "-gencode arch=compute_70,code=sm_70",
    "-gencode arch=compute_75,code=sm_75",
    "-gencode arch=compute_80,code=sm_80",
    "-gencode arch=compute_86,code=sm_86",
    "-gencode arch=compute_89,code=sm_89",
    "-gencode arch=compute_90,code=sm_90",
    "-gencode arch=compute_50,code=compute_50",
}, " ")

vendorRoot = "vendor"


workspace "SST-Experiments"
    location "."
    configurations { "Debug", "Release" }
    platforms { "x64" }
    startproject("Simplex-Experiment")

    filter "system:windows"
        systemversion "latest"

    filter {}

dofile(path.join("GPU-integration", "premake5.lua"))
dofile(path.join("simplex-experiment", "premake5.lua"))
