// gui.h
#pragma once
#include "app.hpp"

// Renderuje wszystkie okna ImGui dla jednej klatki.
// Wywołać między ImGui::NewFrame() a ImGui::Render().
void guiRender(AppState &s);