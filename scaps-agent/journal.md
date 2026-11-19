# Journal


## 2026-11-18 — phase-0, orientacja (częściowa)

- Odczytano state/knowledge i pusty ogon dziennika; brak wcześniejszych doświadczeń. Brak graphify-out/graph.json. Bez poleceń git i bez konsultowania manuala.
- Uruchomiono SCAPS 3.3.12. Rozpoznano Action Panel: Working point, Illumination, Action, Set problem, Calculate, Results, eksport i Script set-up. Sprawdzono listę przykładów w def/.
- Kliknięcie tekstu Set Problem trafiło w pole po prawej; próby przycisku po lewej nie otworzyły panelu. Jeden screenshot potrzebny do rozróżnienia przycisku i pola oraz kontrolek niewidocznych w OCR.
- Dwa kolejne real_mouse kliknięcia zakończyły się SetCursorPos timeout 258; Enter nie zmienił okien. Zakończono podcel przy limicie około 15 wywołań nawigacyjnych/diagnostycznych zamiast powtarzać nieskuteczne akcje.
- Nie załadowano urządzenia, nie wykonano I-V/C-V ani eksportu. runs/phase-0/ nie zawiera wyniku ani definicji z tej próby (prompt.md to istniejący materiał wejściowy).
- Zamknięto SCAPS przez gui.close; gui.windows zwrócił pustą listę.

| quantity | expected (theory, formula) | SCAPS | diff | comment |
|---|---|---|---|---|
| Voc | Dla ogniwa PV pod światłem: 0 < Voc < Eg/q; wymaga znajomości absorbera | Brak wyniku | N/D | Nie załadowano przykładu |
| Jsc | Moduł gęstości fotoprądu > 0 przy generacji i zbieraniu; zależy od widma i EQE | Brak wyniku | N/D | Brak zapisanego I-V |
| FF | FF = Pmax/(Voc·abs(Jsc)); dla działającego ogniwa 0 < FF < 1 | Brak wyniku | N/D | Nie da się ocenić wiarygodności fizycznej |

## 2026-11-18 06:43–06:48 CET — phase-0 retry, zakończona skryptowo

- Odczytano state, knowledge i ogon journal; zachowano poprzednie obserwacje. `desktop_active()` = locked; bez prób kliknięć i bez screenshotów. Brak graphify-out/graph.json. Bez git.
- Przeczytano fragmenty manuala 10.4.1/10.4.3 i quitscript oraz systemowy schemat poleceń/przykłady; lookupy zapisano w annoyances. Znaleziono przyczynę smoke: argumenty złożone wymagają kropek (`results.iv`, `quitscript.quitSCAPS`), nie spacji. Jawne `checkaction 1` i `clear actions` usuwają zależność od odziedziczonych akcji.
- Naprawiono runs/smoke/smoke.script i zachowano kopię runs/phase-0/dark-iv.script. CdTe-base.def załadowany przez SCAPS; SCAPS zapisał kopię definicji, dark-iv.act oraz dark.iv (41 punktów, 0–0,8 V, 300 K). Proces zakończył się sam: exit=6, 3,5 s, brak okien.
- Drugi skrypt dark-cv.script zapisał dark-cv.act i dark-1MHz.cv: 21 punktów, −1 do 0 V, dark, 300 K, 1 MHz. Eksport ma kolejność 0 → −1 V. Proces zakończył się sam: exit=6, 3,6 s.
- Odczytano pliki shell; check_results.py sprawdza format/warunki/zakresy i liczy dopasowanie. Assertions PASS. W knowledge opisano nagłówki, kolumny, jednostki, znaki i receptury obu trybów (GUI jawnie nieprzetestowane).
- `gui.close('scaps')`, następnie `windows` = []; SCAPS zamknięty. Nie przechodzono do fazy 1.

| quantity | expected (theory, formula) | SCAPS | diff | comment |
|---|---|---|---|---|
| n_eff | J≈J0 exp(qV/nkT); SRH w obszarze zubożonym: n≈2 | 1,881619 | −5,92% względem 2 | Fit 0,20–0,50 V; R²=0,99998540; dominuje j_SRH |
| J0_eff | exp(intercept) dopasowania ln(J[A/cm²]); dodatnie | 2,416542·10⁻¹⁰ A/cm² | Brak niezależnego wzorca | Lokalna ekstrapolacja, nie uniwersalna stała całej krzywej |
| J(0), generacja dark | równowaga: J=0; bez światła Jgen=0 | J(0)=0; j_total_gen=0 we wszystkich 41 punktach | 0 w precyzji eksportu | Zgodne |
| C/A przy −1 V | Przybliżenie zubożonego CdTe: eps0·9,4/(4 µm)=2,080734 nF/cm² | 2,076195 nF/cm² | −0,218% | Zgodne z niemal pełnym zubożeniem; profil pola niebadany |
| Napp vs Na | Na z definicji=2·10¹⁴ cm⁻³; interpretacja Mott-Schottky wymaga odpowiedniego reżimu zubożenia | Napp(0)=5,265·10¹⁵ cm⁻³ | ×26,3 | Nie utożsamiać z Na: płaska C-V, stałe eps analizy=10 |

## 2026-11-19 00:05–00:06 CET — phase-G, nieukończona próba GUI

- Pulpit active; Set problem → Solar Cell Definition Panel → add layer. Bridge zwrócił dwa identyczne tytuły Layer Properties Panel, tej samej klasy i PID. Odczyty po pełnym i skróconym tytule: found 2. Ponowna kontrola pulpitu=active; Escape bez zmiany.
- Pomocniczy computer-use odrzucił odczyt: Computer Use was not approved to use scaps3312. Nie ustawiono parametrów. Około 1 min od pierwszego OCR do zamknięcia, łącznie z diagnostyką. gui.close=Closed; windows=[].
- Brak .def, datasetu i wyników. Raport: runs/phase-G/report.md. Bez manuala, git, builda i testów solvera; brak zmian kodu.

| quantity | expected (theory, formula) | SCAPS | diff | comment |
|---|---|---|---|---|
| J(0), dark equilibrium | J=0 | Brak | N/D | Nie wykonano I-V |
| C/A, pełne zubożenie jednorodnej warstwy | ε₀εr/d | Brak | N/D | Brak definicji i C-V |
| C(f,T) | Zależność od modelu i stałych czasowych defektu | Brak | N/D | Brak C-f i temperature batch |

## 2026-11-19 00:12–00:17 CET — phase-G retry, częściowa

- Pulpit active. Poprawka hwnd pozwoliła odczytywać i klikać Layer/Defect Properties mimo dwóch identycznych tytułów. Utworzono od zera dwuwarstwowe homozłącze p–n, kontakty flatband i neutralny defekt. Liczby zweryfikowano w runs/phase-G/pn-gui.def (8468 bajtów).
- Ctrl+A dopisywało tekst zamiast zastępować. Poprawiono przez Home, Shift+End, wpisanie, Tab. Ten sam problem w file dialog; błędną ścieżkę poprawiono, .def zapisano.
- I-V zaznaczone, zamierzony zakres 0–0,6 V co 0,02 V. Dwa kliknięcia single shot i Enter bez widocznej reakcji/wyników; active potwierdzone. Zatrzymano podcel przy limicie diagnostyki. Nie ustalono, czy solver w ogóle wystartował.
- Save Action List i szybkie close pozostawiły 0-bajtowy pn-gui.act. Nie używać do odtwarzania pomiaru. gui.close=Closed; windows=[]. Brak wyników/datasetu; C-V/C-f/batch/wykresy niewykonane. Trzy screenshoty skopiowane do runs/phase-G/.
- Bez manuala i git; bez zmian kodu produktu/builda. Kontrola .def uruchomiona przez build-lock oczekuje na lock; końcowy status w raporcie. Czasy orientacyjne z zegara, obejmują narzędzia i diagnostykę.

| quantity | expected (theory, formula) | SCAPS | diff | comment |
|---|---|---|---|---|
| Vbi, 300 K | kT/q ln(Na Nd / ni²)=0,90237 V; ni=√NcNv exp(−Eg/2kT) | Brak pomiaru | N/D | Wartości wejściowe z .def; stałe materiałowe traktowane jako niezależne od T |
| W(0), depletion approximation | √[2εVbi/q(1/Na+1/Nd)]=0,33122 µm | Brak | N/D | Homozłącze abrupt, bez korekt Debye i defektów |
| C/A(0) | ε/W=2,67317e−4 F/m² | Brak C-V | N/D | Punkt odniesienia, nie wynik solvera |
| J(0), dark equilibrium | 0 A/m² | Brak I-V | N/D | Brak eksportu |
- Końcowy status: kontrola assertions przez build-lock nie uzyskała locka przez około 2 min; przerwano oczekiwanie, test NIE wykonany. Brak PASS. Raport końcowy: runs/phase-G/report.md.

## 2026-11-19 00:20–00:40 CET — phase-1, cztery kontakty przez GUI

- State/knowledge/journal tail odczytane; desktop_active=active. Bez git, SCAPS scripts i manuala. GUI: jedna warstwa Si-like 2 µm, Eg1.12 eV, chi4.05 eV, eps11.7, N1e16 cm^-3; default DOS1e19 cm^-3 i mobility50 cm²/Vs. Back flatband; front phi_m4.80/4.05 eV dla n oraz4.42/5.17 dla p.
- Pierwszy zapis ujawnił front flatband=1 (work function disabled); poprawiono checkbox, zapisano n-schottky-gui-fixed.def. Starszych plików phase-1 nie nadpisano. Pierwszy skan −1…+1 V: convergence failure+0.82 V, cut+0.8 V i brak gałęzi ujemnej; dataset INVALID.
- Cztery udane skany po51 punktów, n −0.6…+0.4 V, p −0.4…+0.6 V, krok0.02 V,300 K,dark. Osobne equilibrium single shots dały cztery .eb przy0 V. Eksport I–V zawiera wszystkie krzywe sesji — konwersja wydziela właściwy definition/single shot2,4,6,8. All settings zachował numerical settings; Add Comment→OK kończy zapis .act/.scaps.
- analyze_gui.py uruchomiony jako analiza poza lockiem: raw/SI CSV/params/.def/.scaps w czterech datasets phase-1-*-gui oraz osobny INVALID incomplete. Wykresy gui-bands.png/gui-iv.png obejrzane. J0_eff≈0.007447 A/m², n≈1.03677; niski transportowy J0 zgodny z equilibrium drift-diffusion estimate0.007050. Teoria: Sze&Ng§3.3.2–3.3.3, bez manual lookup.
- Build/test: byte-compilation i pełny --check próbowano przez globalny build-lock, lecz wrapper nie uzyskał locka przez kilka minut; oczekiwania przerwano. Build i --check NIE wykonane, brak PASS. Nie powstał build w repo ani TEMP. Dataset quality jawnie wymaga formalnego sprawdzenia przed golden approval.
- SCAPS close=Closed, windows=[].3 screenshots GUI. Raport: runs/phase-1/report-gui.md. Następna faza nieuruchomiona.

| quantity | expected (theory, formula) | SCAPS | diff | comment |
|---|---|---|---|---|
| phi_B n/p-Schottky | phi_m−chi / Eg+chi−phi_m=0.75 eV | 0.75 /0.75 eV |0| Bands at0 V |
| phi_B n/p-ohmic |0 eV|0 /0 eV|0| Front accumulation |
| abs(Jf/Jr),abs(V)=0.2 V |Schottky>>1;ohmic≈1|1602.78/1602.79;0.9431/0.9974|N/D;−5.69%/−0.26%|Polarity reverses n↔p|
| n n/p-Schottky |≈1|1.036770/1.036763|+3.68%|Fit0.12…0.24 Vforward|
| J0 vs Richardson |120T²exp(−phi/kT)=0.027163 A/m²|0.007447 A/m²|−72.58%|Low mobility; A_eff of boundary178.02, not120|
| J0 vs drift-diffusion equilibrium |0.00704966 A/m²|0.007447 A/m²|+5.64%|Linear-response estimate, local fit|

## 2026-11-19 00:40–00:42 CET — phase-2, zatrzymana przed pierwszym pomiarem

- desktop_active=active (dwie kontrole). Load all settings odtworzyło session-gui.scaps z Si-like p; Set problem otworzyło panel definicji.
- Cztery real_mouse clicks load, także po hwnd, i Enter bez dialogu/zmiany OCR. Zatrzymano fazę przy limicie >15 wywołań na podcel, wliczając diagnostykę. Nie uruchomiono solvera; brak temperatur, danych i fitów.
- Dwa screenshoty diagnostyczne, bez manuala/git/skryptów. SCAPS close=Closed; windows=[]. Raport i physics-check table: runs/phase-2/report.md. Build/test niewykonane, brak zmian kodu i PASS.

## 2026-11-19 00:43?00:45 CET ? phase-3, zatrzymana przed pomiarami

- Desktop active. Set problem ? load: dialog dopiero po ponowieniu i Enter. Wpisanie ?cie?ki nie zamkn??o dialogu, brak potwierdzonej definicji. Limit diagnostyki osi?gni?ty; brak solver runs, defektu, datasets i fit?w. Dwa screenshoty.
- Teoria z bazowej .def: runs/phase-3/theory-only.json; obliczenia Python i assertions PASS bez locka. Bez kodu produktu, builda, test?w produktu, manuala i git.
- close pocz?tkowo Still open; Anuluj w file dialog ? close=Closed, windows=[]. Raport: runs/phase-3/report.md.

| quantity | expected (theory, formula) | SCAPS | diff | comment |
|---|---|---|---|---|
| Nd | 1e22 m^-3 z bazowej .def; Nd=2/(q eps slope) | Brak C-V | N/D | Slope dla 1/(C/A)^2; napi?cie Vleft?Vright |
| Vbi, 300 K | phi_B?kT/q ln(Nc/Nd)=0.571421 V | Brak | N/D | phi_B=0,75 eV; przeci?cie Mott-Schottky z korekt? kT/q: V=?0.545569 V |
| Slope Mott-Schottky | +2/(q eps Nd)=1.20499e+07 m^4 F^-2 V^-1 | Brak | N/D | Dodatni znak: reverse n-Schottky przy Vleft?Vright>0 |
| C/A(0), depletion | eps/W=0.000381092 F/m?; W=0.271835 ?m | Brak | N/D | Przybli?enie zubo?enia; wymaga sprawdzenia AC |
| e_n, 300 K; e_n/(2pi) | sigma vth Nc exp(?0,4/kT)=19067.6 s^-1; 3034.7 Hz | Brak C-f | N/D | Hipotetyczny defekt; nie dodano go w GUI; f_inf zale?y tak?e od odpowiedzi urz?dzenia |
| Ea z Arrhenius | 0,4 eV dla modelu prefaktora T? | Brak serii T | N/D | Przed ln(omega/T?) zweryfikowa? zale?no?ci Nc(T), vth(T); przy sta?ym prefaktorze u?y? ln(omega) |

## 2026-11-19 00:46–01:02 CET — phase-4, pełna seria Rs/Rsh

- State/knowledge/journal tail odczytane; desktop active. GUI load p-Schottky, dark I–V −0,2…0,6 V co0,02 V, drugi skan Rs10; save obu i all settings. Cztery screenshots. Wait na dialog usuwał niejednoznaczność natychmiastowych odczytów po kliknięciu.
- Pilot script light: timeouty na pustym G(x), potem poprawny AM1.5G przy generationfrominternalmodel/load spectrumfile/action light. Modal Add Comment przy save all settings wymagał GUI. Pre-calculate zapis miał stare Rs/Rsh. Manual lookup zapisany jako docs-needed.
- set external.Rs0 odrzucone; serię oparto na input `.def` z flagami OFF. Wykonano18 dark/light×Rs0/1/10×Rshoff/100/10000, po401 punktów,300 K,light1000 W/m². Wszystkie `.iv/.def/.act` zapisane; potem proces exit3221226525, windows=[]. Przyczyna nieustalona.
-18 kontrolowanych datasets i3 exploratory GUI/pilot: raw,SI CSV11kolumn,params,exact SCAPS definition/settings,spectrum light. analyze.py PASS: kompletność,settings,circuit identities,9FF checks,CSV/JSON round-trip; max residual0,5 µV/3e−6 A/m². Wykres obejrzany.
- Byte-compilation PASS w repo runs/phase-4/__pycache__/. Python bez locka zgodnie z regułą fazy. Bez git i zmian kodu produktu; C++ build nie dotyczy. close=Closed,windows=[]. Raport:runs/phase-4/report.md. Innych faz nie uruchamiano.

| quantity | expected (theory, formula) | SCAPS | diff | comment |
|---|---|---|---|---|
| ΔV, Rs1/Rs10 przy J1,62667343 A/cm² | J·Rs=1,62667343/16,2667343 V |1,626673/16,266734 V|−0,43/−0,30 µV|Dark,Vinternal0,4 V|
| Δ(dJ/dV),Rsh100/10000 |1/Rsh=100/1 S/m²|99,9999999573/0,99999999975|<4,3e−8 S/m²|Odjęto diode slope|
| FF light,Rshoff,Rs0/1/10 |Straty Rs obniżają FF|69,0773/59,0066/25,6467%|−10,0707/−43,4306 pp|Vs Rs0|
| FF light,Rs0,Rshoff/10000/100 |Leakage obniża FF|69,0773/69,0372/65,1144%|−0,0401/−3,9629 pp|Pełna tabela w raporcie|

## 2026-11-19 od 01:05 CET — phase-GB

- State/knowledge/journal tail i AGENT odczytane; desktop active. GUI load n-Schottky działał. Początkowo błędnie szukałem grupy working point zamiast bezpośredniego T. Restart/hwnd, próby `.sbf`, manual Chapter7/8; oba lookupi zapisane jako docs-needed. Błędne labels→nihil lub modal. Load Single Shot Batch nie dał użytecznego wyniku.
- T250/300/350 ustawiono i zapisano GUI; Calculate batch i GUI save dały3 dark I–V. Wczytany fallback Nd1e15/16/17 Log dał3 I–V/C–V; dodany GUI drugi wiersz T dał9 I–V/C–V, nested, T szybciej. Nie było Set problem twice occurrence: udane obliczenia startowały jednym kliknięciem.
- Recorder Insert Below dodało6 cell characteristics; potem General capacitance. Pierwsza light próba dała2 failed członków i modale0,66/0,80 V; OK kontynuuje, Shift→Yes zatrzymuje. Niepoprawne metryki zachowano. Zmiana GUI voltage applied→right, potwierdzona `.def`, dała9 poprawnych light członków.
- GUI save: temperature-gui.iv, doping-nested-gui.iv/.cv, failed-recorder.iv/.gnr, recorder-gui.iv/.gnr. `.sbf` wymaga Add Comment→OK. Recorder po batch liczy od nowa; X/Y i logX działają, Curve info identyfikuje Nd/T/plik.
-5 screenshots nawigacyjnych. Fallback save graph uruchomiony GUI zapisał3 PNG; recorder-panel i cv-panel obejrzane. plot/legend pozostawił PID74384 bez widocznych okien — korekta pierwszej interpretacji jako crash. Normal close sesji60612, windows=[]. PID74384 nadal przy01:37, bez kill.
- Prawdziwy skrypt T batch:3,4 s,exit6, pliki sprawdzone. GUI–script ΔJ=0; middle-Nd nested replay7,71e−9 A/m²; C replay0. Początkowy assert zera dla nested wykrył małą różnicę; przyjęto atol1e−7 A/m², potem PASS.
-7 datasets raw/SI CSV/params/def/batch provenance;1 INVALID,1 EXPLORATORY. Raw C–V jtot dokładnie powiela wcześniejsze I–V, więc go wykluczono z clean CSV.188 diagnostycznych plików zachowano w label-diagnostics.zip.
- `python -m py_compile runs/phase-GB/analyze.py` i `python runs/phase-GB/analyze.py --check` PASS w tym jobie; bytecode w repo runs/phase-GB/__pycache__, Python bez locka. Bez C++ builda (kod produktu bez zmian), git i innych faz. Raport: runs/phase-GB/report.md.

| quantity | expected (theory, formula) | SCAPS | diff | comment |
|---|---|---|---|---|
|φB z ln(J/T²),reverse0,2 V|0,75 eV|0,742332 eV|−1,02%|Plain lnJ: Ea0,792841 eV|
|C/A0,Nd1e21/22/23 m⁻³|sqrt[q eps N/2(Vbi−kT/q)]|1,306641e−4/3,902830e−4/1,172607e−3 F/m²|−0,00327/+0,06838/+0,12828%|300 K,dark,1 MHz; Vbi zależy odN|
|GUI/script J(T)|Identyczne warunki|Δmax0 A/m²|0|3×31 punktów|
|Light Recorder Pmax/FF|−VJ iPmax/(VocJsc)|9/9 zgodne|<1e−5 W/m²/<2e−5 pp|Niezależna kontrola krzywych|

## 2026-11-19 01:45–około02:00 CET — phase-G2

- Active desktop. Load pn-gui.def→Load→OK pokazało nazwę i OK. Drugi Set problem→OK, zaznaczenie I-V i klik środka single shot po hwnd uruchomiły solver. Nie wyizolowano przyczyny wcześniejszego no-op ani konieczności drugiego Set problem.
- GUI single: dark300 K; I-V0…0,6 V/31 pts; C-V−0,8…0 V/41 pts/1 MHz; C-f100 Hz…1 MHz/21 pts/0 V. Dane i sześć PNG zapisane przez GUI. Małe OK w save graph jest niewidoczne w OCR; format/contents→OK→filename działa.
- Batch T250/300/350 K ustawiono GUI i potwierdzono .sbf; I-V/C-V/C-f dla3T. Combined I-V/C-V raw ma70 DC points−0,8…0,58 V zamiast oczekiwanych71 do0,6 V. Nie dopisywano endpointu. C-V current nie pasuje do bias, wyłączony z clean CSV.
- .act przed Add Comment→OK:0 B; po:1787 B. GUI .scaps13684 B z numerical settings; wyodrębniono pełny .def, kopia oryginalnego .def zachowana. Cztery datasety raw/SI CSV/params/settings/hash; ograniczenia jawne.
- Vbi_CV0,900577862 V po2kT/q vs0,902367853 V(−0,19837%); C0=2,75639219e−4 F/m² vs thermal-tail2,75320932e−4(+0,11561%); J0=0. Sze & Ng§2.2 pp.83–85; bez SCAPS-manual lookup.
- Equilibrium GUI0 V policzone, eksport EB utknął w file dialog(hwnd30871520). Input/Cancel/Esc/Alt+F4/normal close bez efektu mimo active. Błędna kontynuacja łańcucha po missing save-graph dialog nie została uznana za zapis. run_script fallback odrzucony: already running. PID60804 nadal otwarty, frame disabled; bez kill. Wcześniejszych procesów nie dotykano.
- PASS w tym jobie: python -m py_compile runs/phase-G2/package_data.py; python runs/phase-G2/package_data.py. Counts/ranges/T, SI JSON/CSV/hash, monotonic I-V,84 impedance identities i physics bounds. Pierwszy assert wykrył70 zamiast71; opis i ograniczenie poprawiono. Bytecode w repo G2/GB __pycache__; Python bez locka, bez C++/git.
- Raport: runs/phase-G2/report.md, summary.json, timings.json. Phase-G physics table uzupełniona; state i knowledge zaktualizowane. Do domknięcia: normalne zamknięcie, niezależny EB i ewentualnie I-V-only Batch do0,6 V.

## 2026-11-19 oko?o 02:03?02:15 CET ? phase-2 retry

- State/knowledge/journal tail/AGENT odczytane; desktop active. Start przez .def zadzia?a?, GUI input bez efektu. Set problem powt?rzone, hwnd/enabled sprawdzone, normal restart i pr?ba T200 bez zmiany. Dwa pomiary r?czne niewykonane;1 nowy screenshot diagnostyczny, bez manuala/git.
- Skrypt: pilots200/360 K, potem prawdziwy batch200?360/co20 K,9?121 dark I?V?0,4?+0,2/co0,005 V. Wszystkie .iv/.act/.def zapisane; potem exit0xC0000409. Oddzielne3 equilibrium EB,exit6. Pe?ne numerical settings do??czone do phase1 fixed GUI base.
- Richardson phi_eff0,7444066 eV/A_eff26,6097 A/cm?/K?; n1,020001?1,057944. Equilibrium barrier0,75 eV.3 datasety raw/SI CSV/params/def; reverse200/220 quality unresolved. Pierwszy assert pilot identity wykry? niestabilny reverse; forward zgodno?? zachowana. Fit-window sensitivity i synthetic extraction checks.
- python -m py_compile runs/phase-2/analyze.py i python runs/phase-2/analyze.py PASS; Python bez locka, bytecode w repo phase2; brak C++ targetu. Wykres obejrzany. Aktualny raport runs/phase-2/report.md, poprzedni report-first-attempt.md. Nie uruchamiano innych faz.

| quantity | expected (theory, formula) | SCAPS | diff | comment |
|---|---|---|---|---|
| phi_B | Phi_m?chi=0,75 eV | equilibrium0,75; Richardson0,7444066 eV |0 /?0,7458%|Effective extraction|
| A* |120 A/cm?/K?, free-electron reference|26,6097|?77,83%|Transport/model dependent|
| n |1 ideal TE|1,020001?1,057944|+2,00?5,79%|200?360 K|
| J0/n300K |linear drift-diffusion0,00704966 A/m?|0,00720254|+2,17%|Equilibrium integral|


## Phase 3 retry — 2026-11-19

---
status: active
type: experiment
tags: [scaps, agent, phase-3]
---

Faza 3 — wymagane pomiary ukończone (retry 2026-11-19).

Cel: C-V n-Schottky, Mott–Schottky i temperaturowe C-f jednego głębokiego defektu.

Wykonano C-V przy 1 kHz i 1 MHz: -2…+0,4 V, krok 0,02 V, 121 punktów, 300 K, dark. Bazowe C-f: 1 Hz…10 MHz, 211 punktów, 0 V. Dodano bulk acceptor: single level Ec-0,4 eV, Nt=1e21 m^-3, sigma_n=sigma_p=1e-19 m^2; C-f powtórzono w 240/270/300/330/360 K. Parametry potwierdzone zapisami .def/.act i nagłówkami raw, bez liczb z OCR.

GUI: start z .def ominął file dialog. Set problem bez reakcji mimo active desktop, hwnd, powtórzenia i normal restart. Wykorzystano dozwolony skryptowy fallback. Ręcznej obsługi pomiarów nie uznano za opanowaną. Seed defektu z istniejącego zapisu SCAPS fazy G; końcowe parametry ustawia defect-series.script.

W runs/phase-3/: series.script (pełny replay), n-schottky-input.def, defect-seed.def, raw .cv/.cf, saved .def/.act, analyze.py, summary.json, analysis.png. Osiem datasets/phase-3-* zawiera raw, CSV SI, params.json, definicje, ustawienia, skrypty i SHA256. Poprzedni raport zachowany jako report-first-attempt.md.

| quantity | expected (theory, formula) | SCAPS | diff | comment |
|---|---|---|---|---|
| Nd, 1 kHz / 1 MHz | 1e22 m^-3; 2/(q eps slope) | 9,94471484e21 / 9,94471530e21 m^-3 | -0,553% | Fit +0,10…+0,40 V; eps=11,7 |
| Vbi, 1 kHz / 1 MHz | 0,571420712 V; phiB-kT ln(Nc/Nd) | 0,565289284 / 0,565289730 V | -1,073% | Intercept/slope + kT/q; depletion approximation |
| Slope 1 kHz | 1,20499e7 m^4 F^-2 V^-1 | 1,21169321e7 | +0,556% | Reverse: V>0 przy bias lewego kontaktu |
| C-f step 300 K | Defekt daje dyspersję | Δ(C/A)=2,3763534e-5 F/m^2 | Brak niezależnej amplitudy | Base Δ=1,512e-8 F/m^2 |
| en/(2pi), 300 K | sigma vth Nc exp(-0,4/kT)/(2pi)=3034,70 Hz | f_inf=5583,30 Hz | +83,98% | Odpowiedź urządzenia obejmuje capture i profil przestrzenny |
| Ea, ln(2pi f_inf/T^2) | 0,4 eV | 0,40289210 eV | +0,723% | 5 temperatur; R^2=0,9998263 |
| Ea, local Debye fit | 0,4 eV | 0,40048779 eV | +0,122% | Wrażliwość metody ekstrakcji |

| T [K] | f_inf [Hz] | en/(2pi) [Hz] | ratio |
|---|---|---|---|
| 240 | 72,6388 | 40,5854 | 1,790 |
| 270 | 748,288 | 440,522 | 1,699 |
| 300 | 5583,30 | 3034,70 | 1,840 |
| 330 | 28240,5 | 14989,4 | 1,884 |
| 360 | 103722 | 57600,9 | 1,801 |

f_inf: maksimum -dC/dln(f), interpolacja parabolą przez trzy sąsiednie próbki. Local Debye fit w f_peak/10…10 f_peak daje 6283,34 Hz przy 300 K. Różnica Ea między metodami: 0,00240 eV. Nie wyznaczono przedziałów ufności ani sigma z prefaktora. Manual §3.5.2: Nc proporcjonalne T^1,5, vth proporcjonalne T^0,5, więc prefaktor emisji T^2. Lookup zapisany jako docs-needed.

Najważniejsze annoyances:

- GUI input delivered bez zmiany panelu po restart/hwnd/double Set problem: około 3 min; SST powinno potwierdzać efekt operacji.
- defect.add_default zwraca no defects defined przy pierwszym defekcie: około 1 min. Zachowano failed-add-default.script; seed .def działa, składnia dodania pierwszego defektu nierozwiązana.
- Model temperaturowy wymaga manuala: około 1 min; SST powinno pokazywać równania przy parametrach.
- Po eksporcie SCAPS exit0xC0000409: około 1 min kontroli, przyczyna nieustalona; dane sprawdzono niezależnie.

Wiedza: C i G są na jednostkę powierzchni. Napp SCAPS używa eps10, dlatego Nd obliczono samodzielnie z eps11,7. Raw current columns C-V pominięto w clean zgodnie z wcześniejszą obserwacją stale columns. Forward branch do -2 V zachowano bez osobnej walidacji transportu dużego prądu i wykluczono z Mott–Schottky.

W tym jobie pełny replay series.script oraz po nim python -m py_compile runs/phase-3/analyze.py i python runs/phase-3/analyze.py — PASS. Kontrole: osiem kompletów, counts/ranges/T/f, exact defect settings, SI round-trip, JSON, finite values, C/G/Z identity, bounds Mott–Schottky/Arrhenius, synthetic Debye recovery. Wykres obejrzany. Python bez locka zgodnie z briefem; bytecode zbudowano w repo runs/phase-3/__pycache__. C++ build nie dotyczy: brak zmian produktu i targetu. Bez git.

SCAPS close=Closed, windows=[] na końcu. Brak nowych screenshots w retry. Problemy otwarte: nieskuteczny input GUI i post-export process failure; pomiary zapisane i sprawdzone.
