local cudaPath = os.getenv("CUDA_PATH")
assert(
    cudaPath ~= nil and cudaPath ~= "",
    "\n\nERROR: CUDA_PATH is not set.\n" ..
    "Install CUDA Toolkit and ensure CUDA_PATH environment variable is defined.\n" ..
    "Expected format: C:\\Program Files\\NVIDIA GPU Computing Toolkit\\CUDA\\vX.Y\n"
)

local cudaGencodeOptions = table.concat({
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

local vendorRoot = "../vendor"

workspace "GPU-integration"
    configurations { "Debug", "Release" }
    platforms { "x64" }
    startproject "GPU-integration"

project "GPU-integration"
    location "."
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    targetdir "build/bin/%{cfg.buildcfg}/%{prj.name}"
    objdir "build/obj/%{cfg.buildcfg}/%{prj.name}"

    files {
        "src/**.hpp",
        "src/**.cpp",
        "src/**.cu",
        "kernels/*.cu",
        path.join(vendorRoot, "imgui/imgui.cpp"),
        path.join(vendorRoot, "imgui/imgui_draw.cpp"),
        path.join(vendorRoot, "imgui/imgui_tables.cpp"),
        path.join(vendorRoot, "imgui/imgui_widgets.cpp"),
        path.join(vendorRoot, "imgui/backends/imgui_impl_glfw.cpp"),
        path.join(vendorRoot, "imgui/backends/imgui_impl_opengl3.cpp"),
        path.join(vendorRoot, "implot/implot.cpp"),
        path.join(vendorRoot, "implot/implot_items.cpp"),
        path.join(vendorRoot, "glad/src/glad.c"),
    }

    includedirs {
        "src",
        path.join(vendorRoot, "imgui"),
        path.join(vendorRoot, "imgui/backends"),
        path.join(vendorRoot, "implot"),
        path.join(vendorRoot, "glad/include"),
        path.join(vendorRoot, "glfw/include"),
        cudaPath .. "/include",
    }

    libdirs {
        path.join(vendorRoot, "glfw/lib-vc2022"),
        cudaPath .. "/lib/x64",
    }

    links {
        "cudart",
        "nvrtc",
        "cuda",
        "glfw3dll",
        "opengl32",
        "gdi32",
        "user32",
        "shell32",
    }

    postbuildcommands {
        '{COPYFILE} "../vendor/glfw/lib-vc2022/glfw3.dll" "%{cfg.targetdir}"'
    }

    filter "system:windows"
        systemversion "latest"
        defines { "_CRT_SECURE_NO_WARNINGS" }

    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"
        optimize "Off"
        defines { "_DEBUG", "DEBUG" }

    filter "configurations:Release"
        runtime "Release"
        symbols "Off"
        optimize "Speed"
        defines { "NDEBUG" }

    filter { "files:src/**.cu" }
        buildmessage "NVCC: %{file.relpath}"
        buildoutputs { "$(IntDir)%{file.basename}.obj" }

    filter { "files:kernels/**.cu" }
        buildaction "None"

    filter { "files:src/**.cu", "configurations:Debug" }
        buildcommands {
            '"$(CUDA_PATH)/bin/nvcc"'
            .. ' -c'
            .. ' -G'
            .. ' -g'
            .. ' -O0'
            .. ' -std=c++17'
            .. ' ' .. cudaGencodeOptions
            .. ' -Xcompiler "/MDd /Zi /FS"'
            .. ' -I"$(CUDA_PATH)/include"'
            .. ' -I"src"'
            .. ' -o "$(IntDir)%{file.basename}.obj"'
            .. ' "%{file.relpath}"'
        }

    filter { "files:src/**.cu", "configurations:Release" }
        buildcommands {
            '"$(CUDA_PATH)/bin/nvcc"'
            .. ' -c'
            .. ' -O2'
            .. ' -std=c++17'
            .. ' ' .. cudaGencodeOptions
            .. ' -Xcompiler "/MD"'
            .. ' -DNDEBUG'
            .. ' -I"$(CUDA_PATH)/include"'
            .. ' -I"src"'
            .. ' -o "$(IntDir)%{file.basename}.obj"'
            .. ' "%{file.relpath}"'
        }

    filter {}
