local vendorRoot = "../vendor"

project "GPU-integration"
    location "."
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    targetdir(path.join("%{wks.location}", "build/bin/%{cfg.buildcfg}/%{prj.name}"))
    objdir(path.join("%{wks.location}", "build/obj/%{cfg.buildcfg}/%{prj.name}"))

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
        path.join(vendorRoot, "imnodes/imnodes.cpp"),
        path.join(vendorRoot, "glad/src/glad.c"),
    }

    includedirs {
        "src",
        path.join(vendorRoot, "imgui"),
        path.join(vendorRoot, "imgui/backends"),
        path.join(vendorRoot, "implot"),
        path.join(vendorRoot, "glad/include"),
        path.join(vendorRoot, "imnodes"),
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
        '{COPYFILE} "%{wks.location}/vendor/glfw/lib-vc2022/glfw3.dll" "%{cfg.targetdir}"'
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
            .. ' "%{file.abspath}"'
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
            .. ' "%{file.abspath}"'
        }

    filter {}
