# Projekt GUI — SA-NM IV Fitting Tool
## Specyfikacja interfejsu użytkownika (Dear ImGui + ImPlot)

> **Środowisko:** Dear ImGui z dockingiem, ImPlot dla wykresów, C++17.
> **Cel:** Eksploracja działania solvera, generacja/fitowanie
> charakterystyk IV, analiza batchowa.

---

## 1. Filosofia układu

### Trzy tryby pracy — jeden interfejs

Użytkownik przełącza się między trzema trybami przez zakładki górnego
paska. Każdy tryb ma własny układ paneli w dockspace, ale panele można
swobodnie odłączać i przearanżować.

```
[Menu Bar]  [● Single Fit]  [  Batch Fitting  ]  [  Simplex Inspector  ]
+--------------------------------------------------------------------+
|                      DockSpace                                     |
+--------------------------------------------------------------------+
```

### Domyślny układ dla każdego trybu

```
SINGLE FIT (domyślny):
┌──────────────┬──────────────────────────────┬─────────────────────┐
│  Controls    │                              │  Results            │
│  [290px]     │    IV Curve (ImPlot)         │  [280px]            │
│              │                              │                     │
│  ▸ Model     │                              │  ▸ Fit Results      │
│  ▸ Noise     │                              │  ▸ Trace            │
│  ▸ Solver    │                              │                     │
│              │                              │                     │
└──────────────┴──────────────────────────────┴─────────────────────┘
│  Log                                                    [100px]   │
└────────────────────────────────────────────────────────────────────┘

BATCH FITTING:
┌──────────────┬──────────────────────────────┬─────────────────────┐
│  Batch       │  Batch Results               │  Statistics         │
│  Config      │  (scrollable table)          │  (histogramy)       │
│  [290px]     │                              │  [280px]            │
└──────────────┴──────────────────────────────┴─────────────────────┘
│  Log                                                               │
└────────────────────────────────────────────────────────────────────┘

SIMPLEX INSPECTOR:
┌──────────────┬──────────────────┬───────────┬─────────────────────┐
│  Controls    │  IV Curve        │  Simplex  │  Trace              │
│  (skrócone)  │                  │  2D View  │  (szczegółowy)      │
└──────────────┴──────────────────┴───────────┴─────────────────────┘
│  Log                                                               │
└────────────────────────────────────────────────────────────────────┘
```

---

## 2. Panel: Controls (lewy, ~290px)

Jeden panel z trzema zakładkami. Widoczny we wszystkich trybach.

---

### 2.1 Zakładka `Model`

```
┌─ Model ─────────────────────────────────────┐
│ Model:  [○ 4-param]  [● 6-param]            │
│ Temperatura T: [300.0] K                    │
│                                             │
│ Napięcie: od [-0.50] do [0.70] V            │
│ Punktów:  [100]                             │
│                                             │
│ ─────────── Parametry ─────────────────     │
│                     Val    Min    Max  Free │
│  I₀  [A]    [1e-10] [1e-15][1e-5] [✓]      │
│  A   [-]    [1.50]  [0.50] [3.00] [✓]      │
│  Rₛ  [Ω]   [0.10]  [0.00] [10.0] [✓]      │
│  Rₛₕ [Ω]  [1000.]  [10.0] [1e6]  [✓]      │
│  ─── tylko 6-param ────────────────         │
│  α   [-]    [1.50]  [0.50] [3.00] [✓]      │
│  Rₛₕ₂[Ω] [500.0]   [1.0]  [1e5]  [✓]      │
│                                             │
│        [Generate IV Curve]                  │
└─────────────────────────────────────────────┘
```

**Szczegóły implementacji:**

- `I₀` i `Rₛₕ` — pole tekstowe z notacją naukową (`%.2e`). Suwak
  logarytmiczny opcjonalnie (przełącznik obok). `ImGui::InputDouble`
  z formatem `"%.3e"`.
- `A`, `Rₛ`, `α`, `Rₛₕ₂` — `ImGui::SliderDouble` + `ImGui::InputDouble`
  obok siebie (SameLine). Suwak daje szybki ruch, input precyzję.
- Pole `Free [✓]` — `ImGui::Checkbox`. Gdy `free=false`, rząd jest
  wyszarzony (`ImGui::BeginDisabled()`).
- `Min`/`Max` to zwykłe `ImGui::InputDouble` w wąskich kolumnach,
  ukryte za przyciskiem `[⚙]` (rozwijane inline) jeśli potrzeba
  zaoszczędzić miejsca.
- `[Generate IV Curve]` — oblicza `evaluateDiodeIV4/6` dla całej
  siatki V, aktualizuje dane do wykresu.

---

### 2.2 Zakładka `Noise`

```
┌─ Noise ─────────────────────────────────────┐
│                                             │
│ Szum Gaussowski: σ = [1e-06] A              │
│ Seed (RNG):          [42   ]                │
│                                             │
│ [○ Absolutny σ]  [● Względny σ/|I_max|]    │
│                                             │
│           [Add Noise to Data]               │
│                                             │
│ ─────────── Podgląd ────────────────────    │
│ SNR:      ~86 dB                            │
│ I_max:    4.73e-03 A                        │
│ σ_abs:    1.00e-06 A                        │
└─────────────────────────────────────────────┘
```

**Szczegóły:**

- Po `[Add Noise]`: każdy punkt `I_noisy[i] = I_model[i] + N(0,σ)`.
  RNG to osobny `std::mt19937` od solvera — seed niezależny.
- Podgląd SNR aktualizuje się na żywo przy zmianie σ.
- Przełącznik absolutny/względny: gdy względny, σ = (% wartości) ×
  `max(|I_model|)`. Przydatne gdy I rozciąga się przez kilka rzędów.
- `[Add Noise]` jest aktywny tylko gdy `I_model` jest wygenerowane
  (szara gdy brak danych).

---

### 2.3 Zakładka `Solver`

```
┌─ Solver ────────────────────────────────────┐
│ ─── Nelder–Mead ────────────────────────    │
│ α (reflection): [1.00]  γ (expansion): [2.00]│
│ ρ (contraction):[0.50]  σ (shrink):    [0.50]│
│ Degeneracy tol: [1e-12]                     │
│                                             │
│ ─── Simulated Annealing ────────────────    │
│ [ ] Włącz SA                               │
│   T_initial:     [5.00  ]                   │
│   Harmonogram: [Geometric ▼]               │
│   Geom. rate:    [0.995  ]                  │
│   RNG seed:      [0      ]                  │
│                                             │
│ ─── Trace ──────────────────────────────    │
│ [ ] Włącz trace (wolniejsze!)               │
│     Maks. kroków w trace: [10000]           │
│                                             │
│ ─── Kryteria zatrzymania ───────────────    │
│ Max iteracji:  [5000  ]                     │
│ Chi2 tol:      [1e-18 ]                     │
│                                             │
│  [Run Fit]  [Step x1]  [Step x10]  [Stop]  │
│                                             │
│ Stan: ● Idle  / ⏳ Running / ✓ Converged   │
│ Iteracja: 243 / 5000    chi2: 3.41e-04      │
└─────────────────────────────────────────────┘
```

**Szczegóły:**

- Gdy `SA off`: sekcja SA jest `BeginDisabled()` — widoczna ale wyszarzona.
- `Harmonogram`: `ImGui::Combo` z pozycjami {Boltzmann, Geometric, Adaptive}.
  Gdy Boltzmann: pole `Geom. rate` wyszarzone.
- `[Run Fit]` — uruchamia `runUntilConvergence`. Staje się `[Stop]`
  w trakcie działania. W przyszłości można wrzucić do worker thread,
  ale na razie synchronicznie (UI blokuje się na czas fitu).
- `[Step x1]`/`[Step x10]` — aktywne tylko gdy trace włączony i solver
  jest zainicjalizowany (state != Idle).
- Wskaźnik stanu `●/⏳/✓` w trzech kolorach (szary/żółty/zielony).

---

## 3. Panel: IV Curve (środkowy, główny)

```
┌─ IV Curve ──────────────────────────────────────────────────────────┐
│  [Lin Y] [Log Y]  [Autoscale]  [Export PNG]          Chi2: 3.41e-04│
│                                                                     │
│  I [A]                                                              │
│  ^                                                              *   │
│  |                                                         **       │
│  |                                                    **            │
│  |  . . . . . . . . . . . . . . . . . . . . . .***                 │
│  |. . . . .  . . . . . . . . . .**                                  │
│  |. . . . . . . . . . .***                                          │
│  |---**--------------------------------------------> V [V]         │
│  |  **                                                              │
│  | *                                                                │
│                                                                     │
│  ──── Legenda ─────────────────────────────────────                 │
│  ── Krzywa modelu (true)    · · Dane z szumem    -- Dopasowanie    │
└─────────────────────────────────────────────────────────────────────┘
```

**Szczegóły ImPlot:**

```cpp
ImPlot::BeginPlot("IV Characteristic", "V [V]", "I [A]", ...);

// Dane z szumem — scatter
ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 3.0f, ImVec4(0.5,0.5,0.5,0.6));
ImPlot::PlotScatter("Dane (z szumem)", V.data(), I_noisy.data(), N);

// Krzywa modelu (true)
ImPlot::SetNextLineStyle(ImVec4(0.2f, 0.5f, 1.0f, 1.0f), 2.0f);
ImPlot::PlotLine("Model (true)", V.data(), I_model.data(), N);

// Krzywa dopasowana
ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.3f, 0.2f, 1.0f), 1.5f, ImPlotLineFlags_None);
ImPlot::PlotLine("Dopasowanie", V.data(), I_fitted.data(), N);

ImPlot::EndPlot();
```

- Przełącznik `[Lin Y]`/`[Log Y]` — `ImPlot::SetupAxisScale`.
  Log Y wymaga filtrowania `I<=0` (bezwzględna wartość + ostrzeżenie).
- `[Export PNG]` — `ImPlot::SavePlot` lub ręczny `stbi_write_png`
  z frame buffer (do implementacji w Etapie 5 lub GUI).
- W trybie **Simplex Inspector**: na wykresie pojawia się pionowa linia
  przerywana "Aktualny krok trace: iter=42".

---

## 4. Panel: Results (prawy, ~280px)

Dwie zakładki: `Fit Results` i `Trace`.

---

### 4.1 Zakładka `Fit Results`

```
┌─ Fit Results ───────────────────────────────┐
│ Status: ✓ Zbieżny                           │
│ Stop reason: tol                            │
│ Iteracje: 234 / 5000                        │
│ Chi²:     3.41e-04                          │
│                                             │
│ ─── Parametry ─────────────────────────     │
│         True       Fitted    Rel. err        │
│ I₀    1.20e-10   1.19e-10   0.83%           │
│ A      1.450      1.452      0.14%           │
│ Rₛ     0.080      0.079      1.25%           │
│ Rₛₕ  1800.0     1803.4      0.19%           │
│                                             │
│ SA akceptacji: 17                           │
│                                             │
│ [Copy as CSV]  [Copy as JSON]               │
└─────────────────────────────────────────────┘
```

**Szczegóły:**

- Kolory rel. error: zielony `<1%`, żółty `1–5%`, czerwony `>5%`.
- `Status` z ikoną emoji (wystarczy ASCII): `✓ Zbieżny` / `✗ Max iter`.
- "True" kolumna widoczna tylko gdy dane są syntetyczne (generowane przez
  nas) — gdy dane zewnętrzne, kolumna znika i zostaje tylko `Fitted`.
- "SA akceptacji" widoczne tylko gdy SA włączone.
- `[Copy as CSV]` — wypisuje do schowka: `name,true,fitted,rel_err\n...`.

---

### 4.2 Zakładka `Trace`

```
┌─ Trace ─────────────────────────────────────┐
│ Kroki: 234 w buforze                        │
│                                             │
│  Krok: [  42  ] / 234    [|<] [<] [>] [>|]│
│                                             │
│  Typ:     Reflection                        │
│  chi2:    3.41e-04  →  3.28e-04  (▼ 3.8%)  │
│  T:       0.0127                            │
│  SA:      nie                               │
│                                             │
│ ─── Simpleks PRZED ────────────────────     │
│  #   chi2       role   I₀        A          │
│  0   3.41e-04   best   1.19e-10  1.452      │
│  1   3.89e-04          1.21e-10  1.451      │
│  2   5.23e-04   worst  1.18e-10  1.460      │
│                                             │
│ ─── Simpleks PO ───────────────────────     │
│  #   chi2       role   I₀        A          │
│  0   3.41e-04   best   1.19e-10  1.452      │
│  1   3.89e-04          1.21e-10  1.451      │
│  2   3.28e-04          1.19e-10  1.455      │
│                                             │
│ [Clear Trace]   [Export Trace TXT]          │
└─────────────────────────────────────────────┘
```

**Szczegóły:**

- Nawigacja `[|<][<][>][>|]` + `ImGui::InputInt` dla skoku do kroków.
  Klawisze ← → działają gdy panel ma focus.
- Zmiana procentowa chi2: `▼ X%` (zielony gdy maleje), `▲ X%`
  (czerwony gdy rośnie — może się zdarzyć przez SA).
- Tabele "PRZED"/"PO" — wiersze z role `best`/`worst` podświetlone
  (zielony/czerwony tło wiersza).
- Tylko wolne parametry w tabeli — ustalona na podstawie `FitParam::free`.
- `[Export Trace TXT]` — zapis całego trace przez `traceStepToString`
  do pliku (file dialog lub hardcoded path `trace_output.txt`).

---

## 5. Tryb: Simplex Inspector — dodatkowe okno `Simplex 2D`

```
┌─ Simplex 2D View ──────────────────────────────────────────────────┐
│  Oś X: [I₀ ▼]   Oś Y: [A ▼]   [○ Lin] [● Log X]                 │
│                                                                    │
│  A                                                                 │
│  ^                                                                 │
│ 1.46|              ★ (best)                                        │
│     |             /|\ (N_free=4 → triangle w 2D)                  │
│ 1.45|            / | \                                             │
│     |           ×  |  · (worst=×, other=·)                        │
│ 1.44|          /   |   \                                           │
│     +-------------------------------→ I₀                          │
│      1.18e-10  1.19e-10  1.20e-10                                  │
│                                                                    │
│  Trajektoria best: ────────── (ostatnie 50 kroków)                 │
│                                                                    │
│  [▶ Play] [⏸] [■ Stop]   Prędkość: [████░] 5 kroków/s             │
│  Krok: 42 / 234                                                    │
└────────────────────────────────────────────────────────────────────┘
```

**Szczegóły:**

- `ImPlot::PlotScatter` dla wierzchołków simpleksu:
  - best: żółta gwiazda `ImPlotMarker_Cross` (lub `★` via text)
  - worst: czerwone X `ImPlotMarker_X`
  - pozostałe: szare kółka
- `ImPlot::PlotLine` dla krawędzi simpleksu (wszystkie pary wierzchołków).
- `ImPlot::PlotLine("Trajektoria", ...)` — scatter historycznych pozycji
  `best` z ostatnich N kroków, alpha malejąca (najstarsze blakną).
- Oś X może być logarytmiczna (przydatne dla `I₀` — kilka rzędów).
- Animacja: timer w `guiRender` inkrementuje indeks trace co
  `1/speed` sekund (przy `ImGui::GetIO().DeltaTime`).
- Gdy aktywna animacja, `Trace` panel też się synchronizuje (shared
  `trace_current_idx` w `AppState`).

---

## 6. Tryb: Batch Fitting

### 6.1 Panel: Batch Config (lewy)

```
┌─ Batch Configuration ────────────────────────┐
│ N krzywych: [100  ]                          │
│ Seed (RNG): [12345]                          │
│ Model:      [○ 4-param] [● 6-param]          │
│                                              │
│ ─── Zakresy parametrów (losowanie uniform) ─ │
│             Min         Max        Skala     │
│ I₀ [A]    [1e-12]     [1e-7]   [● Log]      │
│ A  [-]    [0.80 ]     [2.50]   [○ Log]      │
│ Rₛ [Ω]   [0.01 ]     [5.00]   [○ Log]      │
│ Rₛₕ[Ω]   [100. ]     [1e5 ]   [● Log]      │
│ T  [K]    [280. ]     [350.]   [○ Log]      │
│                                              │
│ ─── Szum ──────────────────────────────────  │
│ σ: [1e-7] A  [○ Abs] [● Rel σ/|I_max|]      │
│                                              │
│ ─── Solver ─────────────────────────────     │
│ [✓] NM (bez SA)                              │
│ [✓] SA-NM  T_init: [5.0] Rate: [0.995]      │
│     Porównuj oba na tych samych danych!      │
│                                              │
│ Max iter / krzywa: [3000]                    │
│ Chi2 tol:          [1e-18]                   │
│                                              │
│          [▶ Run Batch]                       │
│                                              │
│ ████████████░░░░░░ 67/100 krzywych           │
│ ETA: ~12s   Elapsed: 8.3s                    │
└──────────────────────────────────────────────┘
```

**Szczegóły:**

- Zakresy z przełącznikiem Lin/Log — gdy Log, losowanie w przestrzeni
  `log10`, potem `10^x`.
- Dwa checkboxy `NM` i `SA-NM` — gdy oba zaznaczone, każda krzywa jest
  fitowana **dwa razy** (na tych samych danych), co pozwala na bezpośrednie
  porównanie.
- Progress bar z `ImGui::ProgressBar`. ETA wyliczana z mean time/curve.
- `[▶ Run Batch]` → `[■ Stop]` w trakcie.
- Batch **powinien** działać asynchronicznie (worker thread lub chunked
  per-frame). Implementacja: wystarczy `std::thread` + `std::atomic<int>`
  licznik postępu; GUI tylko odczytuje licznik.

### 6.2 Panel: Batch Results (środkowy)

```
┌─ Batch Results ─────────────────────────────────────────────────────┐
│ [Tabela] [Wykresy] [Statystyki]                                     │
│                                                                     │
│ TABELA (scrollable):                                                │
│  #   I₀_true  I₀_nm   I₀_err  A_true  A_nm   A_err  chi2_nm  conv │
│  0   1.24e-10  1.25e-10  0.8%  1.45  1.453   0.2%  2.3e-04   ✓   │
│  1   3.71e-11  3.69e-11  0.5%  1.89  1.894   0.2%  8.1e-05   ✓   │
│  2   8.90e-12  fail       —    1.23   fail     —    inf       ✗   │
│  ...                                                               │
│                                                                     │
│ Gdy oba solvery: kolumny _nm i _sa zdublowane                       │
│ Kolory:  zielony <1%, żółty 1-5%, czerwony >5%                      │
│                                                                     │
│ [Export CSV]   [Export JSON]   [Show only failed ✗]                │
└─────────────────────────────────────────────────────────────────────┘
```

### 6.3 Panel: Batch Statistics (prawy)

```
┌─ Statistics ───────────────────────────────────────┐
│ N razem: 100    Zbieżnych NM: 92    SA-NM: 97      │
│                                                    │
│ ─── Odtworzenie parametrów (median |rel. err|) ─── │
│       NM         SA-NM                             │
│ I₀:  0.73%       0.61%      (SA lepsze ✓)          │
│ A:   0.12%       0.11%      (podobne)               │
│ Rₛ:  1.84%       1.72%      (SA lepsze ✓)          │
│ Rₛₕ: 0.43%       0.39%      (SA lepsze ✓)          │
│                                                    │
│ ─── Rozkład chi2 (ImPlot histogram) ──────────── ─ │
│  [histogram: chi2_nm (niebieski) vs chi2_sa (red)] │
│                                                    │
│ ─── Rozrzut: I₀_true vs I₀_fitted ─────────────── │
│  [scatter plot, oś x=true, y=fitted, linia y=x]   │
│  Oś: [I₀ ▼]  Solver: [NM ▼]                       │
│                                                    │
│ [Export Stats TXT]                                 │
└────────────────────────────────────────────────────┘
```

---

## 7. Panel: Log (dolny, ~100px)

```
┌─ Log ─────────────────────────────────────────────────────── [Clear]┐
│ [10:23:44] INFO  IV curve generated: 100 points, V ∈ [-0.50, 0.70] │
│ [10:23:51] INFO  Noise added: sigma=1e-06 A (seed=42)              │
│ [10:23:55] INFO  Fit started: NM, sa_enabled=false, max_iter=5000  │
│ [10:23:55] INFO  Fit converged: iter=234, chi2=3.41e-04            │
│ [10:24:02] WARN  Trace buffer approaching limit (9800/10000 steps)  │
│ [10:24:15] ERROR Batch row 2: fit failed (all vertices invalid)     │
└─────────────────────────────────────────────────────────────────────┘
```

**Szczegóły:**

- `ImGui::BeginChild` z auto-scroll: `ImGui::SetScrollHereY(1.0f)`
  gdy nowe linie.
- Kolory: INFO = white, WARN = `ImVec4(1,0.85,0,1)`, ERROR = `ImVec4(1,0.3,0.2,1)`.
- `[Clear]` po prawej stronie tytułu (`ImGui::SameLine` + `ImGui::SetCursorPosX`).
- Bufor ring: max 500 linii, starsze usuwane.

---

## 8. `AppState` — struktura danych GUI

```cpp
// src/app.hpp

#pragma once
#include "solver/solver_types.hpp"
#include "solver/nelder_mead.hpp"
#include "solver/diode_model.hpp"
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <thread>

// ─── Log ─────────────────────────────────────────────────────────────────────

enum class LogLevel { Info, Warn, Error };
struct LogEntry {
    std::string  text;
    LogLevel     level;
    // timestamp pomijamy — ImGui nie ma wbudowanego czasu; użyj std::chrono
};

// ─── Dane krzywej IV ──────────────────────────────────────────────────────────

struct IVData {
    std::vector<double> V;        // napięcia [V]
    std::vector<double> I_model;  // krzywa bez szumu (true)
    std::vector<double> I_noisy;  // z szumem Gaussowskim
    std::vector<double> I_fitted; // wynik dopasowania
    bool has_model  = false;
    bool has_noisy  = false;
    bool has_fitted = false;
};

// ─── Konfiguracja modelu ──────────────────────────────────────────────────────

struct ModelConfig {
    bool use_6param = false;
    double T = 300.0;         // temperatura [K]
    double V_min = -0.5;
    double V_max =  0.7;
    int    N_points = 100;
    std::vector<FitParam> params;   // len=4 lub 6, zawsze pełna lista

    static ModelConfig default4param();
    static ModelConfig default6param();
};

// ─── Konfiguracja solvera ─────────────────────────────────────────────────────

struct SolverConfig {
    double nm_alpha = 1.0, nm_gamma = 2.0, nm_rho = 0.5, nm_sigma = 0.5;
    double degenerate_tol = 1e-12;
    bool   sa_enabled = false;
    SAConfig sa_config;
    unsigned int rng_seed = 0;
    bool   trace_enabled = false;
    int    max_iter = 5000;
    double chi2_tol = 1e-18;
};

// ─── Stan solvera (Single Fit) ────────────────────────────────────────────────

enum class SolverState { Idle, Ready, Running, Converged, MaxIter, Failed };

struct SingleFitState {
    SolverState              state = SolverState::Idle;
    FitResult                last_result;
    int                      trace_current_idx = 0;
    bool                     trace_play = false;
    float                    trace_play_speed = 5.0f;  // kroków/s

    // Solver przechowywany między krokami (tryb step-by-step)
    std::unique_ptr<SANelderMead> solver;
    std::vector<TraceStep>        snapshot_trace; // kopia dla inspektora
};

// ─── Noise config ─────────────────────────────────────────────────────────────

struct NoiseConfig {
    double        sigma = 1e-6;
    unsigned int  seed = 42;
    bool          relative = false;   // true = sigma jako ułamek I_max
};

// ─── Batch fitting ────────────────────────────────────────────────────────────

struct BatchParamRange {
    double min, max;
    bool log_scale;
};

struct BatchRow {
    std::vector<double> true_params;   // len=4 lub 6
    double T_true;

    // Wyniki NM (jeśli uruchomiony)
    bool nm_converged = false;
    FitResult nm_result;

    // Wyniki SA-NM (jeśli uruchomiony)
    bool sa_converged = false;
    FitResult sa_result;
};

struct BatchConfig {
    int N = 100;
    unsigned int rng_seed = 12345;
    bool use_6param = false;
    std::vector<BatchParamRange> param_ranges;  // len=4 lub 6 + 1 (T)
    NoiseConfig noise;
    bool run_nm = true;
    bool run_sa = true;
    SAConfig sa_config;
    int max_iter = 3000;
    double chi2_tol = 1e-18;
};

struct BatchState {
    std::vector<BatchRow>   results;
    std::atomic<int>        progress{0};
    int                     total = 0;
    bool                    running = false;
    std::thread             worker;
    double                  elapsed_s = 0.0;
};

// ─── AppState ─────────────────────────────────────────────────────────────────

struct AppState {
    // ── Tryb aplikacji ──────────────────────────────────────────────────────
    int active_tab = 0;   // 0=SingleFit, 1=BatchFit, 2=SimplexInspector

    // ── Dane i konfiguracja ─────────────────────────────────────────────────
    ModelConfig   model;
    NoiseConfig   noise;
    SolverConfig  solver_cfg;
    IVData        iv_data;
    SingleFitState fit_state;
    BatchConfig   batch_cfg;
    BatchState    batch_state;

    // ── Log ─────────────────────────────────────────────────────────────────
    std::vector<LogEntry> log_entries;
    void log(LogLevel lvl, const std::string& msg);
    bool log_auto_scroll = true;

    // ── UI state (nie serializable) ─────────────────────────────────────────
    bool show_simplex_2d = true;
    int  simplex_axis_x = 0;   // indeks wolnego parametru dla osi X 2D view
    int  simplex_axis_y = 1;   // indeks wolnego parametru dla osi Y 2D view
};

void appInit(AppState& state);
```

---

## 9. Podział kodu GUI — `guiRender` i sub-funkcje

```
gui.cpp / gui.hpp
  guiRender(AppState& s)
    ├─ guiMenuBar(s)
    ├─ guiModeTabs(s)              ← przełącznik 3 trybów
    ├─ guiPanelControls(s)         ← lewy panel, wspólny
    │    ├─ guiTabModel(s)
    │    ├─ guiTabNoise(s)
    │    └─ guiTabSolver(s)
    ├─ guiPanelIVCurve(s)          ← środkowy panel, ImPlot
    ├─ guiPanelResults(s)          ← prawy panel
    │    ├─ guiTabFitResults(s)
    │    └─ guiTabTrace(s)
    ├─ guiPanelSimplex2D(s)        ← tylko Simplex Inspector mode
    ├─ guiPanelBatchConfig(s)      ← tylko Batch mode
    ├─ guiPanelBatchResults(s)     ← tylko Batch mode
    ├─ guiPanelBatchStats(s)       ← tylko Batch mode
    └─ guiPanelLog(s)              ← dolny pasek, zawsze widoczny
```

Każda `guiPanel*` / `guiTab*` to osobna `static void` w `gui.cpp`.
Żadna z nich nie ma własnego stanu — cały stan w `AppState`.

---

## 10. Kolejność implementacji GUI

Każdy krok kończy się kompilującą i działającą aplikacją.

```
Krok 1 — Fundament (2h):
  AppState + ModelConfig + default params
  guiTabModel: pola parametrów + [Generate IV Curve]
  guiPanelIVCurve: pusty ImPlot (tylko osie)
  Weryfikacja: okno startuje, parametry można edytować

Krok 2 — Generacja i szum (1.5h):
  logika Generate → I_model → IVData::has_model = true
  guiPanelIVCurve: rysowanie I_model (niebieska linia)
  guiTabNoise: sigma, [Add Noise] → I_noisy
  guiPanelIVCurve: scatter I_noisy (szare kropki)

Krok 3 — Fit i wyniki (2h):
  guiTabSolver: pola konfiguracji NM + SA
  [Run Fit]: makeDiodeIVObjective → SANelderMead → runUntilConvergence
  guiPanelIVCurve: rysowanie I_fitted (czerwona linia)
  guiTabFitResults: tabela parametrów, chi2, status

Krok 4 — Trace i inspektor (2h):
  guiTabTrace: nawigacja [|<][<][>][>|], tabele before/after
  guiPanelSimplex2D: scatter wierzchołki + krawędzie simpleksu
  [Step x1] / [Step x10] w Solver tab
  Animacja play/pause w Simplex 2D

Krok 5 — Batch (2.5h):
  guiPanelBatchConfig: zakresy parametrów, progress bar
  worker thread (std::thread + std::atomic<int>)
  guiPanelBatchResults: tabela wyników z kolorami
  guiPanelBatchStats: statystyki + histogramy ImPlot

Krok 6 — Polishing (1h):
  Log panel: kolorowe wpisy, auto-scroll
  Export CSV / TXT
  Przełącznik Lin/Log na osi Y wykresu
  Wyszarzanie (BeginDisabled) nieaktywnych sekcji

Łącznie: ~11h netto
```

---

## 11. Kluczowe pułapki implementacyjne GUI

| Problem                                                              | Rozwiązanie                                                                                                                                                                              |
| -------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `ImGui::SliderDouble` nie istnieje w ImGui                           | użyj `ImGui::SliderScalar` z `ImGuiDataType_Double` lub `float` cast                                                                                                                     |
| `I₀` w suwaku: zakres kilka rzędów                                   | `ImGui::SliderScalar` z `ImGuiSliderFlags_Logarithmic`                                                                                                                                   |
| ImPlot crash gdy `I_fitted` pusta (`has_fitted=false`)               | guard `if (iv_data.has_fitted) ImPlot::PlotLine(...)`                                                                                                                                    |
| Log auto-scroll "przeskakuje" gdy użytkownik scrolluje ręcznie       | sprawdzaj `ImGui::GetScrollY() >= ImGui::GetScrollMaxY()` przed `SetScrollHereY`                                                                                                         |
| Batch worker thread i GUI w tym samym czasie czytają `BatchState`    | `std::atomic<int>` dla licznika; `results` blokowane `std::mutex` tylko przy zapisie/odczycie po zakończeniu; nigdy nie czytaj `results` z GUI w trakcie worker (tylko progress counter) |
| Simplex 2D: N_free=4, projekcja na 2 osie — pozostałe 2 "zawieszone" | wyświetlaj dane z `state_after.vertices[i][axis_x]` i `[axis_y]`, pozostałe osie ignorowane w tym widoku                                                                                 |
| `trace_play` + `step()` synchronicznie blokuje GUI                   | `[Step x1]` jest OK synchronicznie; `[▶ Play]` też (przy 5 kroków/s `DeltaTime` to ~200ms, akceptowalne). Nie potrzeba wątku.                                                            |
| Skala log Y dla ujemnych I                                           | w trybie log Y: `abs(I)` + ostrzeżenie w log, albo pomijanie punktów `I<=0`                                                                                                              |