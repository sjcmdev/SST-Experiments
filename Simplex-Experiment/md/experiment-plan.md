# Plan Implementacji — Faza I CPU
## SA-Enhanced Nelder–Mead dla modelu IV diody

> Każdy etap kończy się **działającym, testowalnym kodem**.
> Kolejne etapy budują na fundamencie poprzednich — nie ma skoków.
> Pełny opis projektu i API: **experiment-overview.md**

---

## Etap 1 — Struktury danych i model IV diody

**Cel:** Zdefiniować wszystkie typy danych projektu. Zaimplementować
i zweryfikować LambertW oraz funkcję modelu IV. Na końcu tego etapu
wiadomo, że prąd jest liczony poprawnie.

### Struktury danych

- [ ] `enum class CoolingSchedule { Boltzmann, Geometric, Adaptive }`
- [ ] `struct FitParam { string name; double value, min, max; bool free; }`
- [ ] `struct SAConfig { double T_initial, T_current, geometric_rate; CoolingSchedule schedule; }`
- [ ] `struct SimplexState` — macierz wierzchołków (N+1) × N_free, chi2 per wierzchołek,
      indeksy best/worst, centroid, numer iteracji, T_current
- [ ] `enum class StepType { Reflection, Expansion, Contraction, Shrink, Restart }`
- [ ] `struct TraceStep` — type, state_before, state_after, chi2_min, T, iteration
- [ ] `struct FitResult` — best_params, chi2_min, delta_chi2, iterations, converged, stop_reason
- [ ] Wszystkie struktury mają sensowne wartości domyślne i kompilują się bez ostrzeżeń

### LambertW (CPU)

- [ ] Implementacja `double lambertW(double x)` — gałąź główna W₀
- [ ] Algorytm: iteracja Halleya z dobrym punktem startowym (inicjalizacja przez
      aproksymację logarytmiczną lub Fritsch et al.)
- [ ] Obsługa brzegowych przypadków: `x = 0` → 0, `x = -1/e` → -1,
      `x < -1/e` → NaN lub exception
- [ ] Zbieżność: max 5 iteracji dla typowego zakresu wartości modelu IV
- [ ] Wyniki zgodne z `scipy.special.lambertw` z tolerancją `< 1e-10` dla zakresu
      `x ∈ [-1/e + ε, 1e6]`

### Model IV — evaluateDiodeIV

- [ ] Sygnatura: `double evaluateDiodeIV(double V, const double* params)`
      gdzie `params = {I₀, A, Rₛ, Rₛₕ}` (skala liniowa)
- [ ] Implementacja jawnej formuły I(V) z LambertW (bez wewnętrznej iteracji Newtona)
- [ ] `Vt = kT/q ≈ 0.02585 V` (300 K) — hardkodowane lub przekazywane jako parametr
- [ ] Zwraca `NaN` gdy parametry poza zakresem numerycznym (nie crashuje)
- [ ] Wyniki zgodne z referencyjnym skryptem Python (`scipy`) dla znanych zestawów parametrów

### Funkcja celu

- [ ] `double computeChiSquared(params, V_data, I_meas, sigma, N_points, model_fn)`
- [ ] Zwraca `+∞` gdy którykolwiek `I_model(Vᵢ) = NaN`
- [ ] `double computeDeltaChiSquared(double chi2, double chi2_min)`

### Kryteria ukończenia Etapu 1

- [ ] `lambertW(x)` zgodny z `scipy.special.lambertw` z tol `< 1e-10`
      dla co najmniej 100 losowych punktów z zakresu typowego modelu IV
- [ ] `evaluateDiodeIV(V, params)` daje fizycznie sensowne I(V)
      (prąd rośnie wykładniczo w kierunku przewodzenia, nasycenie w zaporze)
- [ ] `computeChiSquared` = 0.0 gdy I_meas jest generowany przez ten sam model
      z tymi samymi parametrami (tolerancja numeryczna `< 1e-20`)
- [ ] Żadna funkcja nie rzuca wyjątku ani nie crashuje dla ekstremalnych,
      ale konfigurowalnych wartości parametrów

---

## Etap 2 — Nelder–Mead (bez SA)

**Cel:** Działający, deterministyczny solver NM. Izolacja od SA pozwala
na weryfikację algorytmu w prostych, analitycznych przypadkach testowych
przed dodaniem losowości SA.

### Inicjalizacja simpleksu

- [ ] `initSimplex(start_point, params)` — N+1 wierzchołków przez perturbację
      punktu startowego: `xᵢ = start ± δᵢ` (δ ≈ 5% zakresu `[min, max]`)
- [ ] Wszystkie wierzchołki spełniają granice `[min, max]` po inicjalizacji
- [ ] Obliczenie chi2 dla każdego wierzchołka przy inicjalizacji

### Operacje simpleksu

- [ ] Sortowanie wierzchołków według chi2; identyfikacja best, second_worst, worst
- [ ] Obliczanie centroidu: średnia N najlepszych wierzchołków (bez worst)
- [ ] **Reflection:** `x_r = centroid + α·(centroid - worst)`
- [ ] **Expansion:** `x_e = centroid + γ·(x_r - centroid)`
      — stosowana gdy `f(x_r) < f(best)`
- [ ] **Contraction:** `x_c = centroid + ρ·(worst - centroid)`
      — stosowana gdy reflection odrzucona
- [ ] **Shrink:** `xᵢ = best + σ·(xᵢ - best)` dla wszystkich i ≠ best
      — stosowana gdy contraction nie poprawia
- [ ] Koeficjenty konfigurowalne: α=1.0, γ=2.0, ρ=0.5, σ=0.5

### Obsługa granic i NaN

- [ ] Clipping: każdy wygenerowany wierzchołek przycinany do `[min, max]`
- [ ] Penalizacja: wierzchołek *poza* granicą po clippingu → chi2 = +∞
- [ ] NaN z funkcji modelu (np. I₀ < 0) → chi2 = +∞; solver kontynuuje
- [ ] Żaden z powyższych przypadków nie powoduje crash ani pętli nieskończonej

### Degeneracja simpleksu

- [ ] Detekcja: `max_distance(vertices) < degenerate_tol` (konfigurowalne, default 1e-12)
- [ ] Reakcja: automatyczny restart z perturbacją aktualnego best_point
- [ ] `stop_reason = "degenerate_restart"` zapisywane w TraceStep

### Kryteria zatrzymania

- [ ] `chi2_best < chi2_tol` — zbieżność
- [ ] `iteration >= max_iter` — limit iteracji
- [ ] Oba kryteria konfigurowalne

### Kryteria ukończenia Etapu 2

- [ ] Solver minimalizuje `f(x) = Σ(xᵢ - cᵢ)²` do residuum `< 1e-10`
      w mniej niż 1000 iteracji dla N ≤ 5 wymiarów, znany punkt minimum
- [ ] Solver odtwarza parametry z syntetycznych danych IV (generate → fit → recover)
      z tolerancją `< 1%` dla każdego z 4 parametrów przy sensownym punkcie startowym
- [ ] NaN i wyjście poza granice nie crashują solvera — pokryte testami
- [ ] Deterministyczność: dwa uruchomienia z tym samym punktem startowym
      i tymi samymi parametrami dają identyczne wyniki

---

## Etap 3 — SA Extension + harmonogramy chłodzenia

**Cel:** Integracja Simulated Annealing jako modyfikacji kryterium akceptacji.
Temperatura kontrolowana w runtime. Weryfikacja na przypadku z lokalnym minimum.

### Kryterium SA

- [ ] Modyfikacja pętli NM: gdy `f(x_r) ≥ f(worst)` zamiast automatycznego
      odrzucenia → losowanie z `P = exp(-Δf / T)`
      gdzie `Δf = f(x_r) - f(worst)`, `T = T_current`
- [ ] Losowanie przez `std::mt19937` z `std::uniform_real_distribution<double>(0.0, 1.0)`
- [ ] Seed konfigurowalny (dla reprodukowalności testów)
- [ ] Gdy `T = 0.0`: `P = 0` dla wszystkich Δf > 0 — degeneracja do NM

### Harmonogramy chłodzenia

- [ ] **Boltzmann:** `T_k = T_initial / ln(1 + k)` — aktualizacja po każdym kroku
- [ ] **Geometric:** `T_k = T_initial · geometric_rate^k` — aktualizacja po każdym kroku
- [ ] **Adaptive:** placeholder (zwraca Boltzmann) — do doprecyzowania po testach
- [ ] Harmonogram wybierany przez `SAConfig::schedule`; zmiana możliwa w runtime
      (efektywna od następnego kroku — bez restartu)

### API temperatury (runtime)

- [ ] `setTemperature(double T)` — modyfikuje `T_current`, NIE resetuje k ani `T_initial`
- [ ] `resetCooling()` — przywraca `T_current = T_initial` i `k = 0`
- [ ] Oba wywołania bezpieczne w trakcie działania (thread-safe w przyszłości — na razie single-thread)

### Kryteria ukończenia Etapu 3

- [ ] Przy `T = 0`: zachowanie **identyczne** jak czysty NM z Etapu 2 (deterministyczne)
- [ ] Test lokalnego minimum: funkcja `f(x) = min((x-1)², (x+1)²) - ε·noise`
      z dwiema miskami — czysty NM utyka, SA-NM z `T_initial = 5.0` ucieka
      i znajduje globalne minimum (test probabilistyczny, 9/10 uruchomień)
- [ ] `setTemperature(0.0)` podczas działania → kolejne kroki są deterministyczne
      (weryfikacja: dwa uruchomienia od tego punktu dają te same wyniki)
- [ ] Boltzmann: temperatura jest monotonicznie niemalejąca — weryfikacja przez
      log temperatury vs iteracja
- [ ] Geometric: `T_{k+1} / T_k = geometric_rate` — weryfikacja analityczna
      dla 100 kroków

---

## Etap 4 — Debug trace

**Cel:** Pełna obserwowalność każdego kroku solvera. Trace jest
fundamentem weryfikacji Fazy II (GPU) — bez niego każdy błąd GPU
staje się widoczny dopiero jako złe dopasowanie końcowe.

### Struktura TraceStep

- [ ] Wypełnianie `TraceStep` po **każdym** kroku NM/SA:
  - `type`: Reflection / Expansion / Contraction / Shrink / Restart
  - `state_before`: kopia `SimplexState` przed krokiem
  - `state_after`: kopia `SimplexState` po kroku
  - `chi2_min`: aktualne minimum
  - `T`: temperatura w tym kroku
  - `iteration`: numer kroku globalnego
- [ ] `trace_enabled = false` → solver działa normalnie, żaden TraceStep
      nie jest alokowany (brak narzutu pamięciowego i kopiowania)
- [ ] `trace_enabled = true` → każdy krok zapisywany, niezależnie od typu operacji

### API trace

- [ ] `const std::vector<TraceStep>& getTrace() const`
- [ ] `const TraceStep& getTraceStep(int i) const` — z bounds check
- [ ] `int getTraceSize() const`
- [ ] `void clearTrace()` — zwalnia pamięć, reset

### Czytelność trace

- [ ] `std::string traceStepToString(const TraceStep& step)` — czytelny
      opis kroku do wypisania w stdout lub logfile
      Format: `[iter=42 T=0.012 Shrink] chi2: 0.034 → 0.029`
- [ ] Wypisanie kilku kroków w kolejności pozwala ręcznie prześledzić,
      czy operacje są logicznie spójne

### Kryteria ukończenia Etapu 4

- [ ] Trace zawiera **dokładnie** tyle kroków ile iteracji solvera
      (weryfikacja: `getTraceSize() == iterations` po `runUntilConvergence`)
- [ ] Dla każdego kroku trace: `state_after.chi2_values[best_idx]` ≤
      `state_before.chi2_values[best_idx]` — z wyjątkiem kroków gdzie
      SA zaakceptował gorszy punkt (weryfikacja przez `step_type`)
- [ ] Typ operacji (Reflection/Expansion/Contraction/Shrink) zgadza się
      z logiką algorytmu — ręczna weryfikacja 20 kroków dla prostego przypadku
- [ ] `trace_enabled = false` → `getTraceSize() == 0` po 1000 iteracjach;
      czas wykonania nie jest wolniejszy niż z trace

---

## Etap 5 — JSON Prefit + stabilizacja API

**Cel:** Serializacja stanu parametrów. Zamrożenie i wyczyszczenie
wszystkich publicznych interfejsów. Po tym etapie API jest gotowe
do opisania w dokumentacji i przekazania do chatu Fazy II.

### exportPrefit

- [ ] `std::string exportPrefit(params, model_id)` → poprawny JSON string
- [ ] JSON zawiera: `model_id`, array `parameters` z polami
      `name`, `value`, `free`, `min`, `max`
- [ ] Wartości `double` serializowane z pełną precyzją (np. 17 cyfr znaczących)
- [ ] Kolejność parametrów w JSON = kolejność w `std::vector<FitParam>`

### importPrefit

- [ ] `std::vector<FitParam> importPrefit(json_str, expected_model_id)`
- [ ] Parametr w JSON **i** w mapie → wczytaj wartość, free, min, max
- [ ] Parametr **nie** w JSON, ale jest w mapie → zachowaj wartość domyślną
      (nie modyfikuj istniejącego FitParam)
- [ ] Parametr w JSON, **nie** w mapie → ignoruj z ostrzeżeniem do stderr
- [ ] Niezgodny `model_id` → wypisz warning do stderr, **kontynuuj** (nie throw)
- [ ] Niepoprawny JSON → rzuć `std::invalid_argument` z czytelnym komunikatem

### Stabilizacja API

- [ ] Przegląd wszystkich nagłówków — usunięcie temp nazw, spójność konwencji
      (snake_case dla zmiennych, PascalCase dla typów)
- [ ] `solver_api.h` — jeden nagłówek `#include` wystarczający do użycia
      całego solvera z zewnątrz (bez potrzeby include'owania podplików)
- [ ] Każda publiczna funkcja i struktura ma komentarz inline opisujący:
      co przyjmuje, co zwraca, jakie są edge cases (format: doc-comment)
- [ ] Brak cyclic includes; nagłówki są idempotentne (`#pragma once` lub guards)
- [ ] Kompilacja bez ostrzeżeń na `-Wall -Wextra` (GCC/Clang)

### Kryteria ukończenia Etapu 5

- [ ] Roundtrip JSON: `exportPrefit(importPrefit(exportPrefit(params, id), id), id)`
      daje identyczny string co pierwsze `exportPrefit`
- [ ] Import z brakującym parametrem → nie crashuje, brakujący parametr
      ma wartość domyślną z oryginalnego `FitParam`
- [ ] Import z obcym `model_id` → wyświetla warning, nie rzuca wyjątku,
      parametry są wczytane
- [ ] Import z niepoprawnym JSON → rzuca `std::invalid_argument`
- [ ] `solver_api.h` + jeden plik `.cpp` z `main()` używający solvera
      kompiluje się i uruchamia poprawnie bez żadnych innych include'ów projektu
- [ ] Każda publiczna funkcja ma doc-comment; gotowe do wygenerowania
      dokumentacji przez Doxygen

---

## Podsumowanie etapów

| Etap  | Główne dostarczane elementy                           | Koniec =                                |
| ----- | ----------------------------------------------------- | --------------------------------------- |
| **1** | Wszystkie struktury, LambertW, model IV, χ²           | poprawny prąd IV dla znanych parametrów |
| **2** | Solver NM (bez SA), obsługa NaN i granic, degeneracja | fit syntetycznych danych IV             |
| **3** | SA extension, harmonogramy, runtime T                 | ucieczka z lokalnego minimum            |
| **4** | Debug trace, nawigacja, pretty-print                  | pełna inspekcja każdego kroku           |
| **5** | JSON prefit, stabilizacja API, dokumentacja inline    | handoff do Fazy II (GPU)                |