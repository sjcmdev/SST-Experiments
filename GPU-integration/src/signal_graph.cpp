// signal_graph.cpp
#include "signal_graph.hpp"
#include <algorithm>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Informacje o typach węzłów
// ---------------------------------------------------------------------------
NodeTypeInfo getNodeTypeInfo(NodeType type)
{
    switch (type)
    {
    case NodeType::Constant:
        return {"Constant", 0, 1, {"value", "", "", ""}, {1.0f, 0, 0, 0}};
    case NodeType::Sine:
        return {"Sine", 0, 2, {"freq(Hz)", "amp", "", ""}, {5.0f, 1.0f, 0, 0}};
    case NodeType::Cosine:
        return {"Cosine", 0, 2, {"freq(Hz)", "amp", "", ""}, {5.0f, 1.0f, 0, 0}};
    case NodeType::Rectangle:
        return {"Rectangle", 0, 2, {"t_start", "t_end", "", ""}, {0.25f, 0.75f, 0, 0}};
    case NodeType::Triangle:
        return {"Triangle", 0, 2, {"center", "width", "", ""}, {0.5f, 0.4f, 0, 0}};
    case NodeType::Sawtooth:
        return {"Sawtooth", 0, 1, {"period", "", "", ""}, {0.5f, 0, 0, 0}};
    case NodeType::Gaussian:
        return {"Gaussian", 0, 2, {"mu", "sigma", "", ""}, {0.30f, 0.05f, 0, 0}};
    case NodeType::ExpDecay:
        return {"ExpDecay", 0, 2, {"lambda", "amp", "", ""}, {5.0f, 1.0f, 0, 0}};
    case NodeType::Heaviside:
        return {"Heaviside", 0, 1, {"t0", "", "", ""}, {0.5f, 0, 0, 0}};
    case NodeType::Sinc:
        return {"Sinc", 0, 2, {"t0", "scale", "", ""}, {0.5f, 10.0f, 0, 0}};
    case NodeType::Sum:
        return {"Sum", 2, 0, {"", "", "", ""}, {0, 0, 0, 0}};
    case NodeType::Product:
        return {"Product", 2, 0, {"", "", "", ""}, {0, 0, 0, 0}};
    case NodeType::Scale:
        return {"Scale", 1, 1, {"scale", "", "", ""}, {2.0f, 0, 0, 0}};
    case NodeType::TimeShift:
        return {"TimeShift", 1, 1, {"tau(s)", "", "", ""}, {0.1f, 0, 0, 0}};
    case NodeType::Reflect:
        return {"Reflect", 1, 0, {"", "", "", ""}, {0, 0, 0, 0}};
    case NodeType::Output:
        return {"OUTPUT", 1, 0, {"", "", "", ""}, {0, 0, 0, 0}};
    default:
        return {"?", 0, 0, {"", "", "", ""}, {0, 0, 0, 0}};
    }
}

const char *getNodeTypeLabel(NodeType t)
{
    return getNodeTypeInfo(t).label;
}


// ---------------------------------------------------------------------------
// NodeGraph — implementacja
// ---------------------------------------------------------------------------

int NodeGraph::addNode(NodeType type, float posX, float posY)
{
    (void)posX;
    (void)posY; // pozycja ustawiana przez imnodes osobno
    NodeTypeInfo info = getNodeTypeInfo(type);

    SignalNode n;
    n.id = nextId++;
    n.type = type;

    // Kopiuj domyślne parametry
    memcpy(n.params, info.defaultParams, sizeof(n.params));

    // Przydziel ID pinów wejściowych
    for (int i = 0; i < info.numInputs; ++i)
    {
        n.inputAttrIds[i] = nextId++;
    }

    // Przydziel ID pinu wyjściowego (poza typem Output)
    if (type != NodeType::Output)
    {
        n.outputAttrId = nextId++;
    }

    nodes.push_back(n);
    return n.id;
}

void NodeGraph::removeNode(int nodeId)
{
    // Usuń wszystkie linki związane z tym węzłem
    SignalNode *n = findNodeById(nodeId);
    if (!n)
        return;

    links.erase(std::remove_if(links.begin(), links.end(),
                               [&](const SignalLink &lnk)
                               {
                                   return lnk.srcAttrId == n->outputAttrId || lnk.dstAttrId == n->inputAttrIds[0] || lnk.dstAttrId == n->inputAttrIds[1];
                               }),
                links.end());

    // Usuń węzeł
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                               [nodeId](const SignalNode &nd)
                               { return nd.id == nodeId; }),
                nodes.end());
}

int NodeGraph::addLink(int srcAttrId, int dstAttrId)
{
    // Jeden pin wejściowy może mieć max jedno połączenie
    for (const auto &lnk : links)
    {
        if (lnk.dstAttrId == dstAttrId)
            return -1; // już zajęty
    }
    SignalLink lnk;
    lnk.id = nextId++;
    lnk.srcAttrId = srcAttrId;
    lnk.dstAttrId = dstAttrId;
    links.push_back(lnk);
    return lnk.id;
}

void NodeGraph::removeLink(int linkId)
{
    links.erase(std::remove_if(links.begin(), links.end(),
                               [linkId](const SignalLink &l)
                               { return l.id == linkId; }),
                links.end());
}

const SignalNode *NodeGraph::findOutputNode() const
{
    for (const auto &n : nodes)
        if (n.type == NodeType::Output)
            return &n;
    return nullptr;
}

SignalNode *NodeGraph::findNodeById(int id)
{
    for (auto &n : nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}

const SignalNode *NodeGraph::findNodeById(int id) const
{
    for (const auto &n : nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}

int NodeGraph::findSourceNodeId(int dstAttrId) const
{
    for (const auto &lnk : links)
    {
        if (lnk.dstAttrId == dstAttrId)
        {
            // Znajdź węzeł z tym outputAttrId
            for (const auto &n : nodes)
            {
                if (n.outputAttrId == lnk.srcAttrId)
                    return n.id;
            }
        }
    }
    return -1;
}

bool NodeGraph::isValid() const
{
    const SignalNode *out = findOutputNode();
    if (!out)
        return false;
    // Sprawdź czy OUTPUT ma podłączone wejście
    if (findSourceNodeId(out->inputAttrIds[0]) < 0)
        return false;
    return true;
}

void NodeGraph::initDefault(NodeType generatorType, const float *params)
{
    nodes.clear();
    links.clear();
    nextId = 1;

    // Dodaj generator
    int genId = addNode(generatorType);
    // Nadpisz parametry jeśli podane
    if (params)
    {
        NodeTypeInfo info = getNodeTypeInfo(generatorType);
        for (int i = 0; i < info.numParams; ++i)
        {
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

double evaluateSignal(const NodeGraph &graph, int nodeId, double t, double T)
{
    const SignalNode *node = graph.findNodeById(nodeId);
    if (!node)
        return 0.0;

    const double PI2 = 6.283185307179586;

    switch (node->type)
    {
    case NodeType::Constant:
        return (double)node->params[0];

    case NodeType::Sine:
        return (double)node->params[1] * std::sin(PI2 * (double)node->params[0] * t);

    case NodeType::Cosine:
        return (double)node->params[1] * std::cos(PI2 * (double)node->params[0] * t);

    case NodeType::Rectangle:
    {
        double t0 = node->params[0], t1 = node->params[1];
        return (t >= t0 && t < t1) ? 1.0 : 0.0;
    }
    case NodeType::Triangle:
    {
        double center = node->params[0], width = node->params[1];
        double d = std::fabs(t - center);
        return (d < width * 0.5) ? (1.0 - 2.0 * d / width) : 0.0;
    }
    case NodeType::Sawtooth:
    {
        double period = node->params[0];
        if (period <= 0.0)
            return 0.0;
        double v = t / period;
        return v - std::floor(v);
    }
    case NodeType::Gaussian:
    {
        double mu = node->params[0], sigma = node->params[1];
        if (sigma <= 0.0)
            return 0.0;
        double d = (t - mu) / sigma;
        return std::exp(-0.5 * d * d);
    }
    case NodeType::ExpDecay:
    {
        double lam = node->params[0], amp = node->params[1];
        return (t >= 0.0) ? amp * std::exp(-lam * t) : 0.0;
    }
    case NodeType::Heaviside:
        return (t >= (double)node->params[0]) ? 1.0 : 0.0;

    case NodeType::Sinc:
    {
        double t0 = node->params[0], scale = node->params[1];
        double arg = scale * (t - t0);
        return (std::fabs(arg) < 1e-10) ? 1.0 : std::sin(arg) / arg;
    }

    // Operatory — pobierz wartości wejść, oblicz wynik
    case NodeType::Sum:
    {
        int src0 = graph.findSourceNodeId(node->inputAttrIds[0]);
        int src1 = graph.findSourceNodeId(node->inputAttrIds[1]);
        double v0 = (src0 >= 0) ? evaluateSignal(graph, src0, t, T) : 0.0;
        double v1 = (src1 >= 0) ? evaluateSignal(graph, src1, t, T) : 0.0;
        return v0 + v1;
    }
    case NodeType::Product:
    {
        int src0 = graph.findSourceNodeId(node->inputAttrIds[0]);
        int src1 = graph.findSourceNodeId(node->inputAttrIds[1]);
        double v0 = (src0 >= 0) ? evaluateSignal(graph, src0, t, T) : 0.0;
        double v1 = (src1 >= 0) ? evaluateSignal(graph, src1, t, T) : 0.0;
        return v0 * v1;
    }
    case NodeType::Scale:
    {
        int src = graph.findSourceNodeId(node->inputAttrIds[0]);
        double v = (src >= 0) ? evaluateSignal(graph, src, t, T) : 0.0;
        return v * (double)node->params[0];
    }
    case NodeType::TimeShift:
    {
        int src = graph.findSourceNodeId(node->inputAttrIds[0]);
        double tau = node->params[0];
        return (src >= 0) ? evaluateSignal(graph, src, t - tau, T) : 0.0;
    }
    case NodeType::Reflect:
    {
        int src = graph.findSourceNodeId(node->inputAttrIds[0]);
        return (src >= 0) ? evaluateSignal(graph, src, T - t, T) : 0.0;
    }
    case NodeType::Output:
    {
        int src = graph.findSourceNodeId(node->inputAttrIds[0]);
        return (src >= 0) ? evaluateSignal(graph, src, t, T) : 0.0;
    }
    default:
        return 0.0;
    }
}

void computeSignalCpu(const NodeGraph &graph, double *out, int N, double dt)
{
    const SignalNode *outNode = graph.findOutputNode();
    if (!outNode)
    {
        for (int i = 0; i < N; i++)
            out[i] = 0.0;
        return;
    }
    int srcId = graph.findSourceNodeId(outNode->inputAttrIds[0]);
    for (int i = 0; i < N; ++i)
    {
        double t = (double)i * dt;
        out[i] = (srcId >= 0) ? evaluateSignal(graph, srcId, t) : 0.0;
    }
}