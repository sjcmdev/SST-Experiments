# Solver Optymalizacyjny w GPU-Experiment — Opis Problemu i Projekt

> **Dokument problemowy + decyzje projektowe.**  
> Sekcje oznaczone 🔧 zawierają konkretny projekt do implementacji.  
> Sekcje oznaczone ❓ zawierają pytania otwarte.

---

## Aktualna architektura repozytorium

```
gpu-experiment/
├── kernels/convolution.cu        ← kernel NVRTC (tekst źródłowy)
├── src/
│   ├── app.h / app.cpp           ← AppState, dirty flag, pollAndSubmit
│   ├── gui.h / gui.cpp           ← ImGui / ImPlot / imnodes
│   ├── cuda_interface.h          ← wspólne typy (KernelHandle, GpuTask, …)
│   ├── kernel_manager.h/.cpp     ← NVRTC + Driver API: kompilacja, CUmodule
│   ├── gpu_worker.h/.cpp         ← asynchroniczny wątek GPU, kolejka, future
│   ├── signal_graph.h/.cpp       ← NodeGraph, SignalNode, CPU ewaluator
│   ├── code_gen.h/.cpp           ← generator kodu CUDA z grafu
│   └── cuda/cuda_impl.cu         ← runConvolution, runPipeline (NVCC)
└── vendor/ imgui / implot / imnodes
```

### Przepływ danych (aktualny eksperyment: splot)

```
UI (imnodes) → NodeGraph (codeDirty=true)
  → code_gen: buildSignalModule()    ← generuje string CUDA C++
  → KernelManager::compile()         ← NVRTC → PTX → CUfunction
  → GpuWorkerThread::submitTask()
  → runPipeline() [GPU]              ← genA → genB → conv → D2H readback
  → promise/future → ImPlot
```

### Co można ponownie wykorzystać

| Komponent                      | Potencjalne użycie w solverze                          |
| ------------------------------ | ------------------------------------------------------ |
| `KernelManager`                | kompilacja kernela modelu IV (raz per struktura grafu) |
| `GpuWorkerThread`              | async batch dopasowań                                  |
| `NodeGraph` + `evaluateSignal` | CPU-side model (debug, referencja)                     |
| `code_gen`                     | rozszerzenie o parametry runtime (`double* params`)    |
| ImPlot                         | wykresy $\chi^2$, parametrów, temperatury SA                 |

---

## Algorytm: SA-Enhanced Nelder–Mead

### Architektura: jeden algorytm, nie dwa

Simulated Annealing nie jest osobnym solverem. SA działa wewnątrz każdego
kroku Nelder–Mead jako **modyfikacja kryterium akceptacji**:

```
Standardowy NM:
  f(reflected) < f(worst) → akceptuj zawsze
  f(reflected) ≥ f(worst) → odrzuć, rób contraction/shrink

SA-Enhanced NM:
  f(reflected) < f(best)  → ekspansja (jak NM)
  f(reflected) < f(worst) → akceptuj zawsze (jak NM)
  f(reflected) ≥ f(worst) → akceptuj z P = exp(-Δf / T)    ← SA
                             gdzie Δf = f(reflected) - f(worst)
                             jeśli odrzucono → contraction/shrink (jak NM)
```

Gdy T → 0, algorytm degeneruje się do standardowego Nelder–Mead.
Gdy T jest duże, algorytm eksploruje szerszy obszar przestrzeni parametrów.

### Operacje simpleksu (niezmienione)

- **Reflection** — odbij najgorszy wierzchołek przez centroid pozostałych
- **Expansion** — idź dalej w kierunku odbicia (jeśli to poprawia wynik)
- **Contraction** — cofnij się w stronę centroidu
- **Shrink** — ściągnij wszystkie wierzchołki w kierunku najlepszego

### Dane stanu algorytmu

- N+1 wierzchołków (macierz parametrów)
- Wartości $\chi^2$ dla każdego wierzchołka
- Najlepszy / najgorszy wierzchołek, centroid
- Typ aktualnego kroku
- Aktualna temperatura T, liczba iteracji
- Historia (dla CPU debug trace)

---

## Temperatura T — parametr runtime kontrolowany przez użytkownika

> 🔧 **Decyzja projektowa:** T jest zmienną wystawioną do modyfikacji
> przez użytkownika **w trakcie działania algorytmu**.

### Harmonogram chłodzenia

Domyślny: **Boltzmann**

```
T_k = T_0 / ln(1 + k)
```

gdzie k = numer iteracji, T_0 = temperatura startowa.

Harmonogram musi być łatwo wymienialny. 🔧 Projekt:

```cpp
enum class CoolingSchedule { Boltzmann, Geometric, Adaptive };

struct SAConfig {
    double T_initial;       // ekspozycja w UI, modyfikowalna przed startem
    double T_current;       // wyświetlana w UI, modyfikowalna w trakcie
    CoolingSchedule schedule = CoolingSchedule::Boltzmann;
    double geometric_rate;  // α dla T_k = T_0 * α^k (jeśli Geometric)
};
```

### Interakcja użytkownika z temperaturą

Użytkownik może w trakcie działania algorytmu:
- **obniżyć T** — wymusić zbieżność lokalną (przejście do trybu NM),
- **podnieść T** — "rozgrzać" (reheat) — uciec z lokalnego minimum,
- **zerować T** — natychmiastowy przełącznik w tryb czystego NM.

T_current jest wyświetlane w UI i edytowalne jako pole liczbowe / suwak.

---

## Funkcja celu: $\chi^2$ i $\Delta \chi^2$

### $\chi^2$ (chi-kwadrat)

$$
$\chi^2$(p) = Σ [(I_meas(V_i) - I_model(V_i, p))² / σ_i²]
$$


Minimalizacja $\chi^2$ = dopasowanie modelu IV do danych pomiarowych.

### $\Delta \chi^2$ (delta chi-kwadrat)

$$
\Delta \chi^2(p) = \chi^2(p) - \chi^2_{min}
$$

Zastosowania:
- **Przedziały ufności** — profil $\Delta \chi^2$(p_k) daje 1σ przy $\Delta \chi^2$=1, 2σ przy $\Delta \chi^2$=4
- **Mapa akceptowalnych dopasowań** — regiony $\Delta \chi^2$ < progu
- **Kryterium zatrzymania** — solver zatrzymuje się gdy $\Delta \chi^2$ < ε

Oba tryby muszą być dostępne; wybór przez użytkownika w UI.

---

## Parametry: skale i reprezentacja

### Skala liniowa na wejściu solvera

> 🔧 **Decyzja:** Simplex i SA operują wyłącznie na wartościach liniowych.

Transformacja log/exp jest **wewnątrz modelu** (wewnątrz funkcji liczącej
prąd IV), nie w solverze. Parametr `x` trafia do solvera jako wartość
liniowa. Jeśli dany parametr fizycznie jest logarytmiczny (np. prąd
nasycenia $I_0$ rozpiętý na wiele rzędów), węzeł w grafie przyjmuje go
w skali liniowej i aplikuje `exp()` wewnętrznie.

**Konsekwencja:** solver nie potrzebuje wiedzieć o log-scale. Granice
`[min, max]` są podawane w skali liniowej, przez użytkownika w skali logarytmicznej, wewnątrz są zamieniane na liniową. Perturbacje SA i operacje simpleksu są jednorodne.

### Typy parametrów

```cpp
struct FitParam {
    std::string name;   // mój komentarz: czy nie lepiej zdefiniować to jako int i potem po stronie aplikacji parsować to przez mapa: <int, string> na faktyczny parametr? 
    double value;       // aktualna wartość (liniowa)
    double min, max;    // granice (liniowe)
    bool free;          // true = optymalizowany, false = stały
};
```

Solver widzi tylko parametry `free`. Parametry `fixed` nie wchodzą do
wektora optymalizacji — są stałymi przekazywanymi do modelu.

---

## 🔧 Projekt: przekazywanie parametrów do GPU (bez rekompilacji)

Kluczowa zmiana architektury względem Faz 4–5: parametry NIE są literałami
w generowanym kodzie CUDA.

### ParameterMap — generowany przy kompilacji grafu

`code_gen` przy generowaniu kernela tworzy równolegle mapę:

```cpp
struct ParameterMap {
    // mój komentarz: czy names i string z unorded_map nie lepiej zdefiniować to jako int i potem po stronie aplikacji parsować to przez mapa: <int, string> na faktyczny parametr? 
    std::vector<std::string> names;   // names[i] = nazwa i-tego parametru
    std::unordered_map<std::string, int> index;  // name → indeks w tablicy
    int total_free;   // liczba parametrów free
    int total_fixed;  // liczba parametrów fixed
};
```

Mapa jest przechowywana w `KernelManager` obok `CUfunction`. Jest ważna
przez całą sesję — dopóki graf się nie zmienia.

### Sygnatura kernela

Zamiast zahardkodowanych wartości, kernel przyjmuje:

```cuda
__global__ void evaluateIV(
    const double* __restrict__ params,   // wektor parametrów [total_free]
    const double* __restrict__ V_data,   // napięcia pomiarowe
    double*       I_out,                 // wyjście: prądy modelu
    int           N_points
)
```

Węzły modelu odczytują parametry przez indeks: `params[PARAM_IDX_AMPLITUDE]`
gdzie stałe indeksów są generowane przez `code_gen` jako `#define` lub
`constexpr` w kodzie kernela.

### Cykl ewaluacji funkcji celu

```
solver ma: double host_params[N_free]
  │
  ▼
cuMemcpyHtoD(d_params, host_params, N_free * sizeof(double))
  │
  ▼
cuLaunchKernel(evaluateIV_func, ..., d_params, d_V, d_I_out, N_points)
  │
  ▼
reduceChiSquare<<<...>>>(d_I_out, d_I_meas, d_sigma, d_chi2, N_points)
  │
  ▼
cuMemcpyDtoH(&host_chi2, d_chi2, sizeof(double))
  │
  ▼
solver otrzymuje: double chi2
```

Brak NVRTC w tym cyklu. NVRTC jest wywoływane **tylko** przy zmianie
struktury grafu.

---

## 🔧 Projekt: format prefit (punkt startowy)

Prefit = zapis stanu parametrów do pliku, który może być wczytany jako
punkt startowy dopasowania. Może być generowany przez aplikację lub przez
zewnętrzny skrypt Python.

### Format: JSON

```json
{
  "model_id": "sha256:abc123...",
  "parameters": [
    { "name": "I_0",   "value": 1.23e-12, "free": true,  "min": 1e-15, "max": 1e-6 },
    { "name": "A",     "value": 1.5,      "free": true,  "min": 0.5,   "max": 3.0  },
    { "name": "R_s",   "value": 0.01,     "free": false, "min": 0.0,   "max": 1.0  }
  ]
}
```

`model_id` = fingerprint topologii grafu (hash struktury węzłów i połączeń).
Aplikacja ostrzega, jeśli importowany plik pochodzi z innego modelu.

### Import / Export w UI

- **Export:** zapisuje aktualny stan parametrów (wartości, free/fixed, granice)
- **Import:** wczytuje plik, dopasowuje nazwy do bieżącej `ParameterMap`, uzupełnia
  punkt startowy; parametry obecne w mapie, ale nieobecne w pliku → wartości domyślne

❓ **Otwarte:** Czy jeden punkt startowy wystarczy dla wszystkich dopasowywanych
charakterystyk (batch), czy potrzebny jest GPU prefit generujący indywidualne
punkty startowe per charakterystyka? Do ustalenia po uruchomieniu działającego simpleksu.

---

## Model z Node Editora — decyzje

### Struktura grafu jest stała podczas jednej sesji

Model opisuje prąd przez urządzenie półprzewodnikowe. Zmiana modelu =
nowa sesja dopasowania. W trakcie jednej sesji: graf stały, parametry zmienne.

**Konsekwencja:** kernel NVRTC jest kompilowany **raz** po zmianie grafu.
Żadna kompilacja nie odbywa się podczas iteracji solvera.

### CPU i GPU — te same formuły

`evaluateSignal()` (CPU, debug) i kernel NVRTC (GPU, produkcja) muszą
implementować dokładnie te same formuły dla każdego węzła. Rozbieżność
implementacyjna = błąd systematyczny w dopasowaniu, nie tolerancja numeryczna.

---

## CPU vs GPU — podział ról

### Cały pipeline dopasowania jest na GPU

```
GPU:  [NM/SA: generuj kandydatów] → [evaluateIV kernel] → [reduce$\chi^2$]
         ↑_________________[ zaktualizuj stan solvera ]_________________↑
```

Solver iteruje na GPU. CPU nie prowadzi iteracji produkcyjnych.

### CPU = debug i referencja

- Pełny trace iteracji krok po kroku
- Inspekcja każdego wierzchołka, każdej operacji
- Weryfikacja poprawności GPU dla wybranych przypadków testowych
- Niedostępny dla batch GPU — zbyt duże dane

Debug = CPU (jeden przebieg, pełna observability).  
Batch produkcyjny = GPU (tysiące przebiegów, wyniki końcowe).

---

## Debugowanie i kontrola z UI

> Wymaganie krytyczne. Bez observability niemożliwe jest odróżnienie
> błędu implementacji od zachowania algorytmu.

### Podgląd stanu simpleksu

- Macierz wierzchołków: (N+1) × P wartości parametrów
- $\chi^2$ dla każdego wierzchołka
- Najlepszy / najgorszy wierzchołek, centroid
- Aktualny typ kroku (reflection / expansion / contraction / shrink)
- Aktualna temperatura T, numer iteracji, $\chi^2$_min

### Wizualizacja zbieżności

- Wykres $\chi^2$ (i $\Delta \chi^2$) vs iteracja
- Nakładka: dane pomiarowe IV + krzywa modelu dla aktualnych najlepszych parametrów

### Wizualizacja simpleksu — per-parametr scatter

Dla N~12–16 wymiarów rzut globalny jest nieczytelny.
Rozwiązanie: dla każdego free parametru osobny wykres — zbiór N+1 kolorowanych
punktów (jeden kolor per wierzchołek). Widać rozproszenie simpleksu
w każdym wymiarze niezależnie.

### Wykresy parametrów w czasie

Dla każdego free parametru: linia wartości w kolejnych iteracjach.
Widać, które parametry szybko zbiegają, które oscylują.

### Sterowanie

- Wykonanie 1 / N / do zbieżności kroków
- Restart (nowy punkt startowy lub nowy simplex)
- Edycja T_current w trakcie działania (suwak lub pole liczbowe)
- Zmiana CoolingSchedule w trakcie działania
- Zmiana free/fixed i granic parametrów (efektywna po restarcie)

### Tryb debug CPU — pełny trace

- Zapis każdego kroku: typ operacji, wierzchołki przed/po, $\chi^2$, T
- Przechodzenie krok po kroku (next / prev)
- Wyłącznie dla jednego przebiegu CPU

---

## Walidacja CPU ↔ GPU

Walidacja jest empiryczna — przeprowadzana w boju, nie z góry zdefiniowanymi tolerancjami.

Zakres podstawowy:
1. Ten sam zestaw parametrów → $\chi^2$ CPU vs $\chi^2$ GPU muszą być w sensownej tolerancji
2. Kilka pierwszych kroków simpleksu CPU vs GPU — porównanie ścieżki
3. Znane rozwiązania analityczne (modele testowe z prostą formułą) jako ground truth

Brak bitowej zgodności jest oczekiwany (kolejność operacji FP, FMA, implementacje
transcendentnych). Akceptowalna tolerancja: do ustalenia podczas testów.

---

## Batch na GPU

Wiele niezależnych dopasowań równolegle: wiele punktów startowych (multi-start)
lub wiele charakterystyk IV.

### Pamięć — nie jest ograniczeniem

Docelowo: 128 GB RAM, 48 GB+ VRAM. Tysiące simpleksów dla ~12–16 parametrów
zajmują MB — nie jest to problem projektowy.

### Wyniki batch

Batch zwraca na jedno dopasowanie: najlepsze parametry, $\chi^2_{min}$, liczba iteracji,
status zbieżności. Brak trace.

Weryfikacja GPU: testy wiarygodności — porównanie wybranych dopasowań z CPU,
sprawdzenie rozkładu $\chi^2_{min}$ w batch, modele testowe z known solution.

---

## Główne ryzyka

### Algorytmiczne

**Degeneracja simpleksu** — figura staje się płaska po wielu iteracjach;
konieczna detekcja (objętość < próg) i automatyczny restart.

**Clipping na granicach** — naiwne odbicie od `[min, max]` zaburza kształt;
wymaga przemyślanej obsługi (transformacja lub penalizacja za wyjście poza granice).

**NaN / overflow** — `exp()` wewnątrz modelu IV produkuje NaN dla ekstremalnych
parametrów; solver musi być odporny na NaN jako wartość funkcji celu.

**Lokalne minima** — SA z wysoką temperaturą pomaga, ale nie gwarantuje globalnego
optimum; multi-start jest standardową strategią.

**Harmonogram chłodzenia SA** — zbyt szybkie → utknięcie lokalnie; zbyt wolne →
niepraktycznie długi czas; Boltzmann jako default, ale strojenie empiryczne jest
konieczne dla konkretnych modeli IV.

### Implementacyjne

**Parametry runtime w code_gen** — kluczowa zmiana architektury; błąd tu blokuje
cały projekt.

**Spójność CPU ↔ GPU formuł** — każdy węzeł grafu musi być zaimplementowany
identycznie w `evaluateSignal()` i w generowanym kernelu.

**Wydajność FP64 na GPU** — RTX series: ~1/32 przepustowości FP32; dla złożonych
modeli IV może być wąskim gardłem; do sprawdzenia w boju.

**CURAND dla SA na GPU** — generator losowy musi być zainicjalizowany per wątek
(lub per dopasowanie w batch); stan generatora jest częścią stanu solvera.

### Systemowe

**Zmiana T przez UI podczas GPU batch** — modyfikacja T_current musi być
bezpieczna; synchronizacja między UI a wątkiem GPU.

**NVRTC z wątku głównego** — kompilacja blokuje UI; przy zmianie struktury
grafu akceptowalne (~1-2 s); nigdy podczas iteracji solvera.

---

## Zakres eksperymentu

### Co wchodzi

- SA-Enhanced Nelder–Mead (jeden hybrydowy algorytm)
- Funkcje celu: $\chi^2$ i $\Delta \chi^2$
- Parametry runtime bez rekompilacji NVRTC (ParameterMap + `double* params`)
- Temperatura T jako parametr runtime modyfikowany przez użytkownika
- Harmonogram Boltzmann jako default, łatwo wymienialny (CoolingSchedule enum)
- Model IV z Node Editora (liniowa skala parametrów, exp() wewnątrz modelu)
- Debug UI: per-parametr scatter, wykresy zbieżności i temperatury, sterowanie iteracjami
- CPU debug: pełny trace, krok po kroku
- GPU batch: wiele niezależnych dopasowań, wyniki końcowe
- Prefit: format JSON (import/export punktu startowego)

### Co nie wchodzi

- Pełna aplikacja produkcyjna
- Zaawansowane fizyczne modele (poza tym, co generuje Node Editor)
- Gradient-based methods (BFGS, L-BFGS)
- Optymalizacja kerneli GPU (shared memory, warp efficiency)
- GPU prefit (do rozważenia po uruchomieniu simpleksu)
- System pluginów

---

## Otwarte pytania

**O batch i preficie:**
Czy jeden punkt startowy wystarczy dla wszystkich dopasowywanych charakterystyk
IV (batch), czy konieczny jest mechanizm GPU prefit generujący indywidualne
punkty startowe per charakterystyka? Do ustalenia po uruchomieniu działającego simpleksu.

**O harmonogramie SA:**
Jaki konkretny T_initial i jaka szybkość Boltzmanna działają dla typowych modeli IV
półprzewodnikowych? Brak odpowiedzi a priori — wymaga testów na realnych danych.

**O walidacji:**
Jaka jest empiryczna tolerancja $\Delta \chi^2$ między CPU i GPU dla typowego modelu IV?
Do ustalenia podczas testów.

---

## Podsumowanie

**Problem:** Implementacja SA-Enhanced Nelder–Mead do dopasowania modeli IV
półprzewodników generowanych dynamicznie w node edytorze, z pełnym pipeline'em
na GPU, kontrolą temperatury SA w runtime, per-parametr wizualizacją simpleksu
i CPU debug trace.

**Co jest rozstrzygnięte i zaprojektowane:**

| Kwestia                       | Decyzja                                                          |
| ----------------------------- | ---------------------------------------------------------------- |
| Relacja NM i SA               | SA wewnątrz każdego kroku NM (jeden algorytm)                    |
| Harmonogram chłodzenia        | Boltzmann default, CoolingSchedule enum (wymienialny)            |
| Temperatura T                 | Parametr runtime, edytowalny przez UI w trakcie działania        |
| Skala parametrów solvera      | Zawsze liniowa; exp() wewnątrz modelu IV                         |
| Parametry do GPU              | `double* params` + ParameterMap generowana przy kompilacji grafu |
| Kompilacja NVRTC              | Raz per struktura grafu; zero razy podczas iteracji              |
| Format prefit                 | JSON z model_id (fingerprint grafu)                              |
| Podział CPU/GPU               | GPU = produkcja; CPU = debug i referencja                        |
| Wizualizacja simpleksu        | Per-parametr scatter (N+1 kolorowych punktów)                    |
| Pamięć                        | Nie jest ograniczeniem (128 GB RAM, 48 GB+ VRAM docelowo)        |
| Struktura grafu podczas sesji | Stała; zmiana = nowa sesja                                       |

**Co jest najtrudniejsze:**

1. **ParameterMap + parametry runtime** — fundamentalna zmiana `code_gen`;
   blokuje resztę projektu jeśli źle zaprojektowana.

2. **Odporność na NaN / granice** — modele IV eksplodują numerycznie poza
   zakresem fizycznym; solver musi to obsłużyć bez krachu.

3. **Strojenie harmonogramu SA** — nie istnieje dobra odpowiedź a priori
   dla konkretnych modeli IV; wymaga empirycznej iteracji.

**Dlaczego debug CPU jest kluczowy:** GPU jest black boxem. CPU trace pozwala
zweryfikować każdy krok: czy reflection jest poprawne, czy SA akceptuje właściwe
rozwiązania, czy temperatura spada zgodnie z harmonogramem. Bez tego każdy
błąd implementacji staje się widoczny dopiero jako złe dopasowanie końcowe.