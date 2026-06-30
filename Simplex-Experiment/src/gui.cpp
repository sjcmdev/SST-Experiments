#include "gui.hpp"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"

#include <cstdio>
#include <vector>


static void guiWindowSettings()
{
    ImGui::Begin("UI Settings");

    ImGuiStyle &style = ImGui::GetStyle();
    ImGui::SliderFloat("Font scale", &style.FontScaleMain, 0.75f, 2.00f, "%.2f");
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        style.FontScaleMain = 1.0f;
    }

    ImGui::End();
}

void guiRender(AppState &appState)
{
    ImGui::DockSpaceOverViewport();
    guiWindowSettings();
}
