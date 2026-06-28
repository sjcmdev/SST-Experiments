# Faza 4  Node Editor, Generator Kodu CUDA, Skalowalność

> **Dokument dla samodzielnej implementacji.**  
> Fazy 4 i 5 to największy skok złożoności w projekcie. Faza 4 wprowadza trzy
> zupełnie nowe koncepcje naraz: graf węzłów (imnodes), generator kodu CUDA
> i nowy pipeline GPU. Faza 5 to głównie zduplikowanie Fazy 4 dla Sygnału B
> plus suwak N. Czytaj każdą sekcję przed jej implementacją.

---

## 0. Co się zmienia — przegląd obu faz

| Element             | Fazy 1–3                               | Faza 4                                                                               | Faza 5                                  |
| ------------------- | -------------------------------------- | ------------------------------------------------------------------------------------ | --------------------------------------- |
| Źródło sygnałów     | Hardcoded Gaussian + Rectangle (CPU)   | Sygnał A z node grafu → GPU kernel                                                   | Oba sygnały z node grafów → GPU kernele |
| N                   | Stałe 4096                             | Stałe 4096                                                                           | Suwak 1024–65536                        |
| Pipeline GPU        | generateSignalA (CPU) → H2D → convolve | generateSignalA (GPU) → generateSignalB (GPU) → convolve                             | jw. + dynamiczne N                      |
| Nowe biblioteki     | —                                      | imnodes                                                                              | —                                       |
| Nowe pliki          | —                                      | `signal_graph.h/.cpp`, `code_gen.h/.cpp`                                             | minimalne zmiany                        |
| Zmodyfikowane pliki | —                                      | `kernel_manager.*`, `gpu_worker.*`, `cuda_impl.cu`, `app.*`, `gui.*`, `premake5.lua` | `signal_graph.*`, `app.*`, `gui.*`      |

**Cel Fazy 4**: Użytkownik buduje Sygnał A z primitywów w node edytorze.
Generator kodu tworzy string CUDA C++. NVRTC kompiluje. GPU generuje A,
generuje hardcoded B (rectangle), splata.

**Cel Fazy 5**: To samo dla Sygnału B + zmienny N.

---

## 1. Teoria — przeczytaj raz, wrócisz tu gdy coś nie gra

### 1.1 imnodes — node editor na ImGui

`imnodes` to biblioteka warstwowa nad Dear ImGui dodająca interfejs do tworzenia
i edycji grafów węzłów (jak Blender Shader Editor, Unreal Blueprints).

Kluczowe pojęcia:
- **Node (węzeł)**: prostokąt z tytułem, pinami wejściowymi i wyjściowym
- **Attribute (pin)**: punkt wejściowy lub wyjściowy węzła — ma unikalny globalny ID
- **Link (połączenie)**: linia między pinem wyjściowym jednego węzła a wejściowym drugiego

```cpp
// Minimalny przykład (każda klatka ImGui):
ImNodes::BeginNodeEditor();
    ImNodes::BeginNode(nodeId);
        ImNodes::BeginInputAttribute(inputPinId);
            ImGui::Text("input");
        ImNodes::EndInputAttribute();
        ImNodes::BeginOutputAttribute(outputPinId);
            ImGui::Text("output");
        ImNodes::EndOutputAttribute();
    ImNodes::EndNode();
    ImNodes::Link(linkId, srcPinId, dstPinId);
ImNodes::EndNodeEditor();
```

**Ważne**: Wszystkie IDs (node, attribute, link) muszą być unikalne globalnie
i niezmienne przez czas życia obiektu. Używaj rosnącego licznika.

### 1.2 Model danych grafu

Graf sygnałów to DAG (Directed Acyclic Graph — Skierowany Graf Acykliczny).
Dane płyną od generatorów (liście, bez wejść) przez operatory do wyjścia.

```
[Gaussian] ──────→ [Scale(×2)] ──→ [Sum] ──→ [OUTPUT]
[Sine(10Hz)] ────────────────────────┘
```

Węzeł `OUTPUT` jest specjalny — ma jedno wejście, brak wyjścia. Oznacza
koniec grafu i wartość sygnału.

### 1.3 Generacja kodu CUDA z grafu

Kod CUDA jest generowany REKURENCYJNIE od węzła OUTPUT:

```
generateExpr(OUTPUT.input_node, timeVar="t") 
  → generateExpr(Sum, "t")
       → generateExpr(Scale, "t")
            → generateExpr(Gaussian, "t") → "exp(-0.5*(t-0.3)*(t-0.3)/0.0025)"
            → scale = 2.0 → "2.0 * exp(...)"
       → generateExpr(Sine, "t") → "sin(62.83...*t)"
  → "(2.0*exp(...) + sin(...))"
```

Wynikowy kernel:
```cuda
extern "C" __global__ void generateSignalA(double* out, int N, double dt) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N) return;
    double t = (double)i * dt;
    out[i] = (2.0*exp(-0.5*(t-0.30)*(t-0.30)/0.0025) + sin(62.831853...*t));
}
```

**TimeShift**: jedyny węzeł który zmienia `t`. Rekurencja dla jego wejścia
używa innego parametru czasu:

```
generateExpr(TimeShift(τ=0.1), "t")
  → generateExpr(input, "(t - 0.10000000000000001)")
```

---

## 2. Krok 0 — Vendor: dodaj imnodes

### 2.1 Pobierz pliki imnodes

Z https://github.com/Nelarius/imnodes pobierz (lub skopiuj) te pliki:

```
vendor/imnodes/imnodes.h
vendor/imnodes/imnodes.cpp
```

Nie potrzebujesz nic więcej. Jeśli w repozytorium jest `imnodes_internal.h`
jako osobny plik, pobierz też go.

> Alternatywnie: w przeglądarce wejdź na raw.githubusercontent.com/Nelarius/imnodes/master/imnodes.h
> i Ctrl+S (zapisz plik).

### 2.2 Modyfikacja `premake5.lua`

Dodaj do `files`:
```lua
files {
    -- ... istniejące ...
    "vendor/imnodes/imnodes.cpp",
}
```

Dodaj do `includedirs`:
```lua
includedirs {
    -- ... istniejące (imgui, implot, glad, glfw, cuda, src) ...
    "vendor/imnodes",  -- NOWE
}
```

Zregeneruj projekt:
```bat
tools\premake5.exe vs2022
```

---

## 3. Krok 1 — Rozszerzenie `kernel_manager.h` i `kernel_manager.cpp`

### 3.1 Problem

Phase 2 `KernelManager` zarządza jednym CUmodule z jedną CUfunction (`"convolution"`).
W Fazie 4 jeden moduł będzie mieć DWIE funkcje: `"generateSignalA"` i `"generateSignalB"`.

### 3.2 Zmodyfikuj `kernel_manager.h`

Zmień `getFunction()` na `getFunction(const std::string& name)`:

```cpp
// kernel_manager.h — zmodyfikowany (wersja Phase 4)
#pragma once
#include <string>
#include <unordered_map>  // NOWE
#include "cuda_interface.h"
#include <cuda.h>
#include <nvrtc.h>

class KernelManager {
public:
    KernelManager();
    ~KernelManager();

    // Skompiluj source (może zawierać WIELE __global__ funkcji).
    // expectedFunctions: lista nazw funkcji do pobrania z modułu po kompilacji.
    NvrtcCompileResult compile(
        const std::string&              source,
        int                             capMajor,
        int                             capMinor,
        const std::vector<std::string>& expectedFunctions  // NOWE
    );

    bool isReady() const;

    // Zwraca uchwyt do nazwanej funkcji. nullptr jeśli nie znaleziono.
    KernelHandle getFunction(const std::string& name) const;  // ZMIENIONE

    // Ile funkcji zostało załadowanych?
    int functionCount() const;

private:
    void unloadModule();

    CUmodule   m_module = nullptr;
    std::unordered_map<std::string, CUfunction> m_functions;  // NOWE
    bool       m_ready  = false;
};

std::string loadKernelSourceFromFile(const std::string& path);
```

### 3.3 Zmodyfikuj `kernel_manager.cpp`

Podmień sekcje gdzie pobieramy CUfunction:

```cpp
// kernel_manager.cpp — zmieniona sygnatura compile()

NvrtcCompileResult KernelManager::compile(
    const std::string&              source,
    int                             capMajor,
    int                             capMinor,
    const std::vector<std::string>& expectedFunctions)
{
    NvrtcCompileResult result;
    if (source.empty()) { result.log = "source empty"; return result; }

    auto t0 = std::chrono::high_resolution_clock::now();

    // --- KROKI A–D bez zmian (nvrtcCreateProgram, Compile, GetLog, GetPTX) ---
    // [zachowaj cały kod z Fazy 2 aż do nvrtcDestroyProgram]
    // ...

    // Po nvrtcDestroyProgram i unloadModule():
    CUresult cuErr = cuModuleLoadData(&m_module, ptx.c_str());
    if (cuErr != CUDA_SUCCESS) {
        const char* s = "?"; cuGetErrorString(cuErr, &s);
        result.log += "\ncuModuleLoadData: " + std::string(s);
        m_module = nullptr; return result;
    }

    // --- ZMIANA: pobierz WSZYSTKIE oczekiwane funkcje ---
    m_functions.clear();
    for (const auto& fname : expectedFunctions) {
        CUfunction func;
        CUresult ferr = cuModuleGetFunction(&func, m_module, fname.c_str());
        if (ferr == CUDA_SUCCESS) {
            m_functions[fname] = func;
        } else {
            const char* s = "?"; cuGetErrorString(ferr, &s);
            result.log += "\nWarning: function \"" + fname + "\" not found: " + s;
        }
    }

    if (m_functions.empty()) {
        result.log += "\nNo functions loaded — cannot continue";
        cuModuleUnload(m_module); m_module = nullptr;
        return result;
    }

    m_ready = true;
    auto t1 = std::chrono::high_resolution_clock::now();
    result.compileTimeMs = std::chrono::duration<float,std::milli>(t1-t0).count();
    result.success = true;
    if (result.log.empty()) result.log = "OK";
    return result;
}

// --- NOWE implementacje ---

KernelHandle KernelManager::getFunction(const std::string& name) const {
    auto it = m_functions.find(name);
    if (it == m_functions.end()) return nullptr;
    return reinterpret_cast<KernelHandle>(it->second);
}

int KernelManager::functionCount() const {
    return static_cast<int>(m_functions.size());
}
```

### 3.4 Zaktualizuj wywołania w Phase 2/3 kod

Wszędzie gdzie było `kernelMgr.getFunction()` (bez argumentu), zmień na:

```cpp
kernelMgr.getFunction("convolution")
```

W `app.cpp` (lub `gpu_worker.cpp`), wywołanie `compile` zmień na:

```cpp
state.convKernelMgr.compile(
    source, 
    state.deviceInfo.computeCapabilityMajor,
    state.deviceInfo.computeCapabilityMinor,
    {"convolution"}  // NOWE: lista oczekiwanych funkcji
);
```

---

## 4. Krok 2 — Model danych grafu: `src/signal_graph.h`

Utwórz nowy plik. Zawiera cały model danych (typy, węzły, linki, graf).

```cpp
// signal_graph.h
#pragma once
#include <vector>
#include <string>
#include <cmath>

// ---------------------------------------------------------------------------
// Typy węzłów
// ---------------------------------------------------------------------------
enum class NodeType {
    // Generatory (0 wejść, generują sygnał z t)
    Constant   = 0,
    Sine       = 1,
    Cosine     = 2,
    Rectangle  = 3,
    Triangle   = 4,
    Sawtooth   = 5,
    Gaussian   = 6,
    ExpDecay   = 7,
    Heaviside  = 8,
    Sinc       = 9,
    // Operatory (1-2 wejścia)
    Sum        = 10,  // 2 wejścia
    Product    = 11,  // 2 wejścia
    Scale      = 12,  // 1 wejście + param
    TimeShift  = 13,  // 1 wejście + param
    Reflect    = 14,  // 1 wejście (odbicie: T-t)
    // Specjalny
    Output     = 15,  // 1 wejście, brak wyjścia — oznacznik końca grafu
};

// ---------------------------------------------------------------------------
// Informacje o typie węzła (liczba wejść, parametrów, etykiety)
// ---------------------------------------------------------------------------
struct NodeTypeInfo {
    const char*  label;
    int          numInputs;    // 0, 1, lub 2
    int          numParams;    // 0-4 parametrów float
    const char*  paramNames[4];
    float        defaultParams[4];
};

// Wywoływana przez kod — zwraca info o danym typie
NodeTypeInfo getNodeTypeInfo(NodeType type);

// Etykiety dla menu "Dodaj węzeł"
const char* getNodeTypeLabel(NodeType type);

// ---------------------------------------------------------------------------
// Węzeł sygnału
// ---------------------------------------------------------------------------
struct SignalNode {
    int      id;               // unikalny ID węzła (dla imnodes)
    NodeType type;
    float    params[4] = {};   // wartości parametrów (max 4)

    // IDs pinów (dla imnodes) — generowane z globalnego licznika
    int      inputAttrIds[2] = {-1, -1};  // ID pinu wejściowego [0] i [1]
    int      outputAttrId    = -1;         // ID pinu wyjściowego (-1 dla Output)
};

// ---------------------------------------------------------------------------
// Połączenie między węzłami
// ---------------------------------------------------------------------------
struct SignalLink {
    int id;           // unikalny ID linku (dla imnodes)
    int srcAttrId;    // outputAttrId węzła źródłowego
    int dstAttrId;    // inputAttrIds[x] węzła docelowego
};

// ---------------------------------------------------------------------------
// Graf sygnału — jeden pełny graf (dla sygnału A lub B)
// ---------------------------------------------------------------------------
struct NodeGraph {
    std::vector<SignalNode> nodes;
    std::vector<SignalLink> links;
    int nextId = 1;  // globalny licznik ID (rosnie, nigdy nie spada)

    // Dodaj węzeł o danym typie. Zwraca ID nowego węzła.
    int addNode(NodeType type, float posX = 0.0f, float posY = 0.0f);

    // Usuń węzeł (i wszystkie połączenia z nim).
    void removeNode(int nodeId);

    // Dodaj połączenie. Zwraca ID linku lub -1 jeśli nieprawidłowe.
    // Sprawdza: czy dstAttrId nie ma już podłączonego połączenia.
    int addLink(int srcAttrId, int dstAttrId);

    // Usuń połączenie o danym ID.
    void removeLink(int linkId);

    // Znajdź węzeł OUTPUT (powinien być dokładnie jeden).
    // Zwraca nullptr jeśli nie ma.
    const SignalNode* findOutputNode() const;

    // Znajdź węzeł po ID. Zwraca nullptr jeśli nie istnieje.
    SignalNode*       findNodeById(int id);
    const SignalNode* findNodeById(int id) const;

    // Znajdź węzeł podłączony do danego pinu wejściowego.
    // Zwraca -1 jeśli nic nie jest podłączone.
    int findSourceNodeId(int dstAttrId) const;

    // Czy graf jest prawidłowy (ma OUTPUT, wszystkie wymagane wejścia podłączone)?
    bool isValid() const;

    // Zainicjalizuj domyślnym węzłem OUTPUT + jednym generatorem.
    void initDefault(NodeType generatorType, const float* params = nullptr);
};

// ---------------------------------------------------------------------------
// CPU ewaluator — oblicza wartość sygnału w punkcie t (dla referencji CPU)
// ---------------------------------------------------------------------------
// T = całkowity czas (domyślnie 1.0 s)
double evaluateSignal(const NodeGraph& graph, int nodeId, double t, double T = 1.0);

// Oblicz cały sygnał na CPU (N próbek). Wynik w out[0..N-1].
void computeSignalCpu(const NodeGraph& graph, double* out, int N, double dt);
```

---

## 5. Krok 3 — Implementacja `src/signal_graph.cpp`

```cpp
// signal_graph.cpp
#include "signal_graph.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Informacje o typach węzłów
// ---------------------------------------------------------------------------
NodeTypeInfo getNodeTypeInfo(NodeType type) {
    switch (type) {
    case NodeType::Constant:
        return {"Constant", 0, 1, {"value", "", "", ""}, {1.0f, 0, 0, 0}};
    case NodeType::Sine:
        return {"Sine",     0, 2, {"freq(Hz)", "amp", "", ""}, {5.0f, 1.0f, 0, 0}};
    case NodeType::Cosine:
        return {"Cosine",   0, 2, {"freq(Hz)", "amp", "", ""}, {5.0f, 1.0f, 0, 0}};
    case NodeType::Rectangle:
        return {"Rectangle",0, 2, {"t_start", "t_end", "", ""}, {0.25f, 0.75f, 0, 0}};
    case NodeType::Triangle:
        return {"Triangle", 0, 2, {"center", "width", "", ""}, {0.5f, 0.4f, 0, 0}};
    case NodeType::Sawtooth:
        return {"Sawtooth", 0, 1, {"period", "", "", ""},       {0.5f, 0, 0, 0}};
    case NodeType::Gaussian:
        return {"Gaussian", 0, 2, {"mu", "sigma", "", ""},      {0.30f, 0.05f, 0, 0}};
    case NodeType::ExpDecay:
        return {"ExpDecay", 0, 2, {"lambda", "amp", "", ""},    {5.0f, 1.0f, 0, 0}};
    case NodeType::Heaviside:
        return {"Heaviside",0, 1, {"t0", "", "", ""},            {0.5f, 0, 0, 0}};
    case NodeType::Sinc:
        return {"Sinc",     0, 2, {"t0", "scale", "", ""},      {0.5f, 10.0f, 0, 0}};
    case NodeType::Sum:
        return {"Sum",      2, 0, {"", "", "", ""},              {0, 0, 0, 0}};
    case NodeType::Product:
        return {"Product",  2, 0, {"", "", "", ""},              {0, 0, 0, 0}};
    case NodeType::Scale:
        return {"Scale",    1, 1, {"scale", "", "", ""},         {2.0f, 0, 0, 0}};
    case NodeType::TimeShift:
        return {"TimeShift",1, 1, {"tau(s)", "", "", ""},        {0.1f, 0, 0, 0}};
    case NodeType::Reflect:
        return {"Reflect",  1, 0, {"", "", "", ""},              {0, 0, 0, 0}};
    case NodeType::Output:
        return {"OUTPUT",   1, 0, {"", "", "", ""},              {0, 0, 0, 0}};
    default:
        return {"?",        0, 0, {"", "", "", ""},              {0, 0, 0, 0}};
    }
}

const char* getNodeTypeLabel(NodeType t) {
    return getNodeTypeInfo(t).label;
}

// ---------------------------------------------------------------------------
// NodeGraph — implementacja
// ---------------------------------------------------------------------------

int NodeGraph::addNode(NodeType type, float posX, float posY) {
    (void)posX; (void)posY;  // pozycja ustawiana przez imnodes osobno
    NodeTypeInfo info = getNodeTypeInfo(type);

    SignalNode n;
    n.id   = nextId++;
    n.type = type;

    // Kopiuj domyślne parametry
    memcpy(n.params, info.defaultParams, sizeof(n.params));

    // Przydziel ID pinów wejściowych
    for (int i = 0; i < info.numInputs; ++i) {
        n.inputAttrIds[i] = nextId++;
    }

    // Przydziel ID pinu wyjściowego (poza typem Output)
    if (type != NodeType::Output) {
        n.outputAttrId = nextId++;
    }

    nodes.push_back(n);
    return n.id;
}

void NodeGraph::removeNode(int nodeId) {
    // Usuń wszystkie linki związane z tym węzłem
    SignalNode* n = findNodeById(nodeId);
    if (!n) return;

    links.erase(std::remove_if(links.begin(), links.end(),
        [&](const SignalLink& lnk) {
            return lnk.srcAttrId == n->outputAttrId
                || lnk.dstAttrId == n->inputAttrIds[0]
                || lnk.dstAttrId == n->inputAttrIds[1];
        }), links.end());

    // Usuń węzeł
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
        [nodeId](const SignalNode& nd){ return nd.id == nodeId; }),
        nodes.end());
}

int NodeGraph::addLink(int srcAttrId, int dstAttrId) {
    // Jeden pin wejściowy może mieć max jedno połączenie
    for (const auto& lnk : links) {
        if (lnk.dstAttrId == dstAttrId) return -1;  // już zajęty
    }
    SignalLink lnk;
    lnk.id        = nextId++;
    lnk.srcAttrId = srcAttrId;
    lnk.dstAttrId = dstAttrId;
    links.push_back(lnk);
    return lnk.id;
}

void NodeGraph::removeLink(int linkId) {
    links.erase(std::remove_if(links.begin(), links.end(),
        [linkId](const SignalLink& l){ return l.id == linkId; }),
        links.end());
}

const SignalNode* NodeGraph::findOutputNode() const {
    for (const auto& n : nodes)
        if (n.type == NodeType::Output) return &n;
    return nullptr;
}

SignalNode* NodeGraph::findNodeById(int id) {
    for (auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}

const SignalNode* NodeGraph::findNodeById(int id) const {
    for (const auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}

int NodeGraph::findSourceNodeId(int dstAttrId) const {
    for (const auto& lnk : links) {
        if (lnk.dstAttrId == dstAttrId) {
            // Znajdź węzeł z tym outputAttrId
            for (const auto& n : nodes) {
                if (n.outputAttrId == lnk.srcAttrId) return n.id;
            }
        }
    }
    return -1;
}

bool NodeGraph::isValid() const {
    const SignalNode* out = findOutputNode();
    if (!out) return false;
    // Sprawdź czy OUTPUT ma podłączone wejście
    if (findSourceNodeId(out->inputAttrIds[0]) < 0) return false;
    return true;
}

void NodeGraph::initDefault(NodeType generatorType, const float* params) {
    nodes.clear();
    links.clear();
    nextId = 1;

    // Dodaj generator
    int genId  = addNode(generatorType);
    // Nadpisz parametry jeśli podane
    if (params) {
        NodeTypeInfo info = getNodeTypeInfo(generatorType);
        for (int i = 0; i < info.numParams; ++i) {
            findNodeById(genId)->params[i] = params[i];
        }
    }

    // Dodaj OUTPUT
    int outId = addNode(NodeType::Output);

    // Połącz generator → OUTPUT.input[0]
    addLink(findNodeById(genId)->outputAttrId,
            findNodeById(outId)->inputAttrIds[0]);
}

// ---------------------------------------------------------------------------
// CPU ewaluator
// ---------------------------------------------------------------------------

double evaluateSignal(const NodeGraph& graph, int nodeId, double t, double T) {
    const SignalNode* node = graph.findNodeById(nodeId);
    if (!node) return 0.0;

    const double PI2 = 6.283185307179586;

    switch (node->type) {
    case NodeType::Constant:
        return (double)node->params[0];

    case NodeType::Sine:
        return (double)node->params[1] * std::sin(PI2 * (double)node->params[0] * t);

    case NodeType::Cosine:
        return (double)node->params[1] * std::cos(PI2 * (double)node->params[0] * t);

    case NodeType::Rectangle: {
        double t0 = node->params[0], t1 = node->params[1];
        return (t >= t0 && t < t1) ? 1.0 : 0.0;
    }
    case NodeType::Triangle: {
        double center = node->params[0], width = node->params[1];
        double d = std::fabs(t - center);
        return (d < width * 0.5) ? (1.0 - 2.0 * d / width) : 0.0;
    }
    case NodeType::Sawtooth: {
        double period = node->params[0];
        if (period <= 0.0) return 0.0;
        double v = t / period;
        return v - std::floor(v);
    }
    case NodeType::Gaussian: {
        double mu = node->params[0], sigma = node->params[1];
        if (sigma <= 0.0) return 0.0;
        double d = (t - mu) / sigma;
        return std::exp(-0.5 * d * d);
    }
    case NodeType::ExpDecay: {
        double lam = node->params[0], amp = node->params[1];
        return (t >= 0.0) ? amp * std::exp(-lam * t) : 0.0;
    }
    case NodeType::Heaviside:
        return (t >= (double)node->params[0]) ? 1.0 : 0.0;

    case NodeType::Sinc: {
        double t0 = node->params[0], scale = node->params[1];
        double arg = scale * (t - t0);
        return (std::fabs(arg) < 1e-10) ? 1.0 : std::sin(arg) / arg;
    }

    // Operatory — pobierz wartości wejść, oblicz wynik
    case NodeType::Sum: {
        int src0 = graph.findSourceNodeId(node->inputAttrIds[0]);
        int src1 = graph.findSourceNodeId(node->inputAttrIds[1]);
        double v0 = (src0 >= 0) ? evaluateSignal(graph, src0, t, T) : 0.0;
        double v1 = (src1 >= 0) ? evaluateSignal(graph, src1, t, T) : 0.0;
        return v0 + v1;
    }
    case NodeType::Product: {
        int src0 = graph.findSourceNodeId(node->inputAttrIds[0]);
        int src1 = graph.findSourceNodeId(node->inputAttrIds[1]);
        double v0 = (src0 >= 0) ? evaluateSignal(graph, src0, t, T) : 0.0;
        double v1 = (src1 >= 0) ? evaluateSignal(graph, src1, t, T) : 0.0;
        return v0 * v1;
    }
    case NodeType::Scale: {
        int src = graph.findSourceNodeId(node->inputAttrIds[0]);
        double v = (src >= 0) ? evaluateSignal(graph, src, t, T) : 0.0;
        return v * (double)node->params[0];
    }
    case NodeType::TimeShift: {
        int src = graph.findSourceNodeId(node->inputAttrIds[0]);
        double tau = node->params[0];
        return (src >= 0) ? evaluateSignal(graph, src, t - tau, T) : 0.0;
    }
    case NodeType::Reflect: {
        int src = graph.findSourceNodeId(node->inputAttrIds[0]);
        return (src >= 0) ? evaluateSignal(graph, src, T - t, T) : 0.0;
    }
    case NodeType::Output: {
        int src = graph.findSourceNodeId(node->inputAttrIds[0]);
        return (src >= 0) ? evaluateSignal(graph, src, t, T) : 0.0;
    }
    default:
        return 0.0;
    }
}

void computeSignalCpu(const NodeGraph& graph, double* out, int N, double dt) {
    const SignalNode* outNode = graph.findOutputNode();
    if (!outNode) { for (int i=0;i<N;i++) out[i]=0.0; return; }
    int srcId = graph.findSourceNodeId(outNode->inputAttrIds[0]);
    for (int i = 0; i < N; ++i) {
        double t = (double)i * dt;
        out[i] = (srcId >= 0) ? evaluateSignal(graph, srcId, t) : 0.0;
    }
}
```

---

## 6. Krok 4 — Generator kodu CUDA: `src/code_gen.h` i `code_gen.cpp`

### 6.1 `src/code_gen.h`

```cpp
// code_gen.h
#pragma once
#include "signal_graph.h"
#include <string>

// Wygeneruj wyrażenie CUDA dla węzła nodeId, używając timeVar jako zmiennej t.
// Wywołanie rekurencyjne — przebiega graf od liści do korzenia.
std::string generateExpr(const NodeGraph& graph, int nodeId, 
                          const std::string& timeVar = "t");

// Wygeneruj kompletny __global__ kernel do generacji sygnału.
// funcName: "generateSignalA" lub "generateSignalB"
// Zwraca pusty string jeśli graf jest nieprawidłowy.
std::string buildSignalKernel(const NodeGraph& graph, 
                               const std::string& funcName);

// Wygeneruj CAŁY string do kompilacji NVRTC:
//   generateSignalA (z graphA)
//   generateSignalB (z graphB, lub hardcoded rectangle jeśli graphB.nodes puste)
// Nie zawiera kernela convolution — jest w osobnym module.
std::string buildSignalModule(const NodeGraph& graphA, const NodeGraph& graphB);

// Hardcoded Signal B (rectangle) — używany w Fazie 4 gdy graphB nie jest gotowy
std::string getHardcodedSignalBKernel();
```

### 6.2 `src/code_gen.cpp`

```cpp
// code_gen.cpp
#include "code_gen.hpp"
#include <sstream>
#include <cstdio>

// Formatuj double z pełną precyzją (17 cyfr znaczących)
static std::string D(double v) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.17g", v);
    // Upewnij się że jest to literał double (dodaj '.0' jeśli brak kropki)
    std::string s(buf);
    bool hasDot = (s.find('.') != std::string::npos);
    bool hasE   = (s.find('e') != std::string::npos || s.find('E') != std::string::npos);
    if (!hasDot && !hasE) s += ".0";
    return s;
}

// ---------------------------------------------------------------------------
std::string generateExpr(const NodeGraph& graph, int nodeId, 
                          const std::string& timeVar)
{
    const SignalNode* node = graph.findNodeById(nodeId);
    if (!node) return "0.0";

    const double PI2 = 6.283185307179586;

    switch (node->type) {
    case NodeType::Constant:
        return D(node->params[0]);

    case NodeType::Sine:
        return "((" + D(node->params[1]) + ") * sin(" +
               D(PI2 * node->params[0]) + " * " + timeVar + "))";

    case NodeType::Cosine:
        return "((" + D(node->params[1]) + ") * cos(" +
               D(PI2 * node->params[0]) + " * " + timeVar + "))";

    case NodeType::Rectangle: {
        double t0 = node->params[0], t1 = node->params[1];
        return "(" + timeVar + " >= " + D(t0) + " && " + timeVar + " < " + D(t1) +
               " ? 1.0 : 0.0)";
    }
    case NodeType::Triangle: {
        double c = node->params[0], w = node->params[1];
        std::string half = D(w * 0.5);
        std::string iw2  = D(2.0 / w);
        return "(fabs(" + timeVar + " - " + D(c) + ") < " + half +
               " ? (1.0 - " + iw2 + " * fabs(" + timeVar + " - " + D(c) + ")) : 0.0)";
    }
    case NodeType::Sawtooth: {
        double per = node->params[0];
        if (per <= 0.0) return "0.0";
        std::string v = "(" + timeVar + " / " + D(per) + ")";
        return "(" + v + " - floor(" + v + "))";
    }
    case NodeType::Gaussian: {
        double mu = node->params[0], sigma = node->params[1];
        if (sigma <= 0.0) return "0.0";
        std::string d = "((" + timeVar + " - " + D(mu) + ") / " + D(sigma) + ")";
        return "(exp(-0.5 * " + d + " * " + d + "))";
    }
    case NodeType::ExpDecay: {
        double lam = node->params[0], amp = node->params[1];
        return "(" + timeVar + " >= 0.0 ? " + D(amp) + " * exp(-" +
               D(lam) + " * " + timeVar + ") : 0.0)";
    }
    case NodeType::Heaviside:
        return "(" + timeVar + " >= " + D(node->params[0]) + " ? 1.0 : 0.0)";

    case NodeType::Sinc: {
        double t0 = node->params[0], scale = node->params[1];
        std::string arg = "(" + D(scale) + " * (" + timeVar + " - " + D(t0) + "))";
        // Warunek na osobno dla czytelności generowanego kodu
        return "(fabs(" + arg + ") < 1e-10 ? 1.0 : (sin(" + arg + ") / " + arg + "))";
    }

    // Operatory
    case NodeType::Sum: {
        int src0 = graph.findSourceNodeId(node->inputAttrIds[0]);
        int src1 = graph.findSourceNodeId(node->inputAttrIds[1]);
        std::string e0 = (src0>=0) ? generateExpr(graph, src0, timeVar) : "0.0";
        std::string e1 = (src1>=0) ? generateExpr(graph, src1, timeVar) : "0.0";
        return "(" + e0 + " + " + e1 + ")";
    }
    case NodeType::Product: {
        int src0 = graph.findSourceNodeId(node->inputAttrIds[0]);
        int src1 = graph.findSourceNodeId(node->inputAttrIds[1]);
        std::string e0 = (src0>=0) ? generateExpr(graph, src0, timeVar) : "0.0";
        std::string e1 = (src1>=0) ? generateExpr(graph, src1, timeVar) : "0.0";
        return "(" + e0 + " * " + e1 + ")";
    }
    case NodeType::Scale: {
        int src = graph.findSourceNodeId(node->inputAttrIds[0]);
        std::string e = (src>=0) ? generateExpr(graph, src, timeVar) : "0.0";
        return "((" + D(node->params[0]) + ") * " + e + ")";
    }
    case NodeType::TimeShift: {
        int src = graph.findSourceNodeId(node->inputAttrIds[0]);
        // Kluczowe: przekazujemy NOWĄ zmienną czasu jako wyrażenie
        // Dla prostoty używamy zagnieżdżonego wyrażenia (t - tau)
        std::string shifted = "(" + timeVar + " - " + D(node->params[0]) + ")";
        return (src>=0) ? generateExpr(graph, src, shifted) : "0.0";
    }
    case NodeType::Reflect: {
        int src = graph.findSourceNodeId(node->inputAttrIds[0]);
        // T = 1.0 — całkowity czas trwania sygnału
        std::string reflected = "(1.0 - " + timeVar + ")";
        return (src>=0) ? generateExpr(graph, src, reflected) : "0.0";
    }
    case NodeType::Output: {
        int src = graph.findSourceNodeId(node->inputAttrIds[0]);
        return (src>=0) ? generateExpr(graph, src, timeVar) : "0.0";
    }
    default:
        return "0.0";
    }
}

// ---------------------------------------------------------------------------
std::string buildSignalKernel(const NodeGraph& graph, const std::string& funcName)
{
    if (!graph.isValid()) {
        // Zwróć kernel zwracający 0 — nie crashuje, tylko daje pusty sygnał
        return
            "extern \"C\" __global__ void " + funcName +
            "(double* out, int N, double dt) {\n"
            "    int i = blockIdx.x * blockDim.x + threadIdx.x;\n"
            "    if (i >= N) return;\n"
            "    out[i] = 0.0; // Graf nieprawidłowy\n"
            "}\n";
    }

    const SignalNode* outNode = graph.findOutputNode();
    int srcId = graph.findSourceNodeId(outNode->inputAttrIds[0]);
    std::string expr = (srcId >= 0) ? generateExpr(graph, srcId, "t") : "0.0";

    std::string code;
    code += "extern \"C\" __global__ void " + funcName + 
            "(double* out, int N, double dt) {\n";
    code += "    int i = blockIdx.x * blockDim.x + threadIdx.x;\n";
    code += "    if (i >= N) return;\n";
    code += "    double t = (double)i * dt;\n";
    code += "    out[i] = " + expr + ";\n";
    code += "}\n";
    return code;
}

// ---------------------------------------------------------------------------
std::string getHardcodedSignalBKernel() {
    // Hardcoded rectangle (Phase 1 Signal B) — używane w Fazie 4
    // gdy graf Signal B nie istnieje lub jest nieprawidłowy.
    return
        "extern \"C\" __global__ void generateSignalB"
        "(double* out, int N, double dt) {\n"
        "    int i = blockIdx.x * blockDim.x + threadIdx.x;\n"
        "    if (i >= N) return;\n"
        "    double t = (double)i * dt;\n"
        "    out[i] = (t >= 0.60 && t < 0.80) ? 1.0 : 0.0;\n"
        "}\n";
}

// ---------------------------------------------------------------------------
std::string buildSignalModule(const NodeGraph& graphA, const NodeGraph& graphB) {
    std::string code;
    code += "// AUTO-GENERATED by code_gen.cpp — nie edytuj ręcznie\n\n";

    // Sygnał A — zawsze z grafu
    code += buildSignalKernel(graphA, "generateSignalA");
    code += "\n";

    // Sygnał B — z grafu (Faza 5) lub hardcoded (Faza 4)
    if (graphB.isValid()) {
        code += buildSignalKernel(graphB, "generateSignalB");
    } else {
        code += getHardcodedSignalBKernel();
    }

    return code;
}
```

---

## 7. Krok 5 — Nowy pipeline GPU: zmiany w `cuda_impl.cu`

W Fazach 1–3 `runConvolution` przyjmowało `double* A` i `double* B` z CPU
(dane H2D). W Fazie 4 sygnały są generowane na GPU — nie przekazujemy CPU data.

### 7.1 Dodaj nowy struct `PipelineResult` do `cuda_interface.h`

```cpp
// cuda_interface.h — DODAJ (nie usuwaj ConvolutionResult — jest nadal używany)

struct PipelineResult {
    bool        success         = false;
    std::string errorMessage;

    // Timing fazy generate (ms)
    float       genAMs          = 0.0f;
    float       genBMs          = 0.0f;
    float       convMs          = 0.0f;
    // Readback
    float       readbackMs      = 0.0f;
    // Walidacja
    float       maxAbsError     = 0.0f;
};
```

### 7.2 Zmień `GpuTask` w `gpu_worker.h` (Faza 4 wersja)

```cpp
// gpu_worker.h — Faza 4: podmień istniejący GpuTask

struct GpuTask {
    // Faza 4: żadnych wektorów sygnałów — generowane na GPU
    int          N;
    double       dt;            // dt = 1.0 / (N - 1)

    KernelHandle genAFunc;      // z signalKernelMgr.getFunction("generateSignalA")
    KernelHandle genBFunc;      // z signalKernelMgr.getFunction("generateSignalB")
    KernelHandle convFunc;      // z convKernelMgr.getFunction("convolution")

    // CPU referencja — do walidacji
    // Przechowujemy kopię grafów (żeby liczyć CPU reference w wątku)
    NodeGraph    graphA;
    NodeGraph    graphB;

    std::promise<AsyncConvResult> promise;

    GpuTask() = default;
    GpuTask(GpuTask&&) = default;
    GpuTask& operator=(GpuTask&&) = default;
    GpuTask(const GpuTask&) = delete;
    GpuTask& operator=(const GpuTask&) = delete;
};
```

### 7.3 Zaktualizuj `AsyncConvResult` w `cuda_interface.h`

```cpp
// cuda_interface.h — zmodyfikowany AsyncConvResult

struct AsyncConvResult {
    PipelineResult    info;        // timing, maxAbsError, success
    std::vector<double> signalA;   // odczyt GPU Signal A (dla ImPlot)
    std::vector<double> signalB;   // odczyt GPU Signal B (dla ImPlot)
    std::vector<double> convOut;   // wynik splotu GPU (dla ImPlot)
};
```

### 7.4 Nowa funkcja `runPipeline` w `cuda_impl.cu`

Dodaj do `cuda_impl.cu`. Deklaracja w `cuda_interface.h`:

```cpp
// cuda_interface.h — dodaj deklarację:
void runPipeline(
    KernelHandle    genAFunc,
    KernelHandle    genBFunc,
    KernelHandle    convFunc,
    int             N,
    double          dt,
    double*         signalA_out,  // CPU buffer (N elementy)
    double*         signalB_out,  // CPU buffer (N elementy)
    double*         convOut,      // CPU buffer (N elementy)
    PipelineResult& result
);
```

Implementacja w `cuda_impl.cu`:

```cpp
// cuda_impl.cu — dodaj runPipeline (zachowaj stary runConvolution)
void runPipeline(
    KernelHandle    genAFunc,
    KernelHandle    genBFunc,
    KernelHandle    convFunc,
    int             N,
    double          dt,
    double*         signalA_out,
    double*         signalB_out,
    double*         convOut,
    PipelineResult& result)
{
    result.success = false;
    result.errorMessage.clear();

    if (!genAFunc || !genBFunc || !convFunc) {
        result.errorMessage = "Jeden lub więcej kerneli nie jest załadowany.";
        return;
    }

    CUfunction fGenA = reinterpret_cast<CUfunction>(genAFunc);
    CUfunction fGenB = reinterpret_cast<CUfunction>(genBFunc);
    CUfunction fConv = reinterpret_cast<CUfunction>(convFunc);

    const size_t bytes = (size_t)N * sizeof(double);

    double* d_A = nullptr;
    double* d_B = nullptr;
    double* d_C = nullptr;

    cudaEvent_t evA0, evA1, evB0, evB1, evC0, evC1, evR0, evR1;
    cudaEventCreate(&evA0); cudaEventCreate(&evA1);
    cudaEventCreate(&evB0); cudaEventCreate(&evB1);
    cudaEventCreate(&evC0); cudaEventCreate(&evC1);
    cudaEventCreate(&evR0); cudaEventCreate(&evR1);

    CUDA_CHECK(cudaMalloc(&d_A, bytes));
    CUDA_CHECK(cudaMalloc(&d_B, bytes));
    CUDA_CHECK(cudaMalloc(&d_C, bytes));

    unsigned int grid  = ((unsigned int)N + 255u) / 256u;
    unsigned int block = 256u;

    // --- Generate Signal A ---
    {
        void* args[] = {&d_A, &N, &dt};
        cudaEventRecord(evA0);
        CU_CHECK_LAUNCH(
            cuLaunchKernel(fGenA, grid,1,1, block,1,1, 0, 0, args, nullptr),
            result);
        cudaEventRecord(evA1);
        cudaEventSynchronize(evA1);
        cudaEventElapsedTime(&result.genAMs, evA0, evA1);
    }

    // --- Generate Signal B ---
    {
        void* args[] = {&d_B, &N, &dt};
        cudaEventRecord(evB0);
        CU_CHECK_LAUNCH(
            cuLaunchKernel(fGenB, grid,1,1, block,1,1, 0, 0, args, nullptr),
            result);
        cudaEventRecord(evB1);
        cudaEventSynchronize(evB1);
        cudaEventElapsedTime(&result.genBMs, evB0, evB1);
    }

    // --- Convolution ---
    {
        void* args[] = {&d_A, &d_B, &d_C, &N};
        cudaEventRecord(evC0);
        CU_CHECK_LAUNCH(
            cuLaunchKernel(fConv, grid,1,1, block,1,1, 0, 0, args, nullptr),
            result);
        cudaEventRecord(evC1);
        cudaEventSynchronize(evC1);
        cudaEventElapsedTime(&result.convMs, evC0, evC1);
    }

    // --- Readback (GPU → CPU) ---
    cudaEventRecord(evR0);
    CUDA_CHECK(cudaMemcpy(signalA_out, d_A, bytes, cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(signalB_out, d_B, bytes, cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(convOut,     d_C, bytes, cudaMemcpyDeviceToHost));
    cudaEventRecord(evR1);
    cudaEventSynchronize(evR1);
    cudaEventElapsedTime(&result.readbackMs, evR0, evR1);

    result.success = true;
    goto pipeline_cleanup;

cuda_error:
    ;

pipeline_cleanup:
    cudaFree(d_A); cudaFree(d_B); cudaFree(d_C);
    cudaEventDestroy(evA0); cudaEventDestroy(evA1);
    cudaEventDestroy(evB0); cudaEventDestroy(evB1);
    cudaEventDestroy(evC0); cudaEventDestroy(evC1);
    cudaEventDestroy(evR0); cudaEventDestroy(evR1);
}
```

---

## 8. Krok 6 — Zaktualizuj `gpu_worker.cpp`

Zmodyfikuj `workerLoop` by wywołać `runPipeline` zamiast `runConvolution`:

```cpp
// gpu_worker.cpp — zaktualizowany workerLoop (sekcja wykonania zadania)

// W workerLoop, po pobraniu task z kolejki:
AsyncConvResult asyncResult;
asyncResult.signalA.resize(task.N, 0.0);
asyncResult.signalB.resize(task.N, 0.0);
asyncResult.convOut.resize(task.N, 0.0);

// Faza 4: uruchom cały pipeline GPU
runPipeline(
    task.genAFunc,
    task.genBFunc,
    task.convFunc,
    task.N,
    task.dt,
    asyncResult.signalA.data(),
    asyncResult.signalB.data(),
    asyncResult.convOut.data(),
    asyncResult.info        // PipelineResult
);

// CPU reference + walidacja (w wątku GPU, nie blokuje UI)
if (asyncResult.info.success) {
    std::vector<double> cpuA(task.N), cpuB(task.N), cpuC(task.N);

    // Faza 4: ewaluuj grafy CPU
    computeSignalCpu(task.graphA, cpuA.data(), task.N, task.dt);

    // Faza 4: dla Sygnału B — jeśli graphB jest prawidłowy, użyj go;
    // inaczej użyj hardcoded rectangle (matching getHardcodedSignalBKernel)
    if (task.graphB.isValid()) {
        computeSignalCpu(task.graphB, cpuB.data(), task.N, task.dt);
    } else {
        // Hardcoded rectangle: t ∈ [0.60, 0.80)
        double dt = task.dt;
        for (int i = 0; i < task.N; ++i) {
            double t = (double)i * dt;
            cpuB[i] = (t >= 0.60 && t < 0.80) ? 1.0 : 0.0;
        }
    }

    // CPU splot (O(N²))
    for (int n = 0; n < task.N; ++n) {
        double sum = 0.0;
        for (int k = 0; k < task.N; ++k) {
            int idx = n - k;
            if (idx >= 0 && idx < task.N) sum += cpuA[k] * cpuB[idx];
        }
        cpuC[n] = sum;
    }

    // maxAbsError GPU vs CPU
    double maxErr = 0.0;
    for (int i = 0; i < task.N; ++i) {
        double err = std::fabs(asyncResult.convOut[i] - cpuC[i]);
        if (err > maxErr) maxErr = err;
    }
    asyncResult.info.maxAbsError = (float)maxErr;
}

m_busy.store(false);
task.promise.set_value(std::move(asyncResult));
```

> **Uwaga wydajnościowa**: Dla N=65536 CPU reference (O(N²)) zajmie ~40 sekund.
> W Fazie 5 z dużym N CPU reference należy pominąć lub ograniczyć.
> Dodaj flagę `bool skipCpuReference = (N > 8192);` do GpuTask.

---

## 9. Krok 7 — Modyfikacja `app.h`

```cpp
// app.h — Faza 4: dodaj nowe pola

#include "signal_graph.h"  // NOWE
#include "code_gen.h"      // NOWE
// ... istniejące includes ...

struct AppState {
    // --- istniejące pola Fazy 1–3 (zachowaj wszystkie) ---
    bool               cudaAvail   = false;
    CudaDeviceInfo     deviceInfo  = {};
    bool               computing   = false;
    bool               dirty       = false;

    // Faza 2: manager kernela splotu (kernels/convolution.cu)
    KernelManager      convKernelMgr;  // POPRZEDNIO: kernelMgr
    std::string        convKernelSource;
    std::string        convKernelFilePath;
    NvrtcCompileResult lastConvCompile;

    // Faza 3
    GpuWorkerThread               gpuWorker;
    std::future<AsyncConvResult>  gpuFuture;

    // Faza 4: node grafe + generacja kodu
    NodeGraph          graphA;          // NOWE: graf sygnału A
    NodeGraph          graphB;          // NOWE: graf sygnału B (pusty w Fazie 4)
    KernelManager      signalKernelMgr; // NOWE: manager kerneli sygnałów (genA+genB)
    std::string        generatedCode;   // NOWE: ostatnio wygenerowany string CUDA
    NvrtcCompileResult lastSignalCompile; // NOWE
    bool               codeDirty = true;  // NOWE: czy kod wymaga regeneracji

    // Wyniki (readback z GPU)
    std::vector<double> signalA;
    std::vector<double> signalB;
    std::vector<double> convOutput;

    // Aktualne N i dt
    int    N  = 4096;
    double dt = 1.0 / (N - 1);
};
```

---

## 10. Krok 8 — Modyfikacja `app.cpp`

### 10.1 Zmień `appInit` — inicjalizacja grafów i kompilacja startowa

```cpp
// app.cpp — zmodyfikowany appInit

void appInit(AppState& state) {
    // --- Istniejący kod Fazy 1–3 ---
    // queryCudaDevice(state.deviceInfo);
    // state.cudaAvail = ...;
    // initCudaDriver();

    // Faza 2: załaduj i skompiluj kernel splotu
    state.convKernelFilePath = "kernels/convolution.cu";
    state.convKernelSource   = loadKernelSourceFromFile(state.convKernelFilePath);
    if (!state.convKernelSource.empty() && state.cudaAvail) {
        state.lastConvCompile = state.convKernelMgr.compile(
            state.convKernelSource,
            state.deviceInfo.computeCapabilityMajor,
            state.deviceInfo.computeCapabilityMinor,
            {"convolution"}  // Faza 4: nowa sygnatura compile()
        );
    }

    // --- NOWE Faza 4: inicjalizacja grafów sygnałów ---
    // Signal A: Gaussian(mu=0.30, sigma=0.05) — jak w Fazie 1
    {
        float params[] = {0.30f, 0.05f};
        state.graphA.initDefault(NodeType::Gaussian, params);
    }
    // Signal B: pusty w Fazie 4 → buildSignalModule użyje hardcoded rectangle
    // state.graphB pozostaje domyślnie pusty

    // Zainicjuj bufory
    state.signalA.resize(state.N, 0.0);
    state.signalB.resize(state.N, 0.0);
    state.convOutput.resize(state.N, 0.0);

    // Skompiluj moduł sygnałów na starcie
    if (state.cudaAvail) {
        state.generatedCode    = buildSignalModule(state.graphA, state.graphB);
        state.lastSignalCompile = state.signalKernelMgr.compile(
            state.generatedCode,
            state.deviceInfo.computeCapabilityMajor,
            state.deviceInfo.computeCapabilityMinor,
            {"generateSignalA", "generateSignalB"}
        );
        state.codeDirty = false;
    }

    state.dirty = true;  // zlec pierwsze obliczenie
}
```

### 10.2 `appPollAndSubmit` — zaktualizuj GpuTask

```cpp
// app.cpp — zaktualizowane appPollAndSubmit

void appPollAndSubmit(AppState& state) {
    // --- Sprawdź future (bez zmian) ---
    if (state.computing && state.gpuFuture.valid()) {
        using namespace std::chrono;
        if (state.gpuFuture.wait_for(milliseconds(0)) == std::future_status::ready) {
            AsyncConvResult res = state.gpuFuture.get();
            // Przenies dane do stanu
            state.signalA   = std::move(res.signalA);
            state.signalB   = std::move(res.signalB);
            state.convOutput = std::move(res.convOut);
            // Zachowaj info
            // (możesz przechować PipelineResult w AppState jeśli potrzebujesz)
            state.computing = false;
        }
    }

    // --- Zlec nowe zadanie ---
    bool ready =
        state.dirty          &&
        !state.computing     &&
        state.cudaAvail      &&
        state.convKernelMgr.isReady()   &&
        state.signalKernelMgr.isReady();

    if (ready) {
        // Wywołaj submit przez GpuWorkerThread
        // (submit musi teraz wypełnić nowy GpuTask)
        submitPipelineTask(state);
        state.dirty = false;
    }
}

// Pomocnicza — tworzy i wysyła GpuTask do workera
void submitPipelineTask(AppState& state) {
    // Zbuduj task — wszystkie dane kopiowane tutaj
    GpuTask task;
    task.N       = state.N;
    task.dt      = state.dt;
    task.genAFunc = state.signalKernelMgr.getFunction("generateSignalA");
    task.genBFunc = state.signalKernelMgr.getFunction("generateSignalB");
    task.convFunc = state.convKernelMgr.getFunction("convolution");
    task.graphA   = state.graphA;   // KOPIA grafu (do CPU reference w wątku)
    task.graphB   = state.graphB;   // KOPIA

    state.gpuFuture = state.gpuWorker.submitTask(std::move(task));
    state.computing  = state.gpuFuture.valid();
}
```

Zaktualizuj `GpuWorkerThread::submit` — zmień sygnaturę na `submitTask(GpuTask)`:

```cpp
// gpu_worker.h — nowa sygnatura
std::future<AsyncConvResult> submitTask(GpuTask task);

// gpu_worker.cpp:
std::future<AsyncConvResult> GpuWorkerThread::submitTask(GpuTask task) {
    if (m_busy.load()) return {};
    std::future<AsyncConvResult> fut = task.promise.get_future();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_busy.store(true);
        m_taskQueue.push(std::move(task));
    }
    m_cv.notify_one();
    return fut;
}
```

---

## 11. Krok 9 — Node Editor UI: `gui.cpp`

### 11.1 Inicjalizacja imnodes (w `main.cpp` lub `appInit`)

```cpp
// Dodaj do sekcji inicjalizacji (po ImGui::CreateContext()):
ImNodes::CreateContext();
ImNodes::StyleColorsDark();  // opcjonalnie

// W sekcji sprzątania (przed ImGui::DestroyContext()):
ImNodes::DestroyContext();
```

### 11.2 Nowe okno node editora w `guiRender`

Dodaj nowe okno ImGui w `guiRender`:

```cpp
// gui.cpp — dodaj nowe okno node editora dla Sygnału A

void renderNodeEditorWindow(AppState& state) {
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Node Editor — Sygnał A")) { ImGui::End(); return; }

    // --- Pasek narzędzi ---
    bool canRegenerate = !state.computing;
    if (!canRegenerate) ImGui::BeginDisabled();
    if (ImGui::Button("Generuj kod + Kompiluj NVRTC")) {
        state.generatedCode     = buildSignalModule(state.graphA, state.graphB);
        state.lastSignalCompile = state.signalKernelMgr.compile(
            state.generatedCode,
            state.deviceInfo.computeCapabilityMajor,
            state.deviceInfo.computeCapabilityMinor,
            {"generateSignalA", "generateSignalB"}
        );
        state.codeDirty = false;
        if (state.lastSignalCompile.success) state.dirty = true;
    }
    if (!canRegenerate) ImGui::EndDisabled();

    ImGui::SameLine();
    if (state.codeDirty) {
        ImGui::TextColored({1.0f, 0.8f, 0.2f, 1.0f}, "⚠ Kod nieaktualny");
    } else if (state.lastSignalCompile.success) {
        ImGui::TextColored({0.3f, 1.0f, 0.3f, 1.0f}, 
            "OK (%.0f ms)", state.lastSignalCompile.compileTimeMs);
    } else {
        ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "BŁĄD NVRTC");
    }

    // --- Log NVRTC jeśli błąd ---
    if (!state.lastSignalCompile.success && !state.lastSignalCompile.log.empty()) {
        ImGui::BeginChild("##sig_log", ImVec2(0, 60), true);
        ImGui::TextWrapped("%s", state.lastSignalCompile.log.c_str());
        ImGui::EndChild();
    }

    ImGui::Separator();

    // --- Node editor ---
    renderNodeGraph(state.graphA, state.codeDirty);

    ImGui::End();
}
```

### 11.3 Funkcja `renderNodeGraph`

```cpp
// gui.cpp — renderowanie node grafu

void renderNodeGraph(NodeGraph& graph, bool& codeDirtyFlag) {
    // Kontener dwupanelowy — lewa: graf, prawa: przycisk "Dodaj węzeł"
    // (lub użyj full-width i osobnego menu kontekstowego)

    ImNodes::BeginNodeEditor();

    // --- Renderuj węzły ---
    for (auto& node : graph.nodes) {
        renderSingleNode(node, graph, codeDirtyFlag);
    }

    // --- Renderuj połączenia ---
    for (const auto& lnk : graph.links) {
        ImNodes::Link(lnk.id, lnk.srcAttrId, lnk.dstAttrId);
    }

    ImNodes::EndNodeEditor();

    // --- Obsługa nowych połączeń (użytkownik przeciągnął linię) ---
    int srcAttr, dstAttr;
    if (ImNodes::IsLinkCreated(&srcAttr, &dstAttr)) {
        // Upewnij się że połączenie idzie output→input (nie input→input)
        // imnodes może zwrócić je w dowolnej kolejności
        int created = graph.addLink(srcAttr, dstAttr);
        if (created < 0) {
            // Próba z odwrócona kolejnością
            graph.addLink(dstAttr, srcAttr);
        }
        codeDirtyFlag = true;
    }

    // --- Obsługa usuwania połączeń ---
    int destroyedLink;
    if (ImNodes::IsLinkDestroyed(&destroyedLink)) {
        graph.removeLink(destroyedLink);
        codeDirtyFlag = true;
    }

    // --- Usuwanie węzłów klawiszem Delete ---
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        // Nie pozwól usunąć węzła OUTPUT
        int numSelected = ImNodes::NumSelectedNodes();
        if (numSelected > 0) {
            std::vector<int> selectedIds(numSelected);
            ImNodes::GetSelectedNodes(selectedIds.data());
            for (int selId : selectedIds) {
                const SignalNode* n = graph.findNodeById(selId);
                if (n && n->type != NodeType::Output) {
                    graph.removeNode(selId);
                    codeDirtyFlag = true;
                }
            }
        }
    }

    // --- Menu kontekstowe (prawy klik w edytorze) ---
    if (ImNodes::IsEditorHovered() && 
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup("##add_node_popup");
    }
    if (ImGui::BeginPopup("##add_node_popup")) {
        ImGui::TextDisabled("Dodaj węzeł:");
        ImGui::Separator();
        
        // Generatory
        if (ImGui::MenuItem("Constant"))  { addNodeToGraph(graph, NodeType::Constant,  codeDirtyFlag); }
        if (ImGui::MenuItem("Sine"))      { addNodeToGraph(graph, NodeType::Sine,       codeDirtyFlag); }
        if (ImGui::MenuItem("Cosine"))    { addNodeToGraph(graph, NodeType::Cosine,     codeDirtyFlag); }
        if (ImGui::MenuItem("Rectangle")) { addNodeToGraph(graph, NodeType::Rectangle,  codeDirtyFlag); }
        if (ImGui::MenuItem("Triangle"))  { addNodeToGraph(graph, NodeType::Triangle,   codeDirtyFlag); }
        if (ImGui::MenuItem("Sawtooth"))  { addNodeToGraph(graph, NodeType::Sawtooth,   codeDirtyFlag); }
        if (ImGui::MenuItem("Gaussian"))  { addNodeToGraph(graph, NodeType::Gaussian,   codeDirtyFlag); }
        if (ImGui::MenuItem("ExpDecay"))  { addNodeToGraph(graph, NodeType::ExpDecay,   codeDirtyFlag); }
        if (ImGui::MenuItem("Heaviside")) { addNodeToGraph(graph, NodeType::Heaviside,  codeDirtyFlag); }
        if (ImGui::MenuItem("Sinc"))      { addNodeToGraph(graph, NodeType::Sinc,       codeDirtyFlag); }
        ImGui::Separator();
        // Operatory
        if (ImGui::MenuItem("Sum"))       { addNodeToGraph(graph, NodeType::Sum,        codeDirtyFlag); }
        if (ImGui::MenuItem("Product"))   { addNodeToGraph(graph, NodeType::Product,    codeDirtyFlag); }
        if (ImGui::MenuItem("Scale"))     { addNodeToGraph(graph, NodeType::Scale,      codeDirtyFlag); }
        if (ImGui::MenuItem("TimeShift")) { addNodeToGraph(graph, NodeType::TimeShift,  codeDirtyFlag); }
        if (ImGui::MenuItem("Reflect"))   { addNodeToGraph(graph, NodeType::Reflect,    codeDirtyFlag); }

        ImGui::EndPopup();
    }
}

// Dodaj węzeł i oznacz że edytor się zmienił
static void addNodeToGraph(NodeGraph& graph, NodeType type, bool& dirty) {
    graph.addNode(type);
    dirty = true;
}
```

### 11.4 Renderowanie pojedynczego węzła `renderSingleNode`

```cpp
static void renderSingleNode(SignalNode& node, NodeGraph& graph, bool& dirty) {
    NodeTypeInfo info = getNodeTypeInfo(node.type);

    // Kolor węzła zależy od typu
    if (node.type == NodeType::Output) {
        ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(140, 40, 40, 200));
    } else if (info.numInputs == 0) {
        ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(40, 80, 140, 200));
    } else {
        ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(40, 120, 60, 200));
    }

    ImNodes::BeginNode(node.id);

    // Tytuł węzła
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(info.label);
    ImNodes::EndNodeTitleBar();

    // Piny wejściowe
    for (int i = 0; i < info.numInputs; ++i) {
        ImNodes::BeginInputAttribute(node.inputAttrIds[i]);
        ImGui::Text("in %d", i + 1);
        ImNodes::EndInputAttribute();
    }

    // Parametry (suwaki)
    for (int i = 0; i < info.numParams; ++i) {
        ImGui::PushItemWidth(120.0f);
        char label[32];
        snprintf(label, sizeof(label), "%s##%d_%d", info.paramNames[i], node.id, i);
        if (ImGui::DragFloat(label, &node.params[i], 0.005f)) {
            dirty = true;
        }
        ImGui::PopItemWidth();
    }

    // Pin wyjściowy (poza typem Output)
    if (node.type != NodeType::Output) {
        ImNodes::BeginOutputAttribute(node.outputAttrId);
        ImGui::Text("out");
        ImNodes::EndOutputAttribute();
    }

    ImNodes::EndNode();
    ImNodes::PopColorStyle();
}
```

### 11.5 Panel z generowanym kodem

Dodaj w istniejącym oknie głównym lub w osobnym oknie:

```cpp
// gui.cpp — panel "Wygenerowany kod"

void renderGeneratedCodeWindow(AppState& state) {
    ImGui::SetNextWindowSize(ImVec2(600, 300), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Wygenerowany kod CUDA")) { ImGui::End(); return; }

    if (state.generatedCode.empty()) {
        ImGui::TextDisabled("(brak wygenerowanego kodu)");
    } else {
        // InputTextMultiline — tylko do odczytu
        ImGui::InputTextMultiline(
            "##gencode",
            const_cast<char*>(state.generatedCode.c_str()),
            state.generatedCode.size() + 1,
            ImVec2(-1, -1),
            ImGuiInputTextFlags_ReadOnly
        );
    }
    ImGui::End();
}
```

### 11.6 Zmiana w `guiRender` — dodaj wywołania nowych okien

```cpp
// gui.cpp — w guiRender dodaj:
void guiRender(AppState& state) {
    appPollAndSubmit(state);          // bez zmian

    renderMainWindow(state);          // bez zmian — sygnały + splot
    renderNodeEditorWindow(state);    // NOWE — node editor A
    renderGeneratedCodeWindow(state); // NOWE — podgląd kodu
}
```

---

