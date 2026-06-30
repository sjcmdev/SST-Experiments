// main.cpp
// WAŻNA KOLEJNOŚĆ NAGŁÓWKÓW:
//   1. glad.h MUSI być przed glfw3.h (definiuje prototypy OpenGL)
//   2. imgui_impl_opengl3.h po glad.h
//   3. implot.h po imgui.h

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imnodes.h"
#include "implot.h"

#include <cstdio>
#include <cstdlib>

#include "app.hpp"
#include "gui.hpp"

// ---------------------------------------------------------------------------
static void glfwErrorCallback(int error, const char *description)
{
    fprintf(stderr, "[GLFW] Error %d: %s\n", error, description);
}

// ---------------------------------------------------------------------------
int main()
{
    // -----------------------------------------------------------------------
    // 1. GLFW
    // -----------------------------------------------------------------------
    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit())
    {
        fprintf(stderr, "[GLFW] glfwInit() failed\n");
        return 1;
    }

    // OpenGL 3.3 Core — wystarczy dla ImGui, obsługiwane przez wszystkie
    // współczesne GPU NVIDIA.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // Na macOS byłoby potrzebne GLFW_OPENGL_FORWARD_COMPAT — tu nie trzeba.

    GLFWwindow *window = glfwCreateWindow(
        1400, 900,
        "Nealder Mead Experiment",
        nullptr, nullptr);
    if (!window)
    {
        fprintf(stderr, "[GLFW] Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // VSync: ogranicz do refresh rate monitora

    // -----------------------------------------------------------------------
    // 2. GLAD — MUSI być po glfwMakeContextCurrent
    // -----------------------------------------------------------------------
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
    {
        fprintf(stderr, "[GLAD] Failed to initialize OpenGL loader\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    // Opcjonalnie: wypisz wersję OpenGL dla weryfikacji
    fprintf(stdout, "[OpenGL] Version: %s\n", glGetString(GL_VERSION));
    fprintf(stdout, "[OpenGL] Renderer: %s\n", glGetString(GL_RENDERER));

    // -----------------------------------------------------------------------
    // 3. ImGui — MUSI być po GLAD
    // -----------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");


    ImNodes::CreateContext();
    ImNodes::StyleColorsDark(); // opcjonalnie
    ImPlot::CreateContext();

    AppState state;
    appInit(state); // query GPU + generacja sygnałów


    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // Nowa klatka ImGui
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Renderuj UI aplikacji
        guiRender(state);

        // Zakończ klatkę ImGui i wyrenderuj do OpenGL
        ImGui::Render();

        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.12f, 0.12f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow *backupCurrentContext = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backupCurrentContext);
        }

        glfwSwapBuffers(window);
    }

   

    ImNodes::DestroyContext();
    ImPlot::DestroyContext();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
