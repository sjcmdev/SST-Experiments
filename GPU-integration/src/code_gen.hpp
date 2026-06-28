// code_gen.h
#pragma once
#include "signal_graph.hpp"
#include <string>

// Wygeneruj wyrażenie CUDA dla węzła nodeId, używając timeVar jako zmiennej t.
// Wywołanie rekurencyjne — przebiega graf od liści do korzenia.
std::string generateExpr(const NodeGraph &graph, int nodeId,
                         const std::string &timeVar = "t");

// Wygeneruj kompletny __global__ kernel do generacji sygnału.
// funcName: "generateSignalA" lub "generateSignalB"
// Zwraca pusty string jeśli graf jest nieprawidłowy.
std::string buildSignalKernel(const NodeGraph &graph,
                              const std::string &funcName);

// Wygeneruj CAŁY string do kompilacji NVRTC:
//   generateSignalA (z graphA)
//   generateSignalB (z graphB, lub hardcoded rectangle jeśli graphB.nodes puste)
// Nie zawiera kernela convolution — jest w osobnym module.
std::string buildSignalModule(const NodeGraph &graphA, const NodeGraph &graphB);

// Hardcoded Signal B (rectangle) — używany w Fazie 4 gdy graphB nie jest gotowy
std::string getHardcodedSignalBKernel();