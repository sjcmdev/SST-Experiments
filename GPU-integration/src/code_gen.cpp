// code_gen.cpp
#include "code_gen.hpp"
#include <sstream>
#include <cstdio>

// Formatuj double z pełną precyzją (17 cyfr znaczących)
static std::string D(double v)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%.17g", v);
    // Upewnij się że jest to literał double (dodaj '.0' jeśli brak kropki)
    std::string s(buf);
    bool hasDot = (s.find('.') != std::string::npos);
    bool hasE = (s.find('e') != std::string::npos || s.find('E') != std::string::npos);
    if (!hasDot && !hasE)
        s += ".0";
    return s;
}

// ---------------------------------------------------------------------------
std::string generateExpr(const NodeGraph &graph, int nodeId,
                         const std::string &timeVar)
{
    const SignalNode *node = graph.findNodeById(nodeId);
    if (!node)
        return "0.0";

    const double PI2 = 6.283185307179586;

    switch (node->type)
    {
    case NodeType::Constant:
        return D(node->params[0]);

    case NodeType::Sine:
        return "((" + D(node->params[1]) + ") * sin(" +
               D(PI2 * node->params[0]) + " * " + timeVar + "))";

    case NodeType::Cosine:
        return "((" + D(node->params[1]) + ") * cos(" +
               D(PI2 * node->params[0]) + " * " + timeVar + "))";

    case NodeType::Rectangle:
    {
        double t0 = node->params[0], t1 = node->params[1];
        return "(" + timeVar + " >= " + D(t0) + " && " + timeVar + " < " + D(t1) +
               " ? 1.0 : 0.0)";
    }
    case NodeType::Triangle:
    {
        double c = node->params[0], w = node->params[1];
        std::string half = D(w * 0.5);
        std::string iw2 = D(2.0 / w);
        return "(fabs(" + timeVar + " - " + D(c) + ") < " + half +
               " ? (1.0 - " + iw2 + " * fabs(" + timeVar + " - " + D(c) + ")) : 0.0)";
    }
    case NodeType::Sawtooth:
    {
        double per = node->params[0];
        if (per <= 0.0)
            return "0.0";
        std::string v = "(" + timeVar + " / " + D(per) + ")";
        return "(" + v + " - floor(" + v + "))";
    }
    case NodeType::Gaussian:
    {
        double mu = node->params[0], sigma = node->params[1];
        if (sigma <= 0.0)
            return "0.0";
        std::string d = "((" + timeVar + " - " + D(mu) + ") / " + D(sigma) + ")";
        return "(exp(-0.5 * " + d + " * " + d + "))";
    }
    case NodeType::ExpDecay:
    {
        double lam = node->params[0], amp = node->params[1];
        return "(" + timeVar + " >= 0.0 ? " + D(amp) + " * exp(-" +
               D(lam) + " * " + timeVar + ") : 0.0)";
    }
    case NodeType::Heaviside:
        return "(" + timeVar + " >= " + D(node->params[0]) + " ? 1.0 : 0.0)";

    case NodeType::Sinc:
    {
        double t0 = node->params[0], scale = node->params[1];
        std::string arg = "(" + D(scale) + " * (" + timeVar + " - " + D(t0) + "))";
        // Warunek na osobno dla czytelności generowanego kodu
        return "(fabs(" + arg + ") < 1e-10 ? 1.0 : (sin(" + arg + ") / " + arg + "))";
    }

    // Operatory
    case NodeType::Sum:
    {
        int src0 = graph.findSourceNodeId(node->inputAttrIds[0]);
        int src1 = graph.findSourceNodeId(node->inputAttrIds[1]);
        std::string e0 = (src0 >= 0) ? generateExpr(graph, src0, timeVar) : "0.0";
        std::string e1 = (src1 >= 0) ? generateExpr(graph, src1, timeVar) : "0.0";
        return "(" + e0 + " + " + e1 + ")";
    }
    case NodeType::Product:
    {
        int src0 = graph.findSourceNodeId(node->inputAttrIds[0]);
        int src1 = graph.findSourceNodeId(node->inputAttrIds[1]);
        std::string e0 = (src0 >= 0) ? generateExpr(graph, src0, timeVar) : "0.0";
        std::string e1 = (src1 >= 0) ? generateExpr(graph, src1, timeVar) : "0.0";
        return "(" + e0 + " * " + e1 + ")";
    }
    case NodeType::Scale:
    {
        int src = graph.findSourceNodeId(node->inputAttrIds[0]);
        std::string e = (src >= 0) ? generateExpr(graph, src, timeVar) : "0.0";
        return "((" + D(node->params[0]) + ") * " + e + ")";
    }
    case NodeType::TimeShift:
    {
        int src = graph.findSourceNodeId(node->inputAttrIds[0]);
        // Kluczowe: przekazujemy NOWĄ zmienną czasu jako wyrażenie
        // Dla prostoty używamy zagnieżdżonego wyrażenia (t - tau)
        std::string shifted = "(" + timeVar + " - " + D(node->params[0]) + ")";
        return (src >= 0) ? generateExpr(graph, src, shifted) : "0.0";
    }
    case NodeType::Reflect:
    {
        int src = graph.findSourceNodeId(node->inputAttrIds[0]);
        // T = 1.0 — całkowity czas trwania sygnału
        std::string reflected = "(1.0 - " + timeVar + ")";
        return (src >= 0) ? generateExpr(graph, src, reflected) : "0.0";
    }
    case NodeType::Output:
    {
        int src = graph.findSourceNodeId(node->inputAttrIds[0]);
        return (src >= 0) ? generateExpr(graph, src, timeVar) : "0.0";
    }
    default:
        return "0.0";
    }
}

// ---------------------------------------------------------------------------
std::string buildSignalKernel(const NodeGraph &graph, const std::string &funcName)
{
    if (!graph.isValid())
    {
        // Zwróć kernel zwracający 0 — nie crashuje, tylko daje pusty sygnał
        return "extern \"C\" __global__ void " + funcName +
               "(double* out, int N, double dt) {\n"
               "    int i = blockIdx.x * blockDim.x + threadIdx.x;\n"
               "    if (i >= N) return;\n"
               "    out[i] = 0.0; // Graf nieprawidłowy\n"
               "}\n";
    }

    const SignalNode *outNode = graph.findOutputNode();
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
std::string getHardcodedSignalBKernel()
{
    // Hardcoded rectangle (Phase 1 Signal B) — używane w Fazie 4
    // gdy graf Signal B nie istnieje lub jest nieprawidłowy.
    return "extern \"C\" __global__ void generateSignalB"
           "(double* out, int N, double dt) {\n"
           "    int i = blockIdx.x * blockDim.x + threadIdx.x;\n"
           "    if (i >= N) return;\n"
           "    double t = (double)i * dt;\n"
           "    out[i] = (t >= 0.60 && t < 0.80) ? 1.0 : 0.0;\n"
           "}\n";
}

// ---------------------------------------------------------------------------
std::string buildSignalModule(const NodeGraph &graphA, const NodeGraph &graphB)
{
    std::string code;
    code += "// AUTO-GENERATED by code_gen.cpp — nie edytuj ręcznie\n\n";

    // Sygnał A — zawsze z grafu
    code += buildSignalKernel(graphA, "generateSignalA");
    code += "\n";

    // Sygnał B — z grafu (Faza 5) lub hardcoded (Faza 4)
    if (graphB.isValid())
    {
        code += buildSignalKernel(graphB, "generateSignalB");
    }
    else
    {
        code += getHardcodedSignalBKernel();
    }

    return code;
}