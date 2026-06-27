-- premake5.lua (workspace root)
-- One Visual Studio solution for all experiments with a shared vendor folder.

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

local excludedRoots = {
    [".git"] = true,
    [".vs"] = true,
    ["build"] = true,
    ["vendor"] = true,
    ["glad"] = true,
    ["glfw-3.4.bin.WIN64"] = true,
    ["tools"] = true,
}

local function discoverExperiments()
    local experiments = {}
    for _, dir in ipairs(os.matchdirs("*")) do
        local name = path.getname(dir)
        if not excludedRoots[name] and os.isfile(path.join(dir, "src", "main.cpp")) then
            table.insert(experiments, dir)
        end
    end
    table.sort(experiments)
    return experiments
end

local experimentDirs = discoverExperiments()
assert(#experimentDirs > 0, "No experiments found. Expected '<experiment>/src/main.cpp'.")

workspace "SST-Experiments"
    configurations { "Debug", "Release" }
    platforms { "x64" }
    startproject(path.getname(experimentDirs[1]))

    filter "system:windows"
        systemversion "latest"

    filter {}

local function configureExperimentProject(expDir)
    local expName = path.getname(expDir)

    project(expName)
        location(expDir)
        kind "ConsoleApp"
        language "C++"
        cppdialect "C++17"
        targetdir "build/bin/%{cfg.buildcfg}/%{prj.name}"
        objdir "build/obj/%{cfg.buildcfg}/%{prj.name}"

        files {
            path.join(expDir, "src/**.hpp"),
            path.join(expDir, "src/**.cpp"),
            path.join(expDir, "src/**.cu"),
            "vendor/imgui/imgui.cpp",
            "vendor/imgui/imgui_draw.cpp",
            "vendor/imgui/imgui_tables.cpp",
            "vendor/imgui/imgui_widgets.cpp",
            "vendor/imgui/backends/imgui_impl_glfw.cpp",
            "vendor/imgui/backends/imgui_impl_opengl3.cpp",
            "vendor/implot/implot.cpp",
            "vendor/implot/implot_items.cpp",
            "vendor/glad/src/glad.c",
        }

        includedirs {
            path.join(expDir, "src"),
            "vendor/imgui",
            "vendor/imgui/backends",
            "vendor/implot",
            "vendor/glad/include",
            "vendor/glfw/include",
            cudaPath .. "/include",
        }

        libdirs {
            "vendor/glfw/lib-vc2022",
            cudaPath .. "/lib/x64",
        }

        links {
            "cudart",
            "glfw3dll",
            "opengl32",
            "gdi32",
            "user32",
            "shell32",
        }

        postbuildcommands {
            '{COPYFILE} "%{wks.location}/vendor/glfw/lib-vc2022/glfw3.dll" "%{cfg.targetdir}"'
        }

        filter "system:windows"
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

        filter { "files:" .. path.join(expDir, "src/**.cu") }
            buildmessage "NVCC: %{file.relpath}"
            buildoutputs { "$(IntDir)%{file.basename}.obj" }

        filter { "files:" .. path.join(expDir, "src/**.cu"), "configurations:Debug" }
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
                .. ' -I"%{prj.location}/src"'
                .. ' -o "$(IntDir)%{file.basename}.obj"'
                .. ' "%{file.relpath}"'
            }

        filter { "files:" .. path.join(expDir, "src/**.cu"), "configurations:Release" }
            buildcommands {
                '"$(CUDA_PATH)/bin/nvcc"'
                .. ' -c'
                .. ' -O2'
                .. ' -std=c++17'
                .. ' ' .. cudaGencodeOptions
                .. ' -Xcompiler "/MD"'
                .. ' -DNDEBUG'
                .. ' -I"$(CUDA_PATH)/include"'
                .. ' -I"%{prj.location}/src"'
                .. ' -o "$(IntDir)%{file.basename}.obj"'
                .. ' "%{file.relpath}"'
            }

        filter {}
end

for _, expDir in ipairs(experimentDirs) do
    configureExperimentProject(expDir)
end
