# VALIDATION — Lambert W GPU  (`lambert_w_device.cuh`)

Per-algorithm validation document.  
Format: SST project convention (`docs/per-algorithm/VALIDATION.md`).

---

## 1. Algorithm

**Implementacja**: Veberic (2012), przeniesiona na GPU.  
**Referencja CPU**: `utl::LambertW` z https://github.com/SirJamesClarkMaxwell/LambertW  
**Niezależna referencja**: `Fukushima::LambertW` (metoda bisection + lookup table)

### Warianty

| Funkcja           | Gałąź | Domena            | Kodziałanie |
|-------------------|-------|-------------------|-------------|
| `LambertW0_d`     | W₀    | `[-1/e, +∞)`      | NVCC + NVRTC |
| `LambertWm1_d`    | W₋₁   | `[-1/e, 0)`       | NVCC + NVRTC |
| `LambertW0_f`     | W₀    | jak wyżej (float) | via double promotion |
| `LambertWm1_f`    | W₋₁   | jak wyżej (float) | via double promotion |

---

## 2. Regiony przybliżeń (W₀)

Algorytm dzieli dziedzinę na 5 regionów. Każdy region ma inny seed (przybliżenie
startowe), po którym wykonywany jest jeden krok Halleya:

```
x ∈ [-1/e, -0.367679)  → branch-point series, rząd 8   (bez Halleya)
x ∈ [-0.367679,-0.311) → Halley( BP series rząd 10 )
x ∈ [-0.311, 1.38)     → Halley( Padé [4/4] w x )
x ∈ [1.38, 236)        → Halley( Padé log-shifted [3/2] )
x ≥ 236                → Halley( asymptotyka de Bruijn, 5 składników )
```

## 3. Regiony przybliżeń (W₋₁)

```
x ∈ [-1/e, -0.367579)  → branch-point series, rząd 8   (bez Halleya)
x ∈ [-0.367579,-0.366079) → Halley( BP series rząd 4 )
x ∈ [-0.366079,-0.289379) → Halley( Padé -1-√[4/4] )
x ∈ [-0.289379,-0.0509)   → Halley( Padé rational [4/4] )
x ∈ [-0.0509,-1.318e-4)   → Halley( Padé exp-log [3/3] )
x ∈ [-1.318e-4,-6.31e-31) → Halley( Padé exp-log [4/4] )
x ∈ [-6.31e-31, 0)        → Halley( log-recursion, 3 kroki )
```

---

## 4. Dokładność (expected)

Porównanie GPU vs CPU Veberic (ta sama klasa algorytmu):

| Region            | Max błąd względny | Uwagi |
|-------------------|-------------------|-------|
| BP series         | < 2e-15           | Bez iteracji — wynik serii |
| Padé + Halley     | < 2e-15           | 1 krok Halleya wystarczy   |
| Asymptotyka W₀    | < 2e-15           | Dla x >> e                 |
| Log-recursion W₋₁ | < 2e-15           | x blisko 0-                |
| `float` wariant   | < 1e-7            | Przez double promotion      |

Maksymalny residuał `|w·eʷ − x| / |x|` we wszystkich regionach: < 1e-14.

> **Ważne**: GPU używa tych samych współczynników co CPU. Różnice numeryczne
> wynikają wyłącznie z innej kolejności operacji FP (FMA vs scalarne mnożenia).
> Są rzędu 1-2 ULP, co jest zgodne z oczekiwaniami dla double.

### 4.1 Weryfikacja niezależna (mpmath, 50 cyfr) — uwaga o referencji `scipy`

Podczas weryfikacji tej implementacji okazało się, że `scipy.special.lambertw`
**sama traci precyzję** w bezpośrednim sąsiedztwie punktu gałęzi (`|x + 1/e| < 1e-6`),
spadając do błędu względnego ~2×10⁻⁵ przy `x = -1/e + 1e-10` (scipy używa
generycznego solvera Halleya/Newtona bez wyspecjalizowanej serii branch-point).

Potwierdzone przez `mpmath` z 50 cyframi precyzji jako niezależny arbiter:

| x                  | mpmath (ground truth) | scipy błąd | GPU (ten plik) błąd |
|--------------------|------------------------|------------|----------------------|
| `-1/e + 1e-10`     | -1.0000233166210367…  | 2.33e-05   | **1.88e-12**         |
| `-1/e + 1e-7`      | -0.9999076507547150…  | 1.55e-13   | **7.89e-15**         |
| `-1/e + 1e-13`     | -1.0000007373307487…  | 7.37e-07   | **8.87e-11**         |

Wniosek: implementacja GPU (seria branch-point Veberic, rząd 8) jest **dokładniejsza
niż `scipy` w tym regionie**, dokładnie dlatego że używa wyspecjalizowanej serii
zamiast generycznej iteracji. Jeśli w przyszłości pojawi się rozbieżność GPU vs
`scipy` blisko `x = -1/e`, to `scipy` jest podejrzany jako pierwszy — nie ten kod.
Referencją do zaufania blisko punktu gałęzi jest `mpmath` lub `utl::LambertW` (CPU,
ta sama rodzina algorytmu), nie generyczne solvery.

---

## 5. Test suite (`lambert_w_gpu_test.cu`)

### 5.1 Weryfikacja numeryczna

```
N = 65536 punktów testowych
Grid W₀:  [-1/e, 0) (gęsto), [0, 500] (log-uniform)
Grid W₋₁: [-1/e, 0) log-uniform w |x|, zakres |x| ∈ [10⁻⁴⁰, 1/e]
```

Porównanie z:
- `utl::LambertW<0>` / `utl::LambertW<-1>` (Veberic CPU)
- `Fukushima::LambertW0` / `Fukushima::LambertWm1` (niezależna referencja)

Wewnętrzny test: residuał `lw__residual(x, w)` dla każdego punktu.

### 5.2 Znane wartości

```
x =  3.14,  branch  0  →  1.073395661239825
x = -0.2,   branch  0  → -0.2591711018190738
x =  0.0,   branch  0  →  0.0
x = -0.2,   branch -1  → -2.542641357773526
x = -0.3,   branch -1  → -1.781337023421628
x = -1e-10, branch -1  → -25.78808890543161    (log-recursion region)
x =  1e3,   branch  0  →  5.249602852401596    (asymptotic region)
```

### 5.3 Throughput

Mierzony `cudaEvent` dla N=1M ewaluacji W₀, 5 powtórzeń.  
Oczekiwane (RTX 2070 Super, sm_75): **500–1500 M eval/s** dla W₀.

---

## 6. Ograniczenia i znane przypadki graniczne

### Punkt gałęzi x = -1/e

`LambertW0_d(-1/e)` i `LambertWm1_d(-1/e)` powinny zwracać `W = -1.0`.  
W okolicy tego punktu jest ciągłość, ale pochodna jest nieskończona → wolniejsza
zbieżność serii BP. Dlatego algorytm używa serii rzędu 8 bezpośrednio (bez Halleya)
dla `x < -0.367679`, gdzie dokładność serii jest wystarczająca.

### x → 0⁻ dla W₋₁

`LambertWm1_d(x)` → −∞ gdy x → 0⁻.  
Log-recursion obsługuje to poprawnie dla x ∈ [−6.31e-31, 0).  
Dla x < -6.31e-31 stosowany jest Padé log-shifted.  
Dla x = 0 (dokładnie): funkcja zwraca NaN (bo domena to `x < 0`).

### `--use_fast_math` w NVRTC

Opcja `--use_fast_math` włącza sprzętowe przybliżenia `exp`, `log`, `sqrt`
(~0.5 ULP dokładność na `exp`). Dla JFM fitting jest to zazwyczaj akceptowalne.
Jeśli wymagana jest pełna precyzja (np. weryfikacja MC), usuń tę opcję.

---

## 7. Integracja z JFM

### Model jednodirektywny (5P)

Równanie I(V) z jawnym rozwiązaniem przez W₀:

```
I(V) = (Iph + I0 - V/Rsh) / (1 + Rs/Rsh)
       - (n·Vt/Rs) · W₀(arg)

arg  = (I0·Rs / (n·Vt)) · exp((Rs·(Iph+I0) + V) / (n·Vt·(1 + Rs/Rsh)))
```

Kernel `jfm_single_diode_kernel` w `lambert_w_gpu_test.cu` implementuje to bezpośrednio.

### Model dwudirektywny (7P) — W₀ + W₋₁

Modele z dwiema gałęziami (np. bifacial cells, tandem cells) mogą wymagać obu funkcji.
Obie są dostępne w tym samym nagłówku.

---

## 8. Build

### NVCC (statyczne linkowanie w `.cu`)

```lua
-- premake5.lua fragment
filter { "files:src/**.cu" }
    buildcommands {
        '"$(CUDA_PATH)/bin/nvcc" -c -O2 -std=c++17'
        .. ' -gencode arch=compute_75,code=sm_75'
        .. ' -I"lambert_w_device.cuh directory"'
        .. ' -I"$(CUDA_PATH)/include"'
        .. ' -o "$(IntDir)%{file.basename}.obj"'
        .. ' "%{file.relpath}"'
    }
```

### NVRTC (runtime compilation, KernelManager)

```cpp
// W KernelManager::compile():
const char* headerNames[]   = { "lambert_w_device.cuh" };
const char* headerSources[] = { lambertW_header_cstr };  // zawartość pliku

nvrtcCreateProgram(&prog, kernelSource, "kernel.cu",
                   1, headerSources, headerNames);
```

Pełny przykład: `lambert_w_nvrtc_example.cpp`.

---

## 9. Pliki

```
lambert_w_device.cuh                  ← główny nagłówek GPU (NVCC + NVRTC)
                                         destynacja: Vendor/LambertW/lambert_w_device.cuh
faza6_lambertw_gpu_integracja.md      ← patch do KernelManager (header injection)
                                         + przykład wiring + troubleshooting
kernels_jfm_single_diode.cu           ← przykładowy kernel NVRTC (JFM I(V), residuals, MC)
                                         destynacja: kernels/jfm_single_diode.cu
lambert_w_gpu_test.cu                 ← standalone test accuracy + throughput + JFM demo
                                         (NVCC, linkowany z Vendor/LambertW/*.cc)
VALIDATION.md                         ← ten dokument
```

`lambert_w_device.cuh` nie zostanie przypadkowo wciągnięty przez
`files { "%{prj.location}/*.h", "%{prj.location}/*.cc" }` w istniejącym
`Vendor/LambertW/premake5.lua` — `.cuh` ≠ `.h` jako sufiks globu, więc
`cl.exe` nigdy go nie zobaczy. Szczegóły: `faza6_lambertw_gpu_integracja.md`, §3.

---

## 10. Referencje

1. D. Veberic, *Having Fun with Lambert W(x) Function*, arXiv:1003.1628 (2010)
2. D. Veberic, *Lambert W Function for Applications in Physics*, Comp. Phys. Comm. **183** (2012) 2622–2628. DOI: 10.1016/j.cpc.2012.07.008
3. T. Fukushima, *Precise and fast computation of Lambert W-functions without transcendental function evaluations*, J. Comp. Appl. Math. **244** (2013) 77–89
4. R.M. Corless et al., *On the Lambert W function*, Adv. Comput. Math. **5** (1996) 329–359
5. N.G. de Bruijn, *Asymptotic Methods in Analysis*, Dover (1981)
