# Faza GPU — Plan implementacji Monte Carlo Simplex

> Plan jest dopasowany do `GPU-context.md`. Ten dokument opisuje wyłącznie fazę GPU
> dla symulacji Monte Carlo stabilności dopasowania. CPU solver, aktualny UI i import
> danych pozostają bazą referencyjną.

---

## 0. Cel

Zaimplementować backend CUDA dla Monte Carlo:

1. Wygenerować `K` niezależnych realizacji szumu dla tej samej idealnej krzywej IV.
2. Dla każdej realizacji uruchomić niezależny Nelder-Mead na GPU.
3. Zwrócić rozkład dopasowanych parametrów oraz `Δreducedχ²`.
4. Pozostawić CPU solver jako fallback i narzędzie walidacyjne.

Najważniejsza metryka fazy GPU:

```text
chi²              = Σᵢ [(I_meas[i] - I_model[i]) / sigma[i]]²
dof               = max(1, N_points - N_free_params)
reduced_chi²      = chi² / dof
Δreducedχ²[k]     = fit_reduced_chi²[k] - ref_reduced_chi²[k]
```

W MC liczy się `Δreducedχ²`, nie surowe `chi²`.

---

## 1. Stan bazowy, którego nie zmieniać

### CPU solver

Nie przepisywać ani nie usuwać istniejącego stosu CPU:

```text
src/solver/
  solver_types.hpp
  nelder_mead.hpp/.cpp
  param_utils.hpp/.cpp
  diode_model.hpp/.cpp
  objective.hpp/.cpp
  diode_objective.hpp/.cpp
  lambertw.hpp
  phys_const.hpp
  trace_utils.hpp/.cpp
```

GPU ma odwzorować zachowanie CPU:

- solver pracuje w skali liniowej,
- `Rs = 0` ma osobną granicę L'Hôpitala,
- `NaN` lub `Inf` z modelu daje `+∞ chi²`,
- `sigma[i] <= 0` daje `+∞ chi²`,
- simpleks startuje od perturbacji `5%` zakresu `[min, max]`,
- zdegenerowany simpleks restartuje się wokół najlepszego punktu,
- warunek stopu używa `reduced_chi² < reduced_chi2_tol`.

### Model IV

GPU musi implementować dokładnie model z `src/solver/diode_model.cpp`, a nie kernel JFM.

Model 4P:

```text
params = { I0, A, Rs, Rsh }
x      = (I0 * Rs / (A * Vt)) * exp(V / (A * Vt))
I_lw   = W0(x) * (A * Vt) / Rs
I      = I_lw + (V - I_lw * Rs) / Rsh
```

Dla `Rs = 0`:

```text
I = I0 * exp(V / (A * Vt)) + V / Rsh
```

Model 6P:

```text
params = { I0, A, Rs, Rsh, alpha, Rsh2 }
I_lw   = jak w 4P
Vj     = V - I_lw * Rs
I      = I_lw + Vj / Rsh + pow(Vj, alpha) / Rsh2
```

Jeśli `pow(Vj, alpha)` zwróci `NaN`, kandydat dostaje `+∞ chi²`.

---

## 2. Decyzje architektoniczne

| Symbol | Decyzja |
| ------ | ------- |
| A | Jeden blok CUDA = jeden niezależny run Nelder-Mead. |
| B | Blok ma 32 wątki, czyli jeden warp; simpleks żyje w shared memory. |
| C | Wątek 0 steruje NM, wszystkie wątki liczą częściowe `chi²`. |
| D | Kernel NM jest kompilowany statycznie przez NVCC, nie przez NVRTC. |
| E | `GpuParamLayout` mapuje wolne parametry na pełny wektor parametrów. |
| F | Szum MC jest generowany na GPU per realizacja fitu. |
| G | `ref_reduced_chi²` jest liczone na GPU dla każdej realizacji. |
| H | SA nie wchodzi do pierwszej wersji GPU; zostaje tylko hook w konfiguracji. |
| I | GPU jest alternatywnym backendem; CPU solver zostaje dostępny zawsze. |

Uwaga: `GPU-integration/` jest wzorcem architektonicznym, ale ta faza używa NVCC dla
statycznego kernela NM. Nie przenosić mechanizmu NVRTC do GPU Simplex.

---

## 3. Drzewo zmian

### Nowe pliki

```text
src/gpu/
  gpu_types.hpp
  diode_model_device.cuh
  gpu_nm_kernel.cuh
  gpu_nm_kernel.cu
  gpu_mc_runner.hpp
  gpu_mc_runner.cpp
```

### Modyfikowane pliki

```text
src/app.hpp
src/app.cpp
src/gui.cpp
premake5.lua
```

### Poza zakresem

- GPU port `SingleFitState` i `MultiFitState`,
- batch wielu plików `.dat`,
- Levenberg-Marquardt,
- SA w GPU NM,
- runtime-edytowalne kernele NVRTC.

---

## 4. Struktury danych

### `src/gpu/gpu_types.hpp`

Wprowadzić typy zgodne z `GPU-context.md`:

```cpp
enum class GpuModelType : int { Diode4P = 0, Diode6P = 1 };

struct GpuParamLayout {
    static constexpr int MAX_PARAMS = 8;
    int n_total;
    int n_free;
    int free_to_total[MAX_PARAMS];
    double fixed_values[MAX_PARAMS];
    double min_bounds[MAX_PARAMS];
    double max_bounds[MAX_PARAMS];
    double T;
    int dof;
};

struct GpuNmConfig {
    double alpha = 1.0;
    double gamma = 2.0;
    double rho = 0.5;
    double sigma_shrink = 0.5;
    double degenerate_tol = 1e-12;
    int max_iter = 5000;
    double reduced_chi2_tol = 1.0;
    bool sa_enabled = false;
    double sa_T_initial = 0.0;
    double sa_geometric_rate = 0.99;
};

struct GpuMcResult {
    static constexpr int MAX_FREE = 8;
    double best_free_params[MAX_FREE];
    double chi2_min;
    double reduced_chi2_min;
    double ref_reduced_chi2;
    double delta_reduced_chi2;
    int iterations;
    int converged;
    int n_free;
};
```

Po stronie aplikacji dodać stan MC:

```cpp
struct GpuMcConfig {
    int n_samples = 200;
    unsigned int rng_seed = 42;
    bool use_random_starts = true;
    int random_start_seed = 12345;
    bool cpu_validation = false;
};

struct GpuMcSampleData {
    std::vector<double> fitted_params;
    double reduced_chi2;
    double ref_reduced_chi2;
    double delta_reduced_chi2;
    int iterations;
    bool converged;
};

struct GpuMcState {
    std::vector<GpuMcSampleData> samples;
    int n_completed = 0;
    int n_total = 0;
    bool running = false;
    double elapsed_s = 0.0;
    std::string status;
    bool gpu_available = false;
    std::string gpu_device_name;
};
```

---

## 5. Stage G1 — fundament

### Zakres

- Dodać `src/gpu/gpu_types.hpp`.
- Dodać `src/gpu/diode_model_device.cuh`.
- Dodać deklaracje kerneli w `src/gpu/gpu_nm_kernel.cuh`.
- Ustawić `premake5.lua` tak, aby `.cu` z `src/gpu/` były kompilowane przez NVCC.
- Podpiąć include path do `Vendor/LambertW/`.

### `diode_model_device.cuh`

Wymagania:

- używać `LambertW0_d()` z `lambert_w_device.cuh`,
- mieć tę samą stałą `k_B/q` co CPU,
- obsłużyć `Rs = 0`,
- zwracać `NaN` dla parametrów niefizycznych,
- nie implementować modelu JFM z `Iph`.

### Premake

W `premake5.lua` dodać build rules dla CUDA:

- źródła: `src/gpu/*.cu`,
- include: `src`, `Vendor/LambertW`, `$(CUDA_PATH)/include`,
- libdirs: `$(CUDA_PATH)/lib/x64`,
- links: `cudart`, ewentualnie biblioteki CUDA potrzebne przez runner,
- konfiguracje Debug/Release x64.

### Kryteria akceptacji

- Projekt kompiluje się z pustym/minimalnym `gpu_nm_kernel.cu`.
- `gpu_types.hpp` nie wymaga headerów CUDA.
- Device model 4P/6P jest zgodny wzorami z CPU.

---

## 6. Stage G2 — kernel Nelder-Mead

### Zakres

Zaimplementować `src/gpu/gpu_nm_kernel.cu`:

- jeden blok = jedna realizacja MC,
- `blockDim.x = 32`,
- `gridDim.x = K`,
- shared memory przechowuje simpleks,
- wątek 0 wykonuje kroki NM,
- warp równolegle liczy `chi²`,
- wynik jednego bloku trafia do `GpuMcResult[k]`.

### Shared memory

Minimalny stan:

```cpp
struct NmSharedState {
    double vertices[7][6];
    double chi2[7];
    double centroid[6];
    double candidate[6];
    double chi2_candidate;
    double chi2_reflect;
    int order[7];
    int n_vertices;
    int n_free;
    bool any_invalid;
};
```

`chi2_reflect` jest wymagane, żeby poprawnie rozstrzygnąć `reflect` vs `expand`.

### Ewaluacja `chi²`

Dla każdego punktu:

```text
residual = (I_noisy[i] - I_model[i]) / sigma[i]
chi²    += residual²
```

Reguły:

- `sigma[i] <= 0` daje `+∞`,
- `NaN/Inf` z modelu daje `+∞`,
- redukcja sumy przez warp shuffle,
- `reduced_chi²` liczyć jako `chi² / layout.dof`.

### Algorytm NM

Zachować kolejność CPU:

1. sortowanie wierzchołków po `chi²`,
2. centroid bez najgorszego,
3. reflection,
4. expansion,
5. contraction,
6. shrink,
7. restart przy degeneracji,
8. stop przy `reduced_chi² < reduced_chi2_tol` albo `max_iter`.

### Kryteria akceptacji

- Dla `K = 1` i bez szumu wynik GPU jest bliski CPU.
- `reduced_chi2_min = chi2_min / dof`.
- `delta_reduced_chi2 = reduced_chi2_min - ref_reduced_chi2`.
- Kernel nie crashuje na `NaN`, tylko oznacza kandydata jako zły.

---

## 7. Stage G3 — Monte Carlo runner

### Zakres

Dodać `src/gpu/gpu_mc_runner.hpp/.cpp`, czyli C++ warstwę orkiestracji:

- wykrycie CUDA,
- alokacja buforów,
- przygotowanie `GpuParamLayout`,
- launch kerneli,
- kopiowanie wyników,
- mapowanie `best_free_params` na pełny wektor parametrów,
- asynchroniczne uruchomienie z poziomu aplikacji.

### Szum na GPU

Szum w trybie procentowym:

```text
sigma[i]     = max(abs(I_true[i]) * noise_pct / 100, min_sigma)
I_noisy[k,i] = I_true[i] + normal(0, sigma[i])
```

`sigma` jest zależna od idealnej wartości prądu dla danego napięcia, nie od globalnego
`I_max` i nie od zaszumionej wartości. `noise_pct` pochodzi z UI i ma zakres `0..100`.

Szum generować na GPU per realizacja `k`. Jeśli nie ma cuRAND w projekcie, użyć prostego,
deterministycznego generatora device-side z seedem:

```text
seed = rng_seed + k * large_prime + i
```

### Reference reduced chi² na GPU

Dla każdej realizacji:

```text
ref_reduced_chi²[k] = chi²(params_true, I_noisy[k]) / dof
```

To musi być liczone na GPU, zgodnie z `GPU-context.md`.

### Asynchroniczność

Runner nie może blokować UI:

- `appRunGpuMc()` startuje zadanie,
- `appUpdateGpuMcResults()` sprawdza postęp/wynik co klatkę,
- `appCancelGpuMc()` ustawia flagę anulowania, jeśli implementacja ją obsługuje.

### Kryteria akceptacji

- UI pozostaje responsywne podczas MC.
- Brak CUDA wyłącza panel GPU, ale CPU działa dalej.
- `K` wyników ma poprawne `reduced_chi2`, `ref_reduced_chi2`, `delta_reduced_chi2`.

---

## 8. Stage G4 — UI Monte Carlo

### Zakres

Dodać panel MC w `src/gui.cpp`:

- wybór backendu CPU/GPU,
- `K` realizacji,
- seed szumu,
- seed startów,
- `max_iter`,
- `reduced_chi2_tol`,
- przyciski `Run MC (GPU)` i `Cancel`,
- status GPU i nazwa urządzenia,
- tabela wyników.

### Wykresy

Dodać widoki:

- histogram `Δreducedχ²`,
- histogramy parametrów,
- scatter `I0-A`,
- scatter `Rs-Rsh`,
- scatter `alpha-Rsh2` dla modelu 6P,
- kolorowanie punktów przez `Δreducedχ²`.

Progi ufności Numerical Recipes pokazywać w skali reduced:

```text
Δreducedχ²_threshold = Δchi²_NR / dof
```

### Kryteria akceptacji

- Panel renderuje się bez wyników i bez dostępnego GPU.
- Przycisk GPU jest disabled, gdy `gpu_available == false`.
- Punkty MC są kolorowane po `Δreducedχ²`.
- Tabela pokazuje `reduced_chi2`, `ref_reduced_chi2`, `delta_reduced_chi2`, iteracje i converged.

---

## 9. Stage G5 — walidacja

### Testy porównawcze

1. Model 4P GPU vs CPU dla kilku napięć i temperatur.
2. Model 6P GPU vs CPU, w tym przypadek `Vj < 0` i niecałkowite `alpha`.
3. `Rs = 0` GPU vs CPU.
4. `chi²` GPU vs CPU dla tej samej krzywej i tych samych `sigma`.
5. `reduced_chi² = chi² / dof`.
6. `Δreducedχ² = fit_reduced_chi² - ref_reduced_chi²`.
7. `K = 1`, bez szumu: GPU NM zbiega blisko CPU.
8. `K = 50`, seed stały: rozkład GPU jest stabilny między uruchomieniami.
9. Brak CUDA: aplikacja startuje, panel GPU jest disabled.

### Kryteria końcowe

- Debug x64 kompiluje się przez Premake/Visual Studio.
- GPU MC działa dla `K >= 1000`, `N_points >= 200`.
- CPU solver nadal działa bez zmian.
- `Δreducedχ²` jest główną wartością w wynikach MC i na wykresach.

---

## 10. Pułapki

| Problem | Skutek | Rozwiązanie |
| ------- | ------ | ----------- |
| Użycie JFM kernel zamiast dark IV | zły model fizyczny | portować dokładnie `diode_model.cpp` |
| Surowe `chi²` w MC | złe kryteria akceptacji | używać `reduced_chi²` i `Δreducedχ²` |
| Szum liczony od `I_max` | nierówny model szumu | `sigma[i] = abs(I_true[i]) * noise_pct / 100` |
| `NaN` w redukcji | zatrucie całego wyniku | wykrywać `NaN/Inf` i zwracać `+∞` |
| Brak `chi2_reflect` | błędny krok expansion | przechować `f_reflect` przed expansion |
| NVRTC zamiast NVCC | niezgodne z kontekstem | kernel NM budować statycznie NVCC |
| Blokowanie UI | aplikacja wygląda jak zawieszona | runner asynchroniczny |
| Brak CPU fallback | aplikacja bezużyteczna bez CUDA | GPU tylko jako alternatywny backend |

---

## 11. Kolejność prac

1. `G1`: typy, device model, Premake CUDA.
2. `G2`: kernel NM bez MC-noise, test `K = 1`.
3. `G3`: GPU noise, GPU reference chi², runner asynchroniczny.
4. `G4`: panel UI i wykresy MC.
5. `G5`: walidacja CPU/GPU i poprawki stabilności.

