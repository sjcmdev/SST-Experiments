// signal_graph.h
#pragma once
#include <vector>
#include <string>
#include <cmath>

// ---------------------------------------------------------------------------
// Typy węzłów
// ---------------------------------------------------------------------------
enum class NodeType
{
    // Generatory (0 wejść, generują sygnał z t)
    Constant = 0,
    Sine = 1,
    Cosine = 2,
    Rectangle = 3,
    Triangle = 4,
    Sawtooth = 5,
    Gaussian = 6,
    ExpDecay = 7,
    Heaviside = 8,
    Sinc = 9,
    // Operatory (1-2 wejścia)
    Sum = 10,       // 2 wejścia
    Product = 11,   // 2 wejścia
    Scale = 12,     // 1 wejście + param
    TimeShift = 13, // 1 wejście + param
    Reflect = 14,   // 1 wejście (odbicie: T-t)
    // Specjalny
    Output = 15, // 1 wejście, brak wyjścia — oznacznik końca grafu
};

// ---------------------------------------------------------------------------
// Informacje o typie węzła (liczba wejść, parametrów, etykiety)
// ---------------------------------------------------------------------------
struct NodeTypeInfo
{
    const char *label;
    int numInputs; // 0, 1, lub 2
    int numParams; // 0-4 parametrów float
    const char *paramNames[4];
    float defaultParams[4];
};

// Wywoływana przez kod — zwraca info o danym typie
NodeTypeInfo getNodeTypeInfo(NodeType type);

// Etykiety dla menu "Dodaj węzeł"
const char *getNodeTypeLabel(NodeType type);

// ---------------------------------------------------------------------------
// Węzeł sygnału
// ---------------------------------------------------------------------------
struct SignalNode
{
    int id; // unikalny ID węzła (dla imnodes)
    NodeType type;
    float params[4] = {}; // wartości parametrów (max 4)

    // IDs pinów (dla imnodes) — generowane z globalnego licznika
    int inputAttrIds[2] = {-1, -1}; // ID pinu wejściowego [0] i [1]
    int outputAttrId = -1;          // ID pinu wyjściowego (-1 dla Output)
};

// ---------------------------------------------------------------------------
// Połączenie między węzłami
// ---------------------------------------------------------------------------
struct SignalLink
{
    int id;        // unikalny ID linku (dla imnodes)
    int srcAttrId; // outputAttrId węzła źródłowego
    int dstAttrId; // inputAttrIds[x] węzła docelowego
};

// ---------------------------------------------------------------------------
// Graf sygnału — jeden pełny graf (dla sygnału A lub B)
// ---------------------------------------------------------------------------
struct NodeGraph
{
    std::vector<SignalNode> nodes;
    std::vector<SignalLink> links;
    int nextId = 1; // globalny licznik ID (rosnie, nigdy nie spada)

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
    const SignalNode *findOutputNode() const;

    // Znajdź węzeł po ID. Zwraca nullptr jeśli nie istnieje.
    SignalNode *findNodeById(int id);
    const SignalNode *findNodeById(int id) const;

    // Znajdź węzeł podłączony do danego pinu wejściowego.
    // Zwraca -1 jeśli nic nie jest podłączone.
    int findSourceNodeId(int dstAttrId) const;

    // Czy graf jest prawidłowy (ma OUTPUT, wszystkie wymagane wejścia podłączone)?
    bool isValid() const;

    // Zainicjalizuj domyślnym węzłem OUTPUT + jednym generatorem.
    void initDefault(NodeType generatorType, const float *params = nullptr);
};

// ---------------------------------------------------------------------------
// CPU ewaluator — oblicza wartość sygnału w punkcie t (dla referencji CPU)
// ---------------------------------------------------------------------------
// T = całkowity czas (domyślnie 1.0 s)
double evaluateSignal(const NodeGraph &graph, int nodeId, double t, double T = 1.0);

// Oblicz cały sygnał na CPU (N próbek). Wynik w out[0..N-1].
void computeSignalCpu(const NodeGraph &graph, double *out, int N, double dt);