## 12. Faza 5 — Drugi graf dla Sygnału B + skalowalność N

### 12.1 Inicjalizacja drugiego grafu Signal B

W `appInit`, zmień:

```cpp
// Faza 5: inicjalizuj graphB z Rectangle (matching Phase 1)
{
    float params[] = {0.60f, 0.80f};
    state.graphB.initDefault(NodeType::Rectangle, params);
}
```

W `buildSignalModule(graphA, graphB)` — jeśli `graphB.isValid()`, kod Signal B
jest automatycznie generowany z grafu. Nie potrzebujesz żadnej innej zmiany.

### 12.2 Drugie okno node editora

Prawie identyczne jak okno dla Signal A. Najprościej — wyodrębnij funkcję
`renderNodeEditorWindow(NodeGraph& graph, const std::string& title, ...)`:

```cpp
// gui.cpp — generyczna funkcja dla dowolnego grafu

void renderNodeEditorWindowForGraph(
    const std::string& title,
    NodeGraph&         graph,
    bool&              codeDirtyFlag,
    AppState&          state)
{
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str())) { ImGui::End(); return; }

    // ... identyczna zawartość jak renderNodeEditorWindow, ale dla `graph` ...
    renderNodeGraph(graph, codeDirtyFlag);

    ImGui::End();
}

// W guiRender (Faza 5):
renderNodeEditorWindowForGraph("Node Editor — Sygnał A", state.graphA, state.codeDirty, state);
renderNodeEditorWindowForGraph("Node Editor — Sygnał B", state.graphB, state.codeDirty, state);
```

> **Ważne:** oba node edytory modyfikują `state.codeDirty = true`.
> Ten sam przycisk "Generuj kod + Kompiluj NVRTC" obsługuje oba.

### 12.3 Suwak N

Dodaj do panelu Controls (lub głównego okna):

```cpp
// gui.cpp — suwak N (Faza 5)

// N jako potęga 2: exp_N ∈ [10, 16] → N ∈ [1024, 65536]
static int exp_N = 12;  // domyślnie: 4096 = 2^12

if (ImGui::SliderInt("log₂(N)", &exp_N, 10, 16)) {
    int newN = 1 << exp_N;
    if (newN != state.N) {
        state.N  = newN;
        state.dt = 1.0 / (state.N - 1);
        // Zmień rozmiary buforów
        state.signalA.resize(state.N, 0.0);
        state.signalB.resize(state.N, 0.0);
        state.convOutput.resize(state.N, 0.0);
        state.dirty = true;
    }
}
ImGui::SameLine();
ImGui::Text("N = %d", state.N);

// Ostrzeżenie dla dużego N (CPU reference wolny)
if (state.N > 8192) {
    ImGui::TextColored({1.0f, 0.8f, 0.2f, 1.0f},
        "⚠ N > 8192: CPU reference zajmie ~%.0f s",
        (double)state.N * state.N / 1e9 * 2.0);  // przybliżenie
}
```

### 12.4 Pominięcie CPU reference dla dużego N

W `GpuTask`, dodaj flagę:

```cpp
// gpu_worker.h:
struct GpuTask {
    // ...
    bool skipCpuReference = false;  // NOWE: dla dużego N
};
```

W `app.cpp` (submitPipelineTask):
```cpp
task.skipCpuReference = (state.N > 8192);
```

W `gpu_worker.cpp` (workerLoop):
```cpp
if (asyncResult.info.success && !task.skipCpuReference) {
    // ... CPU reference + walidacja ...
} else if (task.skipCpuReference) {
    asyncResult.info.maxAbsError = -1.0f;  // -1 = "nie obliczono"
}
```

W UI wyświetl:
```cpp
if (info.maxAbsError < 0.0f) {
    ImGui::Text("maxAbsError: (pominięto dla N>8192)");
} else {
    ImGui::Text("maxAbsError: %.2e %s", info.maxAbsError,
        info.maxAbsError < 1e-9 ? "✓" : "✗");
}
```

---

## 13. Kolejność inicjalizacji (Faza 4+5 — pełna)

```
STARTUP:
  1. glfwInit() + window
  2. glad + OpenGL
  3. ImGui::CreateContext()
  4. ImNodes::CreateContext()    ← NOWE
  5. ImPlot::CreateContext()
  6. AppState state;
  7. appInit(state):
       queryCudaDevice + initCudaDriver
       convKernelMgr.compile(convolution, {"convolution"})
       graphA.initDefault(Gaussian)
       graphB = empty (Faza 4) / initDefault(Rectangle) (Faza 5)
       signalKernelMgr.compile(buildSignalModule(A,B), {"genA","genB"})
       state.dirty = true
  8. Loop (main render loop)

SHUTDOWN (obowiązkowa kolejność):
  9. state.gpuWorker.shutdown()    ← PIERWSZY
 10. ImNodes::DestroyContext()     ← NOWE
 11. ImPlot::DestroyContext()
 12. ImGui::DestroyContext()
 13. glfwTerminate()
```

---

## 14. Pułapki i rozwiązania

### P1 — imnodes: węzły nachodzą na siebie na starcie

**Objaw**: Wszystkie węzły pojawiają się w pozycji (0,0).

**Rozwiązanie**: Ustaw pozycje węzłów PRZED pierwszym renderowaniem za pomocą
`ImNodes::SetNodeEditorSpacePos`. Przykład w `initDefault`:

```cpp
// Po addNode(), ustaw pozycję:
// W renderNodeGraph lub przed pierwszym renderem (np. pierwszy raz gdy okno otwarte)
static bool positionsSet = false;
if (!positionsSet) {
    for (int i = 0; i < (int)graph.nodes.size(); ++i) {
        ImNodes::SetNodeEditorSpacePos(graph.nodes[i].id,
            ImVec2(100.0f + i * 200.0f, 150.0f));
    }
    positionsSet = true;
}
```

---

### P2 — Wygenerowany kod nie kompiluje się przez NVRTC: `undefined identifier`

**Objaw**: Log NVRTC zawiera `error: identifier "floor" is not defined` lub podobny.

**Wyjaśnienie**: `floor`, `sin`, `cos`, `exp`, `fabs` są dostępne w CUDA device code
bez `#include`. Ale NVRTC może nie mieć dostępu do nich bez jawnej specyfikacji.

**Rozwiązanie**: Dodaj na początku generowanego modułu:

```cpp
// W buildSignalModule(), na samym początku string:
std::string preamble =
    "// math functions are built-in in CUDA device code\n"
    "#include <math.h>\n";  // Jeśli NVRTC ma ścieżkę do headers
```

Lub jeśli `#include` nie działa (NVRTC nie ma ścieżki do CUDA headers):

```cpp
// W buildSignalModule():
std::string preamble =
    "#define MY_PI 3.141592653589793\n"
    "__device__ double my_sin(double x) { return __builtin_sin(x); }\n";
    // etc.
```

W praktyce NVRTC ma wbudowane `sin`, `cos`, `exp`, `floor`, `fabs` bez #include.
Jeśli pojawi się błąd, dodaj preambułę z definicjami lub ścieżkę do CUDA headers:

```cpp
const char* opts[] = {
    archOpt,
    "--std=c++17",
    "--include-path=" CUDA_INCLUDE_PATH  // np. "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.x/include"
};
```

---

### P3 — Graf nieprawidłowy: `buildSignalKernel` zwraca kernel z `out[i] = 0.0`

**Objaw**: Sygnał A jest płaską linią (zerową).

**Przyczyna A**: `graph.isValid()` zwraca false. Sprawdź:
- Czy jest węzeł OUTPUT: `graph.findOutputNode() != nullptr`
- Czy OUTPUT ma podłączone wejście: `graph.findSourceNodeId(outNode->inputAttrIds[0]) >= 0`

**Przyczyna B**: Węzeł połączony do OUTPUT.input[0] przez atrybut który nie jest
`outputAttrId` węzła źródłowego — możliwy błąd przy tworzeniu linku.

**Debugowanie**: Dodaj do UI wyświetlanie stanu grafu:
```cpp
ImGui::Text("Nodes: %d  Links: %d  Valid: %s",
    (int)graph.nodes.size(),
    (int)graph.links.size(),
    graph.isValid() ? "YES" : "NO");
```

---

### P4 — `addLink` zawsze zwraca -1 (brak połączeń)

**Przyczyna**: `srcAttrId` jest `inputAttrIds[x]` zamiast `outputAttrId` (albo odwrotnie).
imnodes może zwrócić para (startAttr, endAttr) w dowolnej kolejności.

**Rozwiązanie**: Sprawdź oba warianty:

```cpp
// W GUI obsłudze IsLinkCreated:
int srcAttr, dstAttr;
if (ImNodes::IsLinkCreated(&srcAttr, &dstAttr)) {
    // Próba 1
    int id = graph.addLink(srcAttr, dstAttr);
    if (id < 0) {
        // Próba 2 — odwróć kolejność
        graph.addLink(dstAttr, srcAttr);
    }
    codeDirty = true;
}
```

Ew. sprawdź który z atrybutów jest `outputAttrId` (pinem wyjściowym):
```cpp
// Znajdź który atrybut jest "output" (należy do outputAttrId)
auto isOutputAttr = [&](int attrId) {
    for (const auto& n : graph.nodes)
        if (n.outputAttrId == attrId) return true;
    return false;
};
int realSrc = isOutputAttr(srcAttr) ? srcAttr : dstAttr;
int realDst = isOutputAttr(srcAttr) ? dstAttr : srcAttr;
graph.addLink(realSrc, realDst);
```

---

### P5 — CPU reference nie zgadza się z GPU (duży błąd)

**Objaw**: `maxAbsError > 1e-6` dla prostego grafu (Gaussian).

**Przyczyna A**: CPU evaluator i CUDA generator używają różnych formuł dla danego
typu węzła.

**Debugowanie**: Przetestuj dla Constant(1.0):
- GPU: `generateSignalA` powinno zwracać wszędzie 1.0
- CPU: `computeSignalCpu` powinno zwracać wszędzie 1.0
- Jeśli się różnią — błąd w jednej z implementacji

**Przyczyna B**: Błąd zaokrąglenia w `dt`. Upewnij się że `dt` jest obliczone
identycznie w CPU i GPU:
```cpp
// app.cpp:
state.dt = 1.0 / (double)(state.N - 1);
// gpu_worker.cpp (task.dt):
// task.dt = state.dt — przekazana wartość
// cuda kernel: double t = (double)i * dt;
// cpu eval:    double t = (double)i * dt;
// Identyczne — OK
```

---

### P6 — `ImNodes::IsLinkDestroyed` nie wywołuje się po Ctrl+Click na linku

**Wyjaśnienie**: `IsLinkDestroyed` zwraca true tylko jeśli użytkownik usuwa link
przez PRZECIĄGNIĘCIE istniejącego końca (disconnect). Aby usunąć przez Delete,
użyj `NumSelectedLinks` + `GetSelectedLinks`:

```cpp
if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
    int numLinks = ImNodes::NumSelectedLinks();
    if (numLinks > 0) {
        std::vector<int> selectedLinks(numLinks);
        ImNodes::GetSelectedLinks(selectedLinks.data());
        for (int lId : selectedLinks) {
            graph.removeLink(lId);
            codeDirty = true;
        }
    }
}
```

---

### P7 — Nowe okna node editora nie renderują się / są puste

**Przyczyna**: `ImNodes::BeginNodeEditor()` musi być wywołane WEWNĄTRZ
`ImGui::Begin()` ... `ImGui::End()`. I musi być jedno wywołanie na okno.

Jeśli masz dwa node edytory (Signal A i B), każdy musi być w osobnym
`ImGui::Begin/End` bloku z innym tytułem.

**Ważne**: Dwa node edytory JEDNOCZEŚNIE na ekranie wymagają dwóch osobnych
kontekstów imnodes LUB imnodes obsługuje to wbudowanie (sprawdź dokumentację
swojej wersji imnodes). Jeśli imnodes nie obsługuje kilku edytorów jednocześnie,
użyj zakładek (ImGui::BeginTabBar) i renderuj tylko aktywny.

---

### P8 — Kompilacja: `LNK2019` dla `evaluateSignal` lub `computeSignalCpu`

**Przyczyna**: `signal_graph.cpp` nie jest w projekcie.

**Rozwiązanie**: Sprawdź `premake5.lua` — `"src/**.cpp"` powinno obejmować
`signal_graph.cpp` i `code_gen.cpp`. Zregeneruj projekt.

---

### P9 — NVRTC błąd: `illegal token` w wygenerowanym kodzie

**Objaw**: Log NVRTC pokazuje błąd składni w wygenerowanym kodzie przy znakach
`<`, `>`, `&` itp.

**Przyczyna**: Funkcja `D(double v)` używa `snprintf("%.17g")` który może
wygenerować `nan`, `inf` lub `-nan` jeśli parametr węzła jest nieprawidłowy.

**Rozwiązanie**: Dodaj walidację parametrów przy DragFloat (clamp do sensownych granic):
```cpp
// Przy DragFloat dla sigma (Gaussian — musi być > 0):
node.params[1] = std::max(node.params[1], 0.001f);
```

---

## 15. Checklista Fazy 4

```
Vendor i build:
[ ] vendor/imnodes/imnodes.h + imnodes.cpp skopiowane
[ ] premake5.lua zaktualizowany (files + includedirs)
[ ] Build Debug bez błędów
[ ] imnodes.cpp widoczny jako kompilowany przez cl.exe

Inicjalizacja:
[ ] ImNodes::CreateContext() wywołane
[ ] ImNodes::DestroyContext() w shutdown
[ ] App startuje bez crash

Node editor — Graf Signal A:
[ ] Okno "Node Editor — Sygnał A" widoczne
[ ] Domyślny graf: Gaussian → OUTPUT (pasek Fazy 1)
[ ] Węzły renderują się ze swoimi tytułami i pinami
[ ] Parametry Gaussian edytowalne przez DragFloat
[ ] Prawy klik → menu z listą węzłów

Interakcja z grafem:
[ ] Można dodać węzeł Gaussian przez menu
[ ] Można połączyć dwa węzły (przeciągnij od output do input)
[ ] Połączenie pojawia się jako linia
[ ] Można usunąć połączenie (Ctrl+Click lub Delete)
[ ] Można usunąć węzeł (Delete) — poza OUTPUT
[ ] Węzeł OUTPUT NIE daje się usunąć

Generator kodu:
[ ] Przycisk "Generuj kod + Kompiluj NVRTC" generuje string CUDA
[ ] Wygenerowany kod widoczny w oknie "Wygenerowany kod CUDA"
[ ] Dla Gaussian(mu=0.3, sigma=0.05): kod zawiera "exp(-0.5*..."
[ ] Kompilacja NVRTC: "OK (X ms)" dla prawidłowego grafu
[ ] Kompilacja NVRTC: komunikat błędu dla pustego grafu

Pipeline GPU:
[ ] Po "Generuj + Kompiluj" + dirty=true: obliczenie startuje
[ ] Wynik splotu wyświetla się w ImPlot
[ ] Signal A w ImPlot odpowiada kształtowi Gaussian (jak Phase 1)
[ ] Signal B w ImPlot to rectangle (hardcoded Phase 4)
[ ] maxAbsError < 1e-9

Zmiany w grafie → nowy wynik:
[ ] Zmień mu Gaussian → "Generuj + Kompiluj" → nowy wynik
[ ] Dodaj Scale(2.0) między Gaussian a OUTPUT → "Generuj" → amplituda ×2

Faza 1/2/3 nadal działają:
[ ] Hot-reload kernela splotu (z Fazy 2) nadal dostępny
[ ] UI responsywne podczas obliczeń (spinner z Fazy 3)
```

---

## 16. Checklista Fazy 5

```
Drugi graf Signal B:
[ ] graphB.initDefault(Rectangle, {0.60f, 0.80f}) w appInit
[ ] Okno "Node Editor — Sygnał B" widoczne
[ ] Domyślny graf: Rectangle → OUTPUT
[ ] Wygenerowany kod Signal B odpowiada kształtowi rectangle

Integracja:
[ ] Zmiana Signal B → "Generuj + Kompiluj" → nowy wynik
[ ] Signal B w ImPlot pochodzi z GPU (nie hardcoded)
[ ] maxAbsError < 1e-9 dla obu sygnałów z grafów

Suwak N:
[ ] SliderInt log₂(N) ∈ [10, 16] działa
[ ] Zmiana N → realokacja buforów → nowe obliczenie
[ ] N=1024: wynik poprawny (maxAbsError < 1e-9)
[ ] N=4096: wynik identyczny jak Phase 1 (baseline)
[ ] N=65536: aplikacja nie crasha, GPU oblicza w < 2s
[ ] Ostrzeżenie o CPU reference dla N > 8192

Pominięcie CPU reference:
[ ] Dla N > 8192: maxAbsError wyświetla "(pominięto)"
[ ] GPU obliczenie nadal przebiega normalnie

Fazy 1–4 nadal działają:
[ ] Hot-reload kernela splotu (Faza 2)
[ ] UI responsywne (Faza 3)
[ ] Node editor Signal A nadal edytowalny
```

---

## Appendix A — Minimalny test generatora kodu (debug helper)

Wstaw do `appInit` po inicjalizacji grafów, żeby przetestować generator
bez GPU:

```cpp
// TEST CODE GEN — usuń po weryfikacji
{
    float params[] = {0.30f, 0.05f};
    NodeGraph testGraph;
    testGraph.initDefault(NodeType::Gaussian, params);
    std::string code = buildSignalKernel(testGraph, "testFunc");
    fprintf(stdout, "=== GENERATED CODE ===\n%s\n=== END ===\n", code.c_str());

    // Weryfikacja CPU evaluatora
    double val = 0.0;
    const SignalNode* outNode = testGraph.findOutputNode();
    int srcId = testGraph.findSourceNodeId(outNode->inputAttrIds[0]);
    val = evaluateSignal(testGraph, srcId, 0.30);  // t=mu → peak powinien być 1.0
    fprintf(stdout, "CPU eval at t=mu: %.6f (expected: 1.0)\n", val);
}
```

---

## Appendix B — Obsługiwane wzory dla każdego generatora

| Generator | Wzór (t ∈ [0, 1] s)         | Parametry   |
| --------- | --------------------------- | ----------- |
| Constant  | `a`                         | a           |
| Sine      | `A · sin(2π·f·t)`           | f [Hz], A   |
| Cosine    | `A · cos(2π·f·t)`           | f [Hz], A   |
| Rectangle | `1 if t0 ≤ t < t1 else 0`   | t0, t1      |
| Triangle  | `1 - 2                      | t-c         | /w if | t-c | <w/2 else 0` | center, width |
| Sawtooth  | `t/T - floor(t/T)`          | period T    |
| Gaussian  | `exp(-½·((t-μ)/σ)²)`        | μ, σ        |
| ExpDecay  | `A·exp(-λ·t) if t≥0 else 0` | λ, A        |
| Heaviside | `1 if t ≥ t0 else 0`        | t0          |
| Sinc      | `sin(s·(t-t0))/(s·(t-t0))`  | t0, scale s |

Operatory modyfikują wynik poprzednich węzłów:
- **Sum**: `in1 + in2`
- **Product**: `in1 * in2`
- **Scale**: `in · a`
- **TimeShift**: `in(t - τ)`
- **Reflect**: `in(1.0 - t)` (odbicie symetryczne względem t=0.5)

---

