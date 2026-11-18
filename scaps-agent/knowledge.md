# Knowledge


## Phase 0 retry — pipeline potwierdzony, 2026-11-18

Pulpit `locked`; obliczenia i eksport działają przez `gui.run_script`. Dwie niezależne sesje zakończyły się po 3,5/3,6 s, `exit=6`, bez pozostawionych okien. Ten kod wyjścia zaobserwowano przy poprawnych plikach; nie ustalono jego ogólnego znaczenia. Sukces oceniać po plikach i zamknięciu procesu.

### Script mode — sprawdzone przepisy

- I-V: uruchomić `gui.run_script(files=[pełna ścieżka CdTe-base.def, pełna ścieżka runs/smoke/smoke.script], timeout_s=55)`. Skrypt sam również ładuje definicję; kopia w `runs/phase-0/dark-iv.script`. Wynik: `runs/phase-0/dark.iv`.
- C-V: analogicznie `[runs/phase-0/CdTe-base.def, runs/phase-0/dark-cv.script]`, zawsze pełne ścieżki. Wynik: `dark-1MHz.cv`.
- Składnia: `command argument value`; części argumentu łączy kropka: `save results.iv PATH`, `save results.cv PATH`, `save settings.definitionfile PATH`, `set quitscript.quitSCAPS`. Średniki z `scriptdescription3312.txt` opisują schemat, nie gotowe polecenie.
- `set quitscript.quitSCAPS` ustawia zachowanie po końcu skryptu, może być na początku. Samo polecenie nie kończy wykonywania w tym miejscu.
- `clear actions` usuwa odziedziczone wybory. Potem `action dark`, `action workingpoint.temperature 300`, `action workingpoint.voltage 0`; dla C-V także `action workingpoint.frequency 1e6`.
- I-V: `action iv.startv 0`, `action iv.stopv 0.8`, `action iv.increment 0.02`, `action iv.checkaction 1`, `action iv.continueafterVoc`, `calculate singleshot`, eksport.
- C-V: `action cv.startv -1`, `action cv.stopv 0`, `action cv.increment 0.05`, `action cv.checkaction 1`, `calculate singleshot`, eksport. Plik ma kolejność 0 → −1 V mimo startV=−1 i stopV=0 w `.act`; nie zakładać porządku skanu z ustawień.
- Pełne ścieżki Windows bez cudzysłowów działają dla użytych plików. Manual opisuje wartości jako nazwy bez ścieżki; praktyczna próba potwierdziła pełne ścieżki. Nie testowano ścieżek ze spacjami.
- Zapisać `.act` przed obliczeniem, aby potwierdzić T, f, dark i wybrane akcje; `.def` dokumentuje urządzenie. Warunki pomiaru potwierdza też nagłówek wyników.
- Kontrola: `python runs/phase-0/check_results.py` (standard library, assertions): liczba/zakres punktów, skończone wartości, dark, T, częstotliwość C-V, monotoniczność I-V i dopasowanie.

### Zapisane formaty — odczyt z plików, bez OCR

Eksporty są tekstowe, rozdzielane tabulatorami i dopełniane spacjami, kropka dziesiętna, notacja wykładnicza. Czytanie `cp1252` zachowuje znak µ. Nagłówek o zmiennej długości: ścieżka i data skryptu, polecenie zapisu, wersja SCAPS, typ symulacji, numer single shot, czas, definicja, dark/light, T/K, V/V, f/Hz, konwencje znaków, następnie nazwy kolumn i dane. Puste wiersze występują przed i po tabeli; nie używać stałej liczby skiprows.

| Plik | Kolumny w kolejności | Jednostki |
|---|---|---|
| `dark.iv` (41 wierszy) | v; jtot; j_total_rec; j_total_gen; jbulk; jifr; jminor_left; jminor_right; j_SRH; j_Radiative; j_Auger | V; wszystkie j w mA/cm² |
| `dark-1MHz.cv` (21 wierszy) | v; C; G; W; Napp; jtot; jbulk; jifr; jopp_n; jopp_p; eps_s to determine W; eps_s to determine N | V; nF/cm²; S/cm²; µm; cm⁻³; pięć j w mA/cm²; dwa względne epsilon bez jednostki |

Napięcie lewego kontaktu względem prawego; prąd dodatni wpływa do lewego kontaktu; moc generowana = −V·J. C i G są znormalizowane do powierzchni, nie są całkowitą pojemnością/przewodnością urządzenia. J z eksportu podzielić przez 1000, aby uzyskać A/cm².

`.def` to tekst z sekcjami kontaktów, warstw i defektów; używa m, m⁻³, m²/(V·s), eV i względnego eps. Wiersze parametrów zawierają też wartości dotyczące grading, nie tylko jedną liczbę. Zapis SCAPS aktualizuje wersję/format starej definicji. `.act` to etykietowane wiersze warunków i akcji z jednostkami; nie mylić z definicją urządzenia.

### Fizyka i ograniczenia interpretacji

CdTe-base: CdTe 4 µm, eps=9,4, Na=2·10¹⁴ cm⁻³; CdS 0,025 µm, SnOx 0,5 µm. Przy 300 K dopasowanie ln(J[A/cm²]) = aV+b w 0,20–0,50 V daje n=1/(a·kT/q)=1,881619, J0_eff=exp(b)=2,416542·10⁻¹⁰ A/cm², R²=0,99998540. Jest to lokalne przybliżenie J≈J0·exp(qV/nkT), nie globalna ekstrakcja wszystkich parametrów diody. n blisko 2 jest zgodne z dominacją rekombinacji SRH widoczną w kolumnach prądu; J0 nie ma tu niezależnej wartości referencyjnej.

C(−1 V)=2,07619533 nF/cm², C(0 V)=2,07982588 nF/cm². Przybliżenie geometryczne samego CdTe eps0·9,4/(4 µm)=2,08073414 nF/cm²: różnica −0,218%. Model wszystkich warstw jako izolatorów w szeregu daje 1,83094 nF/cm² (+13,40% względem niego); nie jest trafnym modelem przewodzących okien. Zgodność z CdTe i płaski C-V sugerują niemal pełne zubożenie absorbera, bez dowodu z profilu pola.

SCAPS używa w analizie C-V stałego eps=10, a nie eps CdTe=9,4. Stąd W_app≈4,26 µm może przekraczać grubość CdTe. Napp=5,27·10¹⁵ do 4,07·10¹⁶ cm⁻³ nie odtwarza Na=2·10¹⁴ cm⁻³; przy niemal płaskiej C-V pochodna 1/C² jest mała, a interpretacja Mott-Schottky nie daje wiarygodnego profilu domieszkowania.

### GUI mode — przepis do wykonania po odblokowaniu

Poniższa mapa z pierwszej próby pozostaje użyteczna; dalsza nawigacja GUI nadal nieprzetestowana. Najpierw `desktop_active()`. Jeśli active: `launch`, `windows`, `read(diff=True)`; pomarańczowy Set problem wybierać współrzędnymi z ostatniego OCR, zawsze `real_mouse=True`. Załadować kopię `runs/phase-0/CdTe-base.def`; pola T/f oraz Dark i checkbox I-V lub C-V są w Action Panel. Uruchomić single shot, otworzyć właściwy panel wyników i odszukać zapis danych. Nazwy przycisków w panelach definicji/eksportu nie zostały potwierdzone. Po zapisie porównać pliki z formatami powyżej. Po błędzie wejścia ponownie sprawdzić `desktop_active()`, przy locked użyć sprawdzonych skryptów. OCR służy wyłącznie nawigacji; screenshot tylko gdy stan kontrolki lub wykres wymaga obrazu.

Poniższa sekcja zachowuje historyczną obserwację z pierwszej próby; stwierdzenia o braku wyników dotyczą wyłącznie tamtej próby.

## Phase 0 — mapa wstępna, 2026-11-18

Obserwacja GUI, bez udanego pomiaru. Poniższe ścieżki wymagają jeszcze praktycznego potwierdzenia. Liczby z OCR nie są danymi pomiarowymi.

| Panel / obszar | Co zaobserwowano | Status |
|---|---|---|
| SCAPS 3.3.12 Action Panel | Główny panel po uruchomieniu; drugie okno `scaps3312` jest ramką aplikacji. | Potwierdzone |
| Working point | Pola Temperature (K), Voltage (V), Frequency (Hz), Number of points. | Pola widoczne; edycja i zapis nieprzetestowane |
| Illumination | Dark/Light; spectrum lub G(x); model analityczny lub plik; kierunek oświetlenia, cut-off, Neutral Density. | Widoczne; wartości i stan niezweryfikowane plikiem |
| Set problem | Pomarańczowy przycisk u dołu po lewej. Osobne pole po prawej też zawiera napis Set Problem. | Panel definicji nie otworzył się |
| Action | Checkboxy I-V, C-V, C-f, QE; zakresy V1/V2 lub f1/f2, liczba punktów i increment. | Widoczne; uruchomienie nieprzetestowane |
| Calculate | single shot, batch, recorder, curve fitting; Execute script i Script set-up. | Widoczne; nieuruchomione |
| Results of calculations | Przyciski EB, G,R, AC, I-V, C-V, C-f, QE. | Widoczne; wykresy nieotwarte |
| Zapis i konfiguracja | Save all simulations; Load/Save Action List; Load/Save all settings. | Widoczne; format eksportu niepoznany |

### Przyszłe przepisy do potwierdzenia

- Urządzenie: `Set problem` → znaleźć load definition; przykłady istnieją w `D:\Aplications\SCAPS\install\def\`, m.in. `example Si.def`, `example CdTe.def`, `example CIGS.def`.
- Punkt pracy: pola Working point oraz przełącznik Dark/Light w Action Panel; po zmianie sprawdzić parametry w pliku zapisanym przez SCAPS.
- I-V: zaznaczyć I-V, ustawić zakres i krok, Calculate: single shot; sprawdzić przycisk wyników I-V. To plan z widocznego GUI, nie wykonany przepis.
- C-V: analogicznie checkbox C-V i zakres V1/V2; częstotliwość prawdopodobnie z Working point — wymaga potwierdzenia plikiem.
- Eksport: zbadać panel wyników oraz Save all simulations; nie ustalono jeszcze rozszerzenia, kolumn ani jednostek pliku.

### Ograniczenie sterowania

`gui.click(real_mouse=True)` początkowo zgłaszał dostarczenie kliknięcia bez zmiany okna, następnie dwukrotnie zwrócił `(258, 'SetCursorPos', 'Upłynął limit czasu operacji oczekiwania.')`. Enter także nie otworzył panelu. Nie ustalono przyczyny; nie ma dowodu na blokadę ekranu ani błąd solvera. `gui.read`, `windows` i `close` działały. Zwykłe zamknięcie potwierdzone pustą listą okien.

## SS3 — galeria paneli, 2026-11-18
- `desktop_active()` zwrócił `locked`: `launch`, `windows` i `screenshot` nadal działają. Action Panel sfotografowany w całości (1500 × 967 px), ale bez załadowanej definicji; nie traktować go jako referencji przykładu CdTe.
- `gui.screenshot` zapisuje PNG klienta do `gui-bridge/shots`; plik trzeba skopiować do katalogu galerii. Domyślny limit wynosi 5 na proces serwera (`GUI_BRIDGE_SHOT_BUDGET`), co nie wystarcza na pełny zestaw SS3. `0` wyłącza zrzuty, nie znosi limitu.

## Phase G — częściowo sprawdzona nawigacja GUI, 2026-11-19

Retry 2026-11-19 00:12–00:17 CET: Set problem → Add layer otwiera dwa wpisy Layer Properties. `windows` podaje hwnd; `read/click(title="hwnd:<topmost hwnd>")` działa. Tak samo w Defect Properties. Roli drugiego panelu nie ustalono.

- Edycja: klik pola → `{HOME}+{END}` → `type_text` → `{TAB}`. **Ctrl+A nie zastępowało treści**, również w file dialog; wpisy dopisywały się do starej nazwy/liczby. Poprawione parametry potwierdzono w `.def`.
- Warstwa: nazwa u góry, grubość w µm; Nd/Na w cm^-3; Add zatwierdza nową warstwę, Accept edycję istniejącej. Add a Defect otwiera edytor; domyślny neutral/single/Et=0,6 eV above EV/Nt=1e14 cm^-3/sigma=1e-15 cm² zapisano w p-base i potwierdzono plikiem.
- Kontakty: przyciski left/right contact → flat bands → OK. `.def` potwierdza `flatband:1`, `recalculate:1` dla obu kontaktów.
- Save w Solar Cell Definition → file dialog → pełna ścieżka w polu nazwy → Enter → **sprawdzić obecność i treść pliku przed close**. Poprawny przykład: `runs/phase-G/pn-gui.def` (dwie warstwy od zera).
- I-V zaznaczono i ustawiono zamierzony zakres 0–0,6 V co 0,02 V. Dwa kliknięcia single shot i Enter nie dały widocznej reakcji; przyczyna nieustalona, przepis solvera GUI nadal niepotwierdzony. Save Action List utworzyło pusty `.act` po szybkim close; nie jest to wiarygodny zapis warunków. Poczekać na zamknięcie dialogu i sprawdzić plik przed kolejną akcją.

SCAPS zamknięty, windows=[]. W tej próbie 3 screenshoty; bez manuala. Raport i czasy: runs/phase-G/report.md. C-V, C-f, temperature batch i eksporty pozostają nieprzetestowane przez GUI.

## Phase 1 — kontakty metal/semiconductor, GUI 2026-11-19

- Front to **right contact** przy x=d, back to **left contact** przy x=0. Przy V=Vleft−Vright i J wpływającym do lewego kontaktu forward dla n-Schottky ma V<0, dla p-Schottky V>0. Sprawdzono eksportami, nie OCR.
- Contact Panel: odznaczyć faktyczny checkbox flat bands (nie sam napis), dopiero wtedy edytować Metal work function. Przy flat bands pole jest disabled i wpis nie działa. Potwierdzić `flatband:0`, `Fi_m` w zapisanej .def. phi_B sprawdzić z Ec−Fn (n) lub Fp−Ev (p) na prawym końcu .eb.
- Single shot działa po kliknięciu środka przycisku. Action Panel screenshot z tej sesji: checkbox I–V x54,y499; Calculate x170,y740; wyniki I–V x904,y787. Współrzędne traktować jako historyczne — przed następną sesją odświeżyć OCR/obraz.
- I–V Panel → save zapisuje **wszystkie krzywe w pamięci**, po jednym nagłówku SCAPS / single shot / definition na blok. Nie zakładać, że pierwszy blok odpowiada aktualnej definicji; wybierać pełną nazwę definicji i single shot ID. Oryginały zachować.
- Pasy przy 0 V: Action → wyłączyć I–V (i inne akcje), workingpoint V=0 → single shot → Energy Bands → save data → .eb. Po skanie I–V EB pokazuje ostatni bias, nie equilibrium. Plik .eb ma zarówno workpoint voltage, jak i actual band bias.
- Save Action List i Save all settings: file dialog → ścieżka → Enter → **Add Comment to Output → OK** → sprawdzić niezerowy plik. Ten dodatkowy panel wyjaśnia dawny 0-bajtowy .act z phase-G; wtedy nie potwierdzono jego obecności.
- GUI .def z tej sesji nie zawiera convergence/mesh. Save all settings → .scaps zawiera pełną definicję z numerical settings i action list. Do odtworzenia fazy 1: session-gui.scaps → właściwa *-gui*.def → T/dark/zakres wg params.json; ponownie zapisać/zweryfikować warunki. Numerical settings w całej sesji nie zmieniano.
- Model fazy 1: 2 µm, Eg1.12, chi4.05 eV, eps11.7, Nc=Nv1e19 cm^-3, mu_n=mu_p50 cm²/Vs, N1e16 cm^-3, brak defektów. Front phi_m4.80/4.05 eV dla n, 4.42/5.17 dla p; back flatband. Cztery kompletne datasets `datasets/phase-1-*-gui`; failed initial scan ma osobny suffix incomplete i INVALID.
- Oba Schottky: phi_B0.75 eV, n_eff1.03677, J0_eff0.007447 A/m², rectification≈1603 przy ±0.2 V. J0 poniżej Richardson z A*=120 o72.58%; niewielka mobilność ogranicza transport. Sze & Ng §3.3.2–3.3.3 uzasadnia kontrolę thermionic-emission-diffusion; equilibrium integral daje0.007050 A/m². Nie wymuszać J0=Richardson, pomijając mobility/S/DOS.
- Raport i wykresy: runs/phase-1/report-gui.md, gui-bands.png, gui-iv.png. `python runs/phase-1/analyze_gui.py` regeneruje artefakty (analysis, bez lock); `--check` to test i wymaga build-lock.
