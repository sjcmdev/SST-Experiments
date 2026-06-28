project "ImGui"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    staticruntime "off"

    targetdir ("../build/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../build/bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        "*.h",
        "*.cpp",
        "backends/imgui_impl_glfw.h",
        "backends/imgui_impl_glfw.cpp",
        "backends/imgui_impl_opengl3.h",
        "backends/imgui_impl_opengl3.cpp"
    }

    includedirs {
        ".",
        "backends",
        "../GLFW/include",
        "../GLAD/generated/include"
    }

    filter "system:linux"
        pic "On"

    filter {}