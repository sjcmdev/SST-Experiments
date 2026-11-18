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
