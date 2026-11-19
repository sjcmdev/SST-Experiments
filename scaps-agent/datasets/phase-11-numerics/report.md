---
status: active
type: experiment
tags: [scaps, agent, phase-11]
---

Faza 11 — numerics & robustness, 2026-11-19

Cel: znaleźć granice wiarygodności SCAPS 3.3.12 i przygotować mały benchmark dla `scaps_core`.

Wykonano 8 przypadków, 14 wyeksportowanych dark I–V (949 punktów), 14 końcowych profili i 2 equilibrium profiles. Dataset: `datasets/phase-11-numerics/`; liczby: `summary.json`; wykres: `analysis.png`. Każdy przypadek ma raw, CSV SI, input/saved/final `.def`, `.act`, replay i `params.json` z pełną definicją parametrów oraz SHA256 źródeł. Energie clean profiles są w J; oryginały SCAPS/Core pozostają w eV.

Pulpit active. GUI nie reagowało po real mouse, double Set problem, single shot, hwnd, restarcie i starcie z `.def`; nie znaleziono hidden modal. Użyto script fallback, bez GUI screenshots. SCAPS zamknięty; `windows=[]`.

Wspólna definicja Si p–n z fazy 5: p po lewej, 5 µm/Na=1e22 m⁻³; n po prawej, 0,2 µm/Nd=1e24 m⁻³. Eg=1,12 eV, chi=4,05 eV, eps_r=11,7; Nc/Nv=2,8e25/1,04e25 m⁻³; mu_n/p=0,135/0,048 m²/Vs. Neutral single trap Ev+0,56 eV w p: Nt=1e20 m⁻³, sigma_n/p=1e−19 m², vth=1e5 m/s. Flatband contacts, finite Sn/Sp jak w zapisanych `.def`; Rs/Gsh disabled. Dark, f=1 MHz zapisana pomocniczo; I–V nie jest pomiarem AC. Standardowo 0…0,6 V co 0,02 V.

| Przypadek | Zmiana względem baseline | J(0,6 V), A/m² | Czas procesu SCAPS | Ocena |
|---|---|---:|---:|---|
| baseline | 300 K | 3821,68079 | 3,2 s | bez flag przyjętych kontroli |
| low80 | 80 K | 1,64907006e−23 | 4,2 s | terminal current unresolved; spatial spread 4,305e−7 A/m² |
| low40 | 40 K | 1,56699961e−57 | 5,5 s | terminal current unresolved; spatial spread 1,608e−7 A/m² |
| doping | Na=Nd=2e25 m⁻³ = 2e19 cm⁻³ | 5,7095741 | 2,7 s | spatial spread 10,12%; degeneracy poza walidacją |
| deep | Nt=1e25 m⁻³ = 1e19 cm⁻³ | 324384,991 | 3,5 s | bilans przy 0,6 V: błąd 1,55e−5 względnie |
| thin | p=50 nm, n=2 nm | 8974,37885 | 2,9 s | numerycznie stabilny model klasyczny |
| thick | p=500 µm, n=20 µm | 481,939788 | 2,8 s | bilans przy 0,6 V: błąd 1,08e−7 względnie |
| forward | zakres do 2 V | 3821,68079 przy 0,6 V | pierwszy timeout 12 s; logged replay 7,4 s | failure 1,68 V; skan ucięty na 1,66 V |

Czasy zawierają start aplikacji i eksport, nie są samym solver time ani medianą powtórzeń. Mesh baseline/80 K/40 K: 261/526/913 węzłów. W tym zakresie nie wystąpiło długie spowolnienie; completion nie oznacza poprawności prądu.

Kontrola numeryczna: |J−J_total_rec| > 1e−8 A/m² + 1%|J| daje flagę per point. W dark przy stałym prądzie i zerowej generacji bilans terminalny oraz całkowita rekombinacja z eksportu powinny się zgadzać. Spatial spread = max J(x)−min J(x), oceniany osobno. Jest to jawny próg diagnostyczny benchmarku, nie uniwersalny solver acceptance gate. Low-T ma mały błąd bezwzględny, ale eksport nie rozstrzyga prądu terminalnego rzędu 1e−23…1e−57 A/m².

Próby poprawy:

- Forward, krok 0,005 V: failure 1,670 V, ostatni punkt 1,665 V; 334 punkty/11,0 s. Zmniejszenie kroku nie usunęło granicy.
- Forward, clamp_psi/n/p=0,1 kT i maxiter=5000: failure nadal 1,68 V, 9,1 s. Fixed mesh: to samo failure, 200 węzłów/3,8 s. Brak potwierdzonej poprawy.
- Osobny `forward-safe` kończy się bez failure na 1,66 V, ale błąd bilansu wynosi 89,69%, a spatial spread/|Jterminal|=17,57. Nazwa oznacza tylko brak modala. Pierwsze przekroczenie progu bilansu 1%: 1,46 V; dla fine 1,455 V. Przy 1,2 V błąd wynosi jeszcze 1,93e−7 względnie.
- Doping: termination_psi/Fn/Fp=1e−6 kT, max mesh ratio=1,2 zamiast 1,6. Mesh 270→452; przy 0,6 V błąd bilansu 1,28e−5→4,27e−7 względnie, lecz spatial spread 10,12→9,86%. Partial improvement; prąd nadal nie jest dobrym oracle.
- Low80: termination=1e−6 kT nie zmieniło anomalnego eksportu. Clamp/tolerancje są jednostkami kT, nie stałymi eV. Ustawienia potwierdzono w saved `.def`.
- `set errorhandling.appendtofile` usuwa blokujący convergence modal i pozwala zapisać ucięte I–V. Zachowano `SCAPSErrorLogFile.log`. Initial GUI OK/Enter nie zamknęły modala; normal close zakończyło sesję. Logged replay forward miał exit=3221226525 po eksportach; pozostałe główne completed runs zwykle exit=6. Oba wymagają niezależnej walidacji plików.

Po failure `.eb` zawiera attempted failed bias: 1,68 V, choć I–V kończy się na 1,66 V. `profile_voltage_V` i `PROFILE_AT_FAILED_BIAS` zapobiegają porównaniu niewłaściwych punktów. Dowód błędu przy 1,66 V pochodzi z osobnego completed `forward-safe`.

Porównanie Core: przeczytano README repo i `scaps_solver_validation_tools/README.md`, wykonano `graphify query` przed wyszukiwaniem kodu. README narzędzi dokumentuje headless pipeline; jego `run_validation.py` potwierdza `--project YAML --output DIR`. Wykonano 3 rzeczywiste uruchomienia istniejącego `scaps_core/build/scaps_headless.exe`, exit=0, po 31/31 converged points. Core nie modyfikowano ani nie budowano; wszystkie inputy/outputy są w phase-11. Hash binarki przed/po identyczny: `core-binary.json`. Po początkowym oczekiwaniu na lock obliczenia CPU uruchomiono poza nim, zgodnie z zakresem locka: compilers/test executables/GPU.

YAML utworzono z saved `.def`: identyczne materiały, doping, neutral trap względem Ev, finite-S flatband contacts, T i napięcia; `constants_mode=scaps_compat`, constant mobility, fixed 401 points/layer. Porównanie I–V obejmuje 0…0,6 V; profil porównano tylko przy 0,6 V. Użyto istniejącego normalize i comparator, energy alignment `none`.

| Przypadek, 0,6 V | SCAPS J, A/m² | Core J, A/m² | Core−SCAPS | Endpoint comparator |
|---|---:|---:|---:|---|
| baseline | 3821,680790 | 3821,924086 | +0,006366% | PASS; max energy profile error 0,5812 meV |
| thin | 8974,378850 | 8974,871281 | +0,005487% | PASS; max energy profile error 0,1417 meV |
| low80 | 1,64907006e−23 | 3,71951224e−14 | factor 2,2555e9 | FAIL: IV mismatch; energie do 4,0081 meV |

Low80 SCAPS `J_total_rec`=3,66221651e−14 A/m², tylko 1,5645% poniżej Core. To wspiera hipotezę utraty dokładności terminal current SCAPS; nie dowodzi niezależnie poprawności fizyki Core. Dla danych low-T nie narzucać raw terminal current SCAPS jako golden truth. Max |ΔJ| Core−SCAPS w 0,1…0,6 V: baseline 0,24330, thin 0,49243, low80 3,7195e−14 A/m².

| quantity | expected (theory, formula) | SCAPS | diff | comment |
|---|---|---|---|---|
| Vbi baseline | VT ln(Na Nd / ni²)=0,8543583 V; ni²=Nc Nv exp(−Eg/VT), SCAPS k/q | 0,8537692 V z equilibrium Ec endpoints | −0,5890 mV | flatband homojunction; finite device |
| Eg baseline | Ec−Ev=1,12 eV | 1,12 eV | <1 µeV | kontrola parametrów i eksportu |
| Ideality baseline | n≈1 przy diffusion dominance | 1,021284 w 0,3…0,5 V | +2,13% | finite SRH/contact contributions |
| Current conservation baseline, 0,6 V | spatial spread/J≈0 | 7,3345e−6 | 0,000733% | przechodzi gate 1% |
| Current conservation doping, 0,6 V | spatial spread/J≈0 | 0,101217 | 10,12% | completed run nie przechodzi gate |
| Current budget forward, 1,66 V | J=J_total_rec w dark | względny błąd 0,896947 | 89,69% | osobny completed scan; przed formalnym failure |

Najważniejsze annoyances: GUI bez command status; convergence modal blokujący zapis; export profilu z failed bias; reported completion mimo złego bilansu. Cztery SCAPS-manual lookups zalogowano jako docs-needed; log zachowuje komunikaty failure. SST powinno zapisywać status per point, actual bias, last converged state, residual i current-conservation diagnostics obok danych.

Replay: `python runs/phase-11/generate.py`; przez GUI MCP uruchomić osobno `[ID-input.def, ID.script]`; dla forward preferować `forward-logged.script` lub warianty z appendtofile. Potem `equilibrium.script`, `python runs/phase-11/prepare_core.py`, komendy z `core-runs.json`, `python runs/phase-11/analyze.py`.

Build/check w tym jobie: `python -m py_compile` dla wszystkich 3 skryptów; bytecode w repo `runs/phase-11/__pycache__`. Pełna analiza z assertions, walidacja 949 punktów i trzech Core output contracts oraz endpoint comparator wykonane. PASS dotyczy integralności artefaktów; comparator low80 celowo FAIL. Wykres obejrzany. Bez C++ builda i pełnego test suite Core, ponieważ zakres nakazuje read-only use gotowej binarki. Bez git/dependencies/zmian produktu.

Nie sprawdzono freeze-out, Fermi–Dirac statistics, tunneling/quantum confinement ani self-heating; 2 nm i 2e19 cm⁻³ są tu stress tests modelu klasycznego. Tylko baseline/thin/low80 porównano z Core; nie wykonano pełnej mesh-convergence study ani niezależnej kontroli G.
Ryzyko: low-T, high doping i high forward wymagają quality flags; nie używać ich bezwarunkowo jako golden current reference.
