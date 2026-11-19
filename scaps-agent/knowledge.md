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

## Phase 2 ? I?V(T), retry 2026-11-19

- Start przez run_script([pe?na .def]) ?aduje n-Schottky poprawnie; timeout bez skryptu ko?cz?cego jest zamierzony. W tej sesji GUI nadal nie zmienia?o T/Set problem mimo active, hwnd, Enter i restartu. Dwa r?czne pomiary nie zosta?y wykonane; nie traktowa? input delivered jako wyniku.
- Sprawdzony skrypt runs/phase-2/series.script: dwa scripted pilots 200/360 K, clear simulations pomi?dzy nimi; nast?pnie calculate batch z temperature.sbf (T200?360 K,9 steps). Dark I?V ?0,4?+0,2 V/co0,005 V;9?121 punkt?w. .act i raw potwierdzaj? nastawy.
- Base n-schottky-input.def ??czy dok?adne phase1 n-schottky-gui-fixed.def z numerical settings z phase1 session-gui.scaps; zachowano back Sn1000 m/s. Nie u?ywa? starszego n-schottky.def z innymi kontaktami.
- Forward V<0. Fit log|J| do J0*expm1(Vforward/(nVT)), 0,12?0,24 V: n1,0200?1,05794. Richardson phi_eff0,7444066 eV, A_eff26,6097 A/cm?/K?, R?0,999999795. Aeff zale?y od okna24,84?27,92; nie jest intrinsic material constant. Equilibrium barrier0,75 eV w3T. Sze & Ng ?3.3.3 (transport limitation).
- Reverse200/220 K nierozstrzygni?te; raw zachowano, quality flag w CSV. Pilot/batch200 K reverse r??ni? si? do1,54e?9 A/m?; forward kontrolowany rtol2e?6,atol1e?11 A/m?. Nie fitowa? low-T reverse.
- Seria po zapisach exit0xC0000409, equilibrium script exit6. Ocenia? kompletno?? plik?w i status okien oddzielnie od exit code.
- runs/phase-2/analyze.py: wsp?lny numeric_rows z phase-GB, SciPy log-diode fit, SI packaging, self-checks i wykres. Py_compile i wykonanie PASS w retry. Trzy datasets phase-2-*; report.md i summary.json s? aktualne. Manuala nie u?yto.

## Phase 3 ? pr?ba ?adowania, 2026-11-19

- Load w pustym Solar Cell Definition otworzy?o file dialog po ponowieniu po hwnd i Enter. Nie wiadomo, kt?ry krok zadzia?a?. Alt+N nie da?o widocznego wpisu; jawny klik pola nazwy i pe?na ?cie?ka tak?e nie zako?czy?y load. Nie jest to potwierdzona receptura.
- close przy file dialog zwraca?o Still open; real_mouse click Anuluj, potem close zako?czy?y aplikacj?, windows=[].
- Dla n-Schottky z fazy1 V=Vleft?Vright: reverse V>0, slope 1/(C/A)^2 dodatni. Vbi=0,571421 V w300 K; w klasycznym przybli?eniu z korekt? Debye intercept wynosi ?(Vbi?kT/q). Fit tylko w odpowiednim re?imie reverse. Teoria, bez pomiaru: runs/phase-3/theory-only.json.
- Arrhenius ln(omega/T?) wymaga prefaktora emisji proporcjonalnego do T?. Sprawdzi? Nc(T), vth(T) modelu; sama etykieta 0,4 eV nie gwarantuje takiego prefaktora.

## Phase 4 — Rs/Rsh i seria I–V, 2026-11-19

- GUI load działa: środek load → wait na file dialog → jawny klik filename → Home/Shift+End → pełna ścieżka → klik Load. Natychmiastowy windows/read może wyprzedzić otwarcie dialogu. Potwierdzono load p-schottky-gui.def i dark I–V.
- Action Panel ma Rs/Rsh yes/no u góry. Na obrazie tej sesji Rs yes x518/y48, Rsh yes x788/y48; odświeżyć OCR przed kolejną sesją. Save all settings → filename → Enter → Add Comment → OK; skryptowy zapis all settings również otworzył modal.
- Obwód: Jterminal=Jdevice(Vinternal)+Vinternal/Rsh; Vterminal=Vinternal+Jterminal·Rs. Eksport ma napięcie terminalowe, więc zakres Vinternal−0,2…0,6 V może dać setki V przy dużym prądzie/Rs10. Porównywać przy wspólnym Vinternal/prądzie.
- `.def`: Series resistance:1 i Rs[Ohm.m2]:1e-4 odpowiada Rs1 Ω·cm²; Shunt conductance:1 i Gsh[S/m2]:100 odpowiada Rsh100 Ω·cm². Flagi0 wyłączają; SCAPS może pominąć wpisy OFF przy zapisie. Input i saved `.def` zachowano.
- `set external.Rs 0` odrzuca zero. OFF uzyskano przez flagę0 w input `.def`. Script set external używa Ω·cm²/S·cm⁻². **Zapisywać `.def` po calculate**: wcześniejsze zapisy pilota zachowały stare external circuit settings.
- Light: sprawdzono `action generationfrominternalmodel`, `load spectrumfile <absolute .spe>`, `action light`, `action intensity.T 100`, `action spectrumcutoff.off`. Nagłówek potwierdza AM1.5G100 mW/cm². Wcześniejszy wariant lightflux/spectrum_defined dawał pusty G(x); winnego polecenia nie ustalono.
- Pełne18 przypadków: `runs/phase-4/series.script`. Po wszystkich eksportach proces exit0xC0000409, windows=[] — przyczyna nieustalona. Nie uznawać kodu za sukces; pliki niezależnie sprawdzone przez `python runs/phase-4/analyze.py`: PASS w tej sesji, analiza Python bez locka.
- FF=Pmax/(Voc·Jsc): shunt przy dużym Rs może minimalnie podnieść FF mimo spadku Pmax. Rs10/Rshoff→Rsh100: FF25,6467→25,8046%, Pmax około19,62→19,21 W/m². Kolumny rekombinacji raw I–V nie obejmują external shunt.

## Phase GB — Batch i Recorder, 2026-11-19

- Batch Set-up → Add. Pierwsza lista zawiera warstwy/kontakty/illumination oraz **bezpośrednio T i V**, nie grupę working point. Dla tej jednej warstwy klik środka listy → Home, Down7, Enter wybierał T; liczba zależy od struktury. OCR może całkowicie pominąć T.
- `.sbf`: temperatura `Label 1 : T`, reszta `nihil`; doping `Label 1 : layer 1`, `Label 2 : ND`. Nd w cm⁻³, T w K. From250/To350/Steps3 daje250/300/350; Nd1e15→1e17/Steps3/Log daje1e15/1e16/1e17. Potwierdzać plikiem i wynikami. Błędne labels mogą cicho dać nihil albo modal ring2.
- Add drugi wiersz → simultaneous=0 daje nested product; w `nested-gui.sbf` Nd jest pierwszym parametrem, T drugim i szybszym. Potwierdzono3×3. Save Batch Settings również wymaga Add Comment→OK. Input: Home/Shift+End → wpis → Tab.
- Fallback do GUI: `run_script([full.def, setup.script])` bez quitscript pozostawia aplikację do pracy ręcznej; timeout jest wtedy zamierzony. `doping-gui-setup.script` ustawia I–V/C–V i poprawny batch. `temperature-batch-script.script` wykonuje prawdziwy T batch i kończy SCAPS.
- Record set-up → IV characteristics/all cell characteristics → Insert Below dodaje6 metryk. General properties → capacitance dodaje C w working point. `recorder-with-C.srf` zawiera komplet. Można ustawić po batch, lecz Calculate recorder liczy ponownie i sam dobiera pomiary zamiast zakresów Action List.
- N-Schottky przy bias lewego kontaktu ma forward/Voc<0. Recorder skanuje dodatni zakres, co dało modale i absurdalne Voc. GUI Set problem→apply voltage V to right contact→Save dało `.def` flagę+1 i poprawne9 light wyników. Znaki porównywać według nagłówków.
- Convergence modal blokuje batch; OK pozwala przejść dalej. Shift trzymany po OK zatrzymuje po członku; Stop batch assignment→Yes. Nieudany Recorder wyeksportował Voc8,28/6,04 V bez failed flag — finite nie oznacza valid.
- Recorder Results: lista Y wybiera jedną wielkość, lista X parametr batch; logX działa. Curve info ON→klik punktu podaje Nd,T,batch,definition i wartości. Kolory bez domyślnej legendy są niejednoznaczne. Save daje osobne dialogi `.iv` metryk i `.gnr` General properties; zwykły `.iv` ma inny schemat.
- I–V/C–V Save zapisuje wszystkie krzywe sesji, z batch/step/of i parametrami w nagłówkach. Nie potwierdzono single-member export w GUI. `save graph.recording.wholepanel`, `save graph.iv.wholepanel`, `save graph.cv.wholepanel` zapisują PNG; sprawdzony fallback do nieczytelnego GUI Save graph.
- **C–V raw jtot jest stale**: dla12 krzywych powiela pierwsze11 prądów wcześniejszego I–V przy innych bias. Clean CSV zawiera V,C/A,G/A, bez tych prądów. Raw Napp używa eps10 zamiast11,7. `analyze.py --check` sprawdza tę obserwację.
- Przy reverse+0,2 V lnJ daje Ea0,792841 eV, ln(J/T²) daje φeff0,742332 eV vs0,75. Nie utożsamiać fitów. C/A0≈sqrt[q eps N/(2(Vbi−kT/q))], Vbi=φB−(kT/q)ln(Nc/N); zmiana N zmienia Vbi, więc samo1/C²∝1/N jest przybliżeniem.
- `python runs/phase-GB/analyze.py --check`: PASS w tej sesji,7 datasets; INVALID i EXPLORATORY osobno. Python bez locka. Raport `runs/phase-GB/report.md`, liczby `summary.json`.
- Po plot/legend PID74384 utracił widoczne okna, lecz pozostał proces. gui.close widzi tylko widoczne okna; windows=[] nie dowodzi końca procesu. Nie ustalono przyczyny, bez kill. Sesja60612 zakończyła się po normal close z opóźnieniem.

## Phase G2 — pomiary GUI, 2026-11-19

- Potwierdzona sekwencja: Set problem→load→pełna ścieżka→Load→OK; Action Panel pokazuje pn-gui.def/OK; ponownie Set problem→OK, zaznaczyć I-V i kliknąć środek single shot po hwnd. Zadziałało; nie wyizolowano konieczności drugiego Set problem. Przyczyna wcześniejszego no-op nadal nieustalona.
- **Bezpośredni GUI export PNG działa:** save graphs→PNG/Whole Panel→małe OK(x361,y353 w tej sesji)→filename→Enter. OCR nie widzi OK. Sześć PNG w G2; aktualizuje wcześniejszą niepewność GB.
- T Batch dla dwóch warstw: pierwszy ring, klik środka→Home/Down8/Enter wybrał T; Down9 dawał V. Nie przenosić indeksu bez kontroli .sbf. From250/To350/Steps3/Lin potwierdzone plikiem.
- Combined I-V+C-V Batch wyeksportował70 DC points−0,8…0,58 V przy act I-V0…0,6 V/31 pts; samotny I-V poprawnie31 do0,6 V. Sprawdzać endpoint w raw. Save pamięta wcześniejsze single shots; wybierać batch/step/measurement.
- C-f może wyglądać na silnie zmienne przy span1,6e−7 wskutek autoscale i identycznych etykiet Y. Defekt neutralny nie gwarantuje widocznej dyspersji przy0 V.
- Abrupt p-n: C-V intercept≈Vbi−2kT/q (Sze & Ng§2.2 pp.83–85), inaczej niż wcześniejszy n-Schottky. Fit G2−0,8…−0,2 V dał Vbi0,90058 V vs0,90237 V. C0 z thermal tails zgodne do0,116%; nie jest to niezależny Vbi z bands.
- EB save dialog w G2 utknął mimo active, hwnd, Cancel/Esc/Alt+F4. Normal close nie zakończyło PID60804; run_script odmawia startu przy działającym SCAPS. Brak potwierdzonej naprawy. Nie uznawać dostarczenia input ani Responding=True za efekt UI. Pomiary zapisane, ale EB i pełne zamknięcie pozostają otwarte.


## Phase 3 retry — pomiary ukończone

Replay: run_script([absolute runs/phase-3/n-schottky-input.def, absolute runs/phase-3/series.script]). Osiem datasets, raw/.def/.act i CSV SI; analyze.py PASS po pełnym replay.

C-f: action cf.startf 1, stopf 1e7, points.per.decade 30, checkaction 1; save results.cf. C[nF/cm2]*1e-5 -> F/m2; G[S/cm2]*1e4 -> S/m2; Z[Ohm.cm2]*1e-4 -> Ohm.m2.

Pierwszy defect.add_default odrzucony; seed .def z wcześniejszego zapisu SCAPS działa. Script ustawia singleacceptor/singlelevel/belowEC, Et0.4,Ntotal1e15[cm^-3],sigma1e-15[cm2]. Saved .def potwierdza SI.

Manual3.5.2: Nc,Nv(T/300)^1.5,vth(T/300)^0.5. Ea z maksimum -dC/dlnf:0.402892eV; localDebye0.400488eV. f_inf/en*2pi=1.70...1.88. Mott-Schottky reverse+0.1...+0.4V,eps11.7:Nd9.9447e21,Vbi0.565289V.

GUI bez reakcji mimo active/restart/double Set problem/hwnd; skryptowy fallback. Post-export exit0xC0000409; close Closed/windows=[]. Pełne ograniczenia w report.md.
