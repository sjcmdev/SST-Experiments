// src/solver/nelder_mead.hpp
#pragma once
#include "solver_types.hpp"
#include <functional>
#include <string>
#include <vector>

/// Rdzeń solvera Nelder-Mead (Etap 2 — bez SA).
///
/// Solver jest MODEL-AGNOSTYCZNY: nie wie nic o diodach ani IV. Przyjmuje
/// dowolną ObjectiveFn (free_values -> wartość skalarna do minimalizacji).
/// Powiązanie z konkretnym modelem fizycznym (np. evaluateDiodeIV4 + dane
/// pomiarowe) odbywa się NA ZEWNĄTRZ tej klasy — patrz diode_objective.hpp.
///
/// WAŻNE (Etap 3 preview — patrz "Decyzje architektoniczne" w planie):
/// gałąź "reflection odrzucona" w step() zawiera oznaczony komentarzem punkt,
/// w którym Etap 3 wstawi kryterium akceptacji Metropolis (SA). Przy T=0
/// (stan Etapu 2) zachowanie jest identyczne z klasycznym NM — to jest
/// CELOWA właściwość architektury, nie przypadek.
class SANelderMead
{
public:
    /// free_values (len = N_free) -> wartość do minimalizacji.
    /// Kontrakt: NIGDY nie rzuca wyjątku; zwraca +∞ dla wejść niefizycznych/
    /// niepoprawnych (patrz computeChiSquared z Etapu 1 jako wzorzec).
    using ObjectiveFn = std::function<double(const std::vector<double> &)>;

    /// all_params      — WSZYSTKIE parametry (free+fixed) w stałej kolejności,
    ///                    TEJ SAMEJ co użyta przy budowie objective_fn
    /// objective_fn    — patrz wyżej; typowo budowane przez makeDiodeIVObjective()
    /// alpha,gamma,rho,sigma — współczynniki NM: reflection/expansion/contraction/shrink
    /// degenerate_tol  — próg detekcji degeneracji simpleksu (odległość euklidesowa);
    ///                    musi być wyraźnie mniejszy niż 5% typowego zakresu parametru,
    ///                    inaczej solver zacznie się od natychmiastowego restartu
    ///
    /// Rzuca std::invalid_argument gdy:
    ///   - brak wolnych parametrów (wszystkie free=false)
    ///   - dla któregoś wolnego parametru: min >= max
    ///   - dla któregoś wolnego parametru: value poza [min,max]
    ///   - degenerate_tol <= 0
    SANelderMead(
        std::vector<FitParam> all_params,
        ObjectiveFn objective_fn,
        double alpha = 1.0,
        double gamma = 2.0,
        double rho = 0.5,
        double sigma = 0.5,
        double degenerate_tol = 1e-12);

    /// Inicjalizuje simpleks (N_free+1 wierzchołków) wokół aktualnych wartości
    /// FitParam::value (z all_params) dla wolnych parametrów. Musi być
    /// wywołane raz, przed pierwszym step().
    void initSimplex();

    /// Wykonuje DOKŁADNIE jeden krok algorytmu. Zwraca typ wykonanej operacji.
    /// Zwiększa iteration o 1 — zawsze, niezależnie od typu operacji (w tym Restart).
    /// Rzuca std::logic_error gdy wywołane przed initSimplex().
    StepType step();

    /// Wykonuje step() w pętli aż do zbieżności (chi2_best < chi2_tol) lub
    /// osiągnięcia max_iter. Oba kryteria konfigurowalne per wywołanie.
    FitResult runUntilConvergence(int max_iter, double chi2_tol);

    // ─── Odczyt stanu ────────────────────────────────────────────────────
    const SimplexState &state() const noexcept { return state_; }
    int iteration() const noexcept { return state_.iteration; }
    double bestChiSquared() const noexcept { return state_.chi2_values[state_.best_idx]; }

    /// TYLKO wolne parametry, w kolejności free_indices. Zgodne z FitResult::best_params.
    std::vector<double> bestParams() const { return state_.vertices[state_.best_idx]; }

    /// PEŁNY wektor parametrów (free+fixed), w kolejności all_params.
    /// Wygodne dla UI/logowania — FitResult::best_params zawiera tylko wolne.
    std::vector<double> bestFullParams() const;

private:
    std::vector<FitParam> all_params_;
    std::vector<int> free_indices_;
    ObjectiveFn objective_fn_;

    double alpha_, gamma_, rho_, sigma_;
    double degenerate_tol_;

    std::vector<double> free_min_; // cache, len = N_free
    std::vector<double> free_max_; // cache, len = N_free

    SimplexState state_;

    void initSimplexAround(const std::vector<double> &start);
    void restart();
    bool isDegenerate() const;
    void clipToBounds(std::vector<double> &v) const;
    bool withinBounds(const std::vector<double> &v) const;
    double evaluateVertex(const std::vector<double> &free_values) const;
    void sortIndices(int &best, int &second_worst, int &worst) const;
    std::vector<double> computeCentroidExcluding(int exclude_idx) const;
    void shrinkSimplex(int best_idx);
    FitResult buildResult(bool converged, const std::string &stop_reason) const;
};

// ─── Operacje geometryczne simpleksu — wolne funkcje ─────────────────────────
// Wydzielone z klasy celowo: testowalne w izolacji bez budowania całego solvera.

std::vector<double> reflectPoint(
    const std::vector<double> &centroid, const std::vector<double> &worst, double alpha);

std::vector<double> expandPoint(
    const std::vector<double> &centroid, const std::vector<double> &x_r, double gamma);

std::vector<double> contractPoint(
    const std::vector<double> &centroid, const std::vector<double> &worst, double rho);