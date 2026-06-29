#include "gui.hpp"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"

#include <cstdio>
#include <vector>

static constexpr int GPU_N_EXP_MIN = 10;
static constexpr int GPU_N_EXP_MAX = 16;
static constexpr int CPU_REFERENCE_WARN_STEPS = 8192;

static void guiWindowDevice(const AppState &appState);
static void guiWindowCompute(AppState &appState);
static void guiWindowSettings();
static void guiWindowPlot(const AppState &appState);
static void renderGeneratedCodeWindow(AppState &state);
static void renderNodeEditorWindows(AppState &state);
static void renderSignalPreviewWindow(NodeGraph &graph, const char *title, const char *plotLabel);
static void renderSignalPreview(NodeGraph &graph, const char *label);
static void renderNodeGraph(NodeGraph &graph, bool &codeDirty, bool &dataDirty, ImNodesEditorContext *editorCtx, const char *canvasId);
static void renderSingleNode(SignalNode &node, NodeGraph &graph, bool &codeDirty, bool &dataDirty);
static void renderAddNodeMenu(NodeGraph &graph, bool &codeDirty, bool &dataDirty);
static void addNodeToGraph(NodeGraph &graph, NodeType type, bool &codeDirty, bool &dataDirty);

static bool sliderDouble(const char *label, double &value, double minValue, double maxValue, const char *format)
{
    return ImGui::SliderScalar(label, ImGuiDataType_Double, &value, &minValue, &maxValue, format);
}

static int stepCountToLog2(int steps)
{
    int exp = GPU_N_EXP_MIN;
    while (exp < GPU_N_EXP_MAX && (1 << exp) < steps)
    {
        ++exp;
    }
    return exp;
}

static void compileSignalGraphs(AppState &state)
{
    state.generatedCode = buildSignalModule(state.graphA, state.graphB);
    state.lastSignalCompile = state.signalKernelMgr.compile(
        state.generatedCode,
        state.deviceInfo.computeCapabilityMajor,
        state.deviceInfo.computeCapabilityMinor,
        {"generateSignalA", "generateSignalB"});

    state.codeDirty = !state.lastSignalCompile.success;
    if (state.lastSignalCompile.success)
    {
        state.dataDirty = true;
    }
}

static void renderSignalCompileStatus(AppState &state)
{
    ImGui::SameLine();
    if (state.codeDirty)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Kod nieaktualny");
    }
    else if (state.lastSignalCompile.success)
    {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                           "OK (%.0f ms)", state.lastSignalCompile.compileTimeMs);
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "BLAD NVRTC");
    }

    if (!state.lastSignalCompile.success && !state.lastSignalCompile.log.empty())
    {
        ImGui::BeginChild("##sig_log", ImVec2(0, 60), true);
        ImGui::TextWrapped("%s", state.lastSignalCompile.log.c_str());
        ImGui::EndChild();
    }
}

void guiRender(AppState &appState)
{
    appPollAndSubmit(appState);

    ImGui::DockSpaceOverViewport();
    guiWindowDevice(appState);
    guiWindowCompute(appState);
    guiWindowSettings();
    guiWindowPlot(appState);
    renderNodeEditorWindows(appState);
    renderGeneratedCodeWindow(appState);
}

static void guiWindowDevice(const AppState &appState)
{
    ImGui::Begin("CUDA Device");

    if (!appState.cudaAvail)
    {
        ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f), "No CUDA device found.");
        ImGui::TextDisabled("GPU features are unavailable.");
        ImGui::End();
        return;
    }

    const auto &device = appState.deviceInfo;
    ImGui::Text("Name:               %s", device.name);
    ImGui::Text("Compute Capability: %d.%d",
                device.computeCapabilityMajor, device.computeCapabilityMinor);
    ImGui::Text("Total Memory:       %.1f GB",
                static_cast<double>(device.totalMemoryBytes) / (1024.0 * 1024.0 * 1024.0));
    ImGui::Text("Double Precision:   %s", device.supportsDouble ? "Yes" : "No");

    ImGui::End();
}

static void guiWindowCompute(AppState &appState)
{
    ImGui::Begin("Convolution");

    bool changed = false;
    int gpuExp = stepCountToLog2(appState.N);
    if (ImGui::SliderInt("log2(N) GPU", &gpuExp, GPU_N_EXP_MIN, GPU_N_EXP_MAX))
    {
        const int newN = 1 << gpuExp;
        if (newN != appState.N)
        {
            appState.N = newN;
            changed = true;
        }
    }
    ImGui::SameLine();
    ImGui::Text("N = %d", appState.N);

    changed |= ImGui::SliderInt("CPU steps", &appState.cpuSteps, 128, 65536);
    if (changed)
    {
        appMarkDirty(appState);
    }

    if (appState.N > CPU_REFERENCE_WARN_STEPS || appState.cpuSteps > CPU_REFERENCE_WARN_STEPS)
    {
        const double estimateSeconds =
            static_cast<double>(appState.cpuSteps) * static_cast<double>(appState.cpuSteps) / 1e9 * 2.0;
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                           "CPU reference skipped for sample counts > %d (~%.1f s avoided)",
                           CPU_REFERENCE_WARN_STEPS,
                           estimateSeconds);
    }

    ImGui::Checkbox("Auto recompute dirty data", &appState.autoRecomputeDirty);
    ImGui::SameLine();
    ImGui::TextDisabled("Dirty: %s", appState.dataDirty ? "yes" : "no");
    ImGui::TextDisabled("Auto recompute also recompiles generated GPU signal code when graph params change.");
    ImGui::Separator();

    ImGui::Text("GPU signal length: N = %d", appState.N);
    ImGui::Text("CPU reference steps: %d", appState.cpuSteps);
    ImGui::Text("Duration: %.2f s", appState.T);
    ImGui::Separator();

    if (appState.computing)
    {
        const char *spinner[] = {"|", "/", "-", "\\"};
        const int spinnerIndex = static_cast<int>(ImGui::GetTime() * 6.0) % 4;
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                           "%s Computing...", spinner[spinnerIndex]);
    }
    else
    {
        const bool canRun = appState.cudaAvail && appState.kernelMgr.isReady();
        if (!canRun)
        {
            ImGui::BeginDisabled();
        }
        const bool clicked = ImGui::Button("Run Convolution (GPU + CPU ref.)");
        if (!canRun)
        {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled(appState.cudaAvail ? "(kernel not ready)" : "(no GPU)");
        }

        if (clicked)
        {
            appRunComputation(appState);
        }
    }

    if (appState.hasResult)
    {
        ImGui::Separator();
        const auto &result = appState.lastResult;

        if (!result.success)
        {
            ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f),
                               "Error: %s", result.errorMessage);
        }
        else
        {
            ImGui::Text("Generate GPU signals: %8.3f ms", result.transferToGpuMs);
            ImGui::Text("GPU convolution:      %8.3f ms", result.kernelMs);
            ImGui::Text("GPU readback:         %8.3f ms", result.transferFromGpuMs);
            if (result.maxAbsError < 0.0)
            {
                ImGui::TextDisabled("CPU reference:        skipped");
            }
            else
            {
                ImGui::Text("CPU reference:        %8.3f ms", result.cpuReferenceMs);
            }
            ImGui::Separator();

            if (appState.validationAvailable)
            {
                ImGui::Text("Max absolute error:  %.6e", result.maxAbsError);
                ImGui::TextColored(result.validationPassed
                                       ? ImVec4(0.2f, 1.f, 0.2f, 1.f)
                                       : ImVec4(1.f, 0.6f, 0.f, 1.f),
                                   result.validationPassed ? "Status: OK" : "Status: VALIDATION FAILED");
            }
            else if (result.maxAbsError < 0.0)
            {
                ImGui::TextDisabled("maxAbsError: (pominieto dla liczby probek > 8192)");
            }
            else
            {
                ImGui::TextDisabled("Validation skipped: CPU/GPU steps differ.");
            }
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Kernel NVRTC");
    ImGui::Text("File: %s", appState.kernelFilePath.empty()
                              ? "kernels/convolution.cu"
                              : appState.kernelFilePath.c_str());
    ImGui::Checkbox("Auto run after reload", &appState.kernelAutoRerun);

    const bool canReloadKernel = appState.cudaAvail && !appState.computing;
    if (!canReloadKernel)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Reload kernel"))
    {
        if (!initCudaDriver())
        {
            appState.lastCompile.success = false;
            appState.lastCompile.log = "Cannot initialize CUDA Driver context.";
        }
        else
        {
            if (appState.kernelFilePath.empty())
            {
                appState.kernelFilePath = "kernels/convolution.cu";
            }

            std::string source = loadKernelSourceFromFile(appState.kernelFilePath);
            if (source.empty())
            {
                appState.lastCompile.success = false;
                appState.lastCompile.log = "Cannot read kernel file.";
            }
            else
            {
                appState.kernelSource = source;
                appState.lastCompile = appState.kernelMgr.compile(
                    appState.kernelSource,
                    appState.deviceInfo.computeCapabilityMajor,
                    appState.deviceInfo.computeCapabilityMinor,
                    {"convolution"});

                if (appState.lastCompile.success)
                {
                    appMarkDirty(appState);
                    if (appState.kernelAutoRerun)
                    {
                        appRunComputation(appState);
                    }
                }
            }
        }
    }
    if (!canReloadKernel)
    {
        ImGui::EndDisabled();
    }

    if (appState.kernelMgr.isReady())
    {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.3f, 1.0f),
                           "Status: OK (%.1f ms)", appState.lastCompile.compileTimeMs);
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Status: no compiled kernel");
    }

    if (!appState.lastCompile.log.empty() && appState.lastCompile.log != "OK")
    {
        ImGui::BeginChild("##nvrtc_log", ImVec2(0, 90), true);
        ImGui::TextWrapped("%s", appState.lastCompile.log.c_str());
        ImGui::EndChild();
    }

    ImGui::End();
}

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

static ImPlotSpec makeLineSpec(const ImVec4 &color, float weight)
{
    ImPlotSpec spec;
    spec.LineColor = color;
    spec.LineWeight = weight;
    return spec;
}

static void guiWindowPlot(const AppState &appState)
{
    ImGui::Begin("Signal Plot");

    if (appState.plotT.empty())
    {
        ImGui::TextDisabled("No data to display.");
        ImGui::End();
        return;
    }

    if (ImPlot::BeginPlot("##signals", ImVec2(-1, -1)))
    {
        ImPlot::SetupAxes("Time [s]", "Amplitude");

        ImPlot::PlotLine("Signal A GPU", appState.plotT.data(), appState.plotA.data(),
                         static_cast<int>(appState.plotT.size()),
                         makeLineSpec(ImVec4(0.3f, 0.7f, 1.0f, 1.f), 1.5f));

        ImPlot::PlotLine("Signal B GPU", appState.plotT.data(), appState.plotB.data(),
                         static_cast<int>(appState.plotT.size()),
                         makeLineSpec(ImVec4(1.0f, 0.7f, 0.2f, 1.f), 1.5f));

        if (appState.hasResult && appState.lastResult.success && !appState.plotC.empty())
        {
            ImPlot::PlotLine("Convolution GPU", appState.plotT.data(), appState.plotC.data(),
                             static_cast<int>(appState.plotT.size()),
                             makeLineSpec(ImVec4(0.2f, 1.0f, 0.4f, 1.f), 2.0f));
        }

        if (appState.hasResult && !appState.plotCpuT.empty() && !appState.plotCref.empty())
        {
            ImPlot::PlotLine("Convolution CPU ref", appState.plotCpuT.data(), appState.plotCref.data(),
                             static_cast<int>(appState.plotCpuT.size()),
                             makeLineSpec(ImVec4(1.0f, 0.3f, 0.3f, 0.8f), 1.5f));
        }

        ImPlot::EndPlot();
    }

    ImGui::End();
}

static void renderOneNodeEditorWindow(AppState &state,
                                      NodeGraph &graph,
                                      ImNodesEditorContext *editorCtx,
                                      const char *title,
                                      const char *canvasId)
{
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title))
    {
        ImGui::End();
        return;
    }

    if (editorCtx)
    {
        ImNodes::EditorContextSet(editorCtx);
    }

    if (ImGui::Button("Dodaj node"))
    {
        ImGui::OpenPopup("##toolbar_add_node_popup");
    }
    if (ImGui::BeginPopup("##toolbar_add_node_popup"))
    {
        renderAddNodeMenu(graph, state.codeDirty, state.dataDirty);
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset canvas"))
    {
        ImNodes::EditorContextResetPanning(ImVec2(0.0f, 0.0f));
    }

    ImGui::SameLine();
    const bool canRegenerate = !state.computing;
    if (!canRegenerate)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Generuj kod + Kompiluj NVRTC"))
    {
        compileSignalGraphs(state);
    }
    if (!canRegenerate)
    {
        ImGui::EndDisabled();
    }

    renderSignalCompileStatus(state);
    ImGui::Separator();

    renderNodeGraph(graph, state.codeDirty, state.dataDirty, editorCtx, canvasId);

    ImGui::End();
}

static void renderNodeEditorWindows(AppState &state)
{
    renderOneNodeEditorWindow(state, state.graphA, state.graphAEditorCtx,
                              "Node Editor - Sygnal A", "SignalAEditorCanvas");
    renderOneNodeEditorWindow(state, state.graphB, state.graphBEditorCtx,
                              "Node Editor - Sygnal B", "SignalBEditorCanvas");
    renderSignalPreviewWindow(state.graphA, "Live Preview - Sygnal A", "Signal A preview");
    renderSignalPreviewWindow(state.graphB, "Live Preview - Sygnal B", "Signal B preview");
}

static void renderSingleNode(SignalNode &node, NodeGraph &graph, bool &codeDirty, bool &dataDirty)
{
    NodeTypeInfo info = getNodeTypeInfo(node.type);

    if (node.type == NodeType::Output)
    {
        ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(140, 40, 40, 200));
    }
    else if (info.numInputs == 0)
    {
        ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(40, 80, 140, 200));
    }
    else
    {
        ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(40, 120, 60, 200));
    }

    ImNodes::BeginNode(node.id);
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(info.label);
    ImNodes::EndNodeTitleBar();

    for (int i = 0; i < info.numInputs; ++i)
    {
        ImNodes::BeginInputAttribute(node.inputAttrIds[i]);
        ImGui::Text("in %d", i + 1);
        ImNodes::EndInputAttribute();
    }

    for (int i = 0; i < info.numParams; ++i)
    {
        ImGui::PushItemWidth(120.0f);
        char label[32];
        snprintf(label, sizeof(label), "%s##%d_%d", info.paramNames[i], node.id, i);
        if (ImGui::DragFloat(label, &node.params[i], 0.005f))
        {
            codeDirty = true;
            dataDirty = true;
        }
        ImGui::PopItemWidth();
    }

    if (node.type != NodeType::Output)
    {
        ImNodes::BeginOutputAttribute(node.outputAttrId);
        ImGui::Text("out");
        ImNodes::EndOutputAttribute();
    }

    ImNodes::EndNode();
    ImNodes::PopColorStyle();
}

static void renderNodeGraph(NodeGraph &graph, bool &codeDirty, bool &dataDirty, ImNodesEditorContext *editorCtx, const char *canvasId)
{
    if (editorCtx)
    {
        ImNodes::EditorContextSet(editorCtx);
    }
    ImGui::PushID(canvasId);

    ImNodes::BeginNodeEditor();

    for (auto &node : graph.nodes)
    {
        renderSingleNode(node, graph, codeDirty, dataDirty);
    }

    for (const auto &link : graph.links)
    {
        ImNodes::Link(link.id, link.srcAttrId, link.dstAttrId);
    }

    ImNodes::EndNodeEditor();

    auto markGraphDirty = [&]()
    {
        codeDirty = true;
        dataDirty = true;
    };

    int srcAttr = 0;
    int dstAttr = 0;
    if (ImNodes::IsLinkCreated(&srcAttr, &dstAttr))
    {
        int created = graph.addLink(srcAttr, dstAttr);
        if (created < 0)
        {
            created = graph.addLink(dstAttr, srcAttr);
        }
        if (created >= 0)
        {
            markGraphDirty();
        }
    }

    int destroyedLink = 0;
    if (ImNodes::IsLinkDestroyed(&destroyedLink))
    {
        graph.removeLink(destroyedLink);
        markGraphDirty();
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        const int numSelectedLinks = ImNodes::NumSelectedLinks();
        if (numSelectedLinks > 0)
        {
            std::vector<int> selectedLinkIds(numSelectedLinks);
            ImNodes::GetSelectedLinks(selectedLinkIds.data());
            for (int linkId : selectedLinkIds)
            {
                graph.removeLink(linkId);
                markGraphDirty();
            }
        }

        const int numSelectedNodes = ImNodes::NumSelectedNodes();
        if (numSelectedNodes > 0)
        {
            std::vector<int> selectedIds(numSelectedNodes);
            ImNodes::GetSelectedNodes(selectedIds.data());
            for (int selectedId : selectedIds)
            {
                const SignalNode *node = graph.findNodeById(selectedId);
                if (node && node->type != NodeType::Output)
                {
                    graph.removeNode(selectedId);
                    markGraphDirty();
                }
            }
        }
    }

    if ((ImNodes::IsEditorHovered() || ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup("##add_node_popup");
    }
    if (ImGui::BeginPopup("##add_node_popup"))
    {
        ImGui::TextDisabled("Dodaj node:");
        ImGui::Separator();
        renderAddNodeMenu(graph, codeDirty, dataDirty);
        ImGui::EndPopup();
    }

    ImGui::PopID();
}

static void addNodeToGraph(NodeGraph &graph, NodeType type, bool &codeDirty, bool &dataDirty)
{
    const int nodeId = graph.addNode(type);
    ImNodes::SetNodeScreenSpacePos(nodeId, ImGui::GetMousePos());
    codeDirty = true;
    dataDirty = true;
}

static void renderAddNodeMenu(NodeGraph &graph, bool &codeDirty, bool &dataDirty)
{
    if (ImGui::MenuItem("Constant"))  addNodeToGraph(graph, NodeType::Constant, codeDirty, dataDirty);
    if (ImGui::MenuItem("Sine"))      addNodeToGraph(graph, NodeType::Sine, codeDirty, dataDirty);
    if (ImGui::MenuItem("Cosine"))    addNodeToGraph(graph, NodeType::Cosine, codeDirty, dataDirty);
    if (ImGui::MenuItem("Rectangle")) addNodeToGraph(graph, NodeType::Rectangle, codeDirty, dataDirty);
    if (ImGui::MenuItem("Triangle"))  addNodeToGraph(graph, NodeType::Triangle, codeDirty, dataDirty);
    if (ImGui::MenuItem("Sawtooth"))  addNodeToGraph(graph, NodeType::Sawtooth, codeDirty, dataDirty);
    if (ImGui::MenuItem("Gaussian"))  addNodeToGraph(graph, NodeType::Gaussian, codeDirty, dataDirty);
    if (ImGui::MenuItem("ExpDecay"))  addNodeToGraph(graph, NodeType::ExpDecay, codeDirty, dataDirty);
    if (ImGui::MenuItem("Heaviside")) addNodeToGraph(graph, NodeType::Heaviside, codeDirty, dataDirty);
    if (ImGui::MenuItem("Sinc"))      addNodeToGraph(graph, NodeType::Sinc, codeDirty, dataDirty);
    ImGui::Separator();
    if (ImGui::MenuItem("Sum"))       addNodeToGraph(graph, NodeType::Sum, codeDirty, dataDirty);
    if (ImGui::MenuItem("Product"))   addNodeToGraph(graph, NodeType::Product, codeDirty, dataDirty);
    if (ImGui::MenuItem("Scale"))     addNodeToGraph(graph, NodeType::Scale, codeDirty, dataDirty);
    if (ImGui::MenuItem("TimeShift")) addNodeToGraph(graph, NodeType::TimeShift, codeDirty, dataDirty);
    if (ImGui::MenuItem("Reflect"))   addNodeToGraph(graph, NodeType::Reflect, codeDirty, dataDirty);
}

static void renderSignalPreviewWindow(NodeGraph &graph, const char *title, const char *plotLabel)
{
    ImGui::SetNextWindowSize(ImVec2(520, 260), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title))
    {
        ImGui::End();
        return;
    }

    renderSignalPreview(graph, plotLabel);
    ImGui::End();
}

static void renderSignalPreview(NodeGraph &graph, const char *label)
{
    if (!graph.isValid())
    {
        ImGui::TextDisabled("Live preview: podlacz kompletny graf do OUTPUT.");
        return;
    }

    constexpr int previewCount = 512;
    static std::vector<double> previewTime(previewCount);
    static std::vector<double> previewValue(previewCount);

    const double previewDt = APP_SIGNAL_T / static_cast<double>(previewCount - 1);
    for (int i = 0; i < previewCount; ++i)
    {
        previewTime[i] = static_cast<double>(i) * previewDt;
    }

    computeSignalCpu(graph, previewValue.data(), previewCount, previewDt);

    ImVec2 plotSize = ImGui::GetContentRegionAvail();
    if (plotSize.x <= 0.0f)
    {
        plotSize.x = -1.0f;
    }
    if (plotSize.y <= 0.0f)
    {
        plotSize.y = -1.0f;
    }

    if (ImPlot::BeginPlot(label, plotSize))
    {
        ImPlot::SetupAxes("t", "value");
        ImPlot::PlotLine("signal", previewTime.data(), previewValue.data(), previewCount);
        ImPlot::EndPlot();
    }
}

static void renderGeneratedCodeWindow(AppState &state)
{
    ImGui::SetNextWindowSize(ImVec2(600, 300), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Wygenerowany kod CUDA"))
    {
        ImGui::End();
        return;
    }

    if (state.generatedCode.empty())
    {
        ImGui::TextDisabled("(brak wygenerowanego kodu)");
    }
    else
    {
        ImGui::InputTextMultiline(
            "##gencode",
            const_cast<char *>(state.generatedCode.c_str()),
            state.generatedCode.size() + 1,
            ImVec2(-1, -1),
            ImGuiInputTextFlags_ReadOnly);
    }

    ImGui::End();
}
