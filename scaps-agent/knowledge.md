# SCAPS cookbook — 3.3.12

Przepisy końcowe z faz 0–11; synteza 2026-11-19. [Historia](journal.md) i
[pełne archiwum knowledge](runs/phase-12/knowledge-before.md).
[Katalog danych](datasets/CATALOG.md) · [capability map](runs/phase-12/capabilities.md) ·
[top 10 pain points](runs/phase-12/pain-points.md).
GUI potwierdzono m.in. w fazach 1/4/GB/G2; późniejsze serie wymagały script fallback.
Nie wszystkie kroki GUI działały niezawodnie w każdej sesji.

## Start, edycja, recovery

1. `desktop_active()`; active → launch/windows/read(diff=True). Po awarii input sprawdzić ponownie.
   Locked wyklucza klikanie/typing; script mode korzysta ze sprawdzonych skryptów.
2. Click zawsze `real_mouse=True`, x/y z bieżącego OCR. Dwa identyczne tytuły → `hwnd:<hwnd>`.
   OCR służy nawigacji, gubi kropki i nie pokazuje checkbox state; parametry weryfikować plikiem.
3. Set problem → load → poczekać na dialog → filename → `{HOME}+{END}` → wpis → Load → OK.
   Jeśli full path zawodzi, po nawigacji do folderu wpisać tylko filename. Fallback:
   `run_script(files=[absolute.def])`; bez kończącego skryptu timeout celowo zostawia GUI.
4. Liczby: klik → `{HOME}+{END}` → type_text → `{TAB}`; Ctrl+A potrafiło dopisywać tekst.
   Wyłączyć rzeczywisty checkbox flat bands przed edycją metal work function.
5. Ustawić potrzebną akcję, T/V/f i dark/light. Single shot; przy no-op ponownie Set problem → OK
   → single shot. Potwierdzać nowy wynik, nie input delivered.
6. Po >15 wywołaniach bez postępu: log annoyance, normal close/restart, windows/modal/hwnd,
   dwa Set problem, startup z .def, script series. Nie powtarzać ślepo tych samych kliknięć.

## Zapis i tożsamość pomiaru

- I–V/C–V Save zapisuje wszystkie krzywe pamięci. Wybierać definition + shot/batch/step/T.
  `.iv` Recorder ma inny schemat niż zwykłe I–V.
- Save Action List / Save all settings / Batch settings: filename → Enter → Add Comment → OK.
  Przed close odczytać niezerowy plik. All-settings export w skrypcie też otwierał modal,
  nawet fully_suppressed; sprawdzona .def/.act z numeryką wystarcza do replay.
- Pakiet: raw, input i post-calculate .def, .act, optics/batch/script, clean CSV SI, params.json,
  quality flags i SHA256. GUI .def fazy 1 nie zawierało numeryki: jest w session-gui.scaps.
  Późniejsze saved .def ją zawierają; sprawdzać treść. .sbf nadpisuje parametry per member.
- Equilibrium EB: wyłączyć skany, V=0, single shot, Energy Bands → save data; sprawdzić actual bias.
  Po skanie EB jest przy końcowym/attempted bias, a po failure może być failed iterate.
- PNG: save graphs → PNG/Whole Panel → małe OK → filename → Enter działało w G2, OCR pomijało OK.
  `save graph.iv.wholepanel PATH` jest sprawdzonym alternatywnym eksportem.

## Jednostki i znaki

| Native quantity | Conversion / meaning |
|---|---|
| J [mA/cm²] | ×10 → A/m²; I=J×area_m2, jawna powierzchnia |
| C [nF/cm²] | ×1e-5 → F/m²; C/A, nie całkowite C |
| G [S/cm²] | ×1e4 → S/m²; zachować signed raw |
| Z [Ohm cm²] | ×1e-4 → Ohm m²; Z_area=1/(g+jωc) |
| Rs/Rsh [Ohm cm²] | ×1e-4 → Ohm m² |
| Density / cross section | cm^-3 ×1e6 → m^-3; cm^-2 ×1e4 → m^-2; cm² ×1e-4 → m² |
| Energy | eV ×q → J; natywne parametry oznaczać eV |
| Default voltage/current | Vleft−Vright; J dodatni wpływa do left; generated power=−VJ |

Front=right przy x=d, back=left przy x=0. n-Schottky forward V<0, p-Schottky V>0.
Recorder GB wymagał apply voltage to right contact: zawsze czytać konwencję nagłówka.
.def używa głównie SI i eV; script defect Nt/sigma i external Rs/Gsh używają cm-based units.
Raw ma zmienną długość nagłówka i zwykle cp1252; parser ma odnaleźć nagłówek tabeli.

## Script mode i serie

Sprawdzony szablon I–V: PATH zastąpić pełną ścieżką w runs/phase-N.

```text
set quitscript.quitSCAPS
clear actions
action dark
action workingpoint.temperature 300
action workingpoint.voltage 0
action iv.startv 0
action iv.stopv 0.6
action iv.increment 0.02
action iv.checkaction 1
action iv.continueafterVoc
clear simulations
calculate singleshot
save results.iv PATH.iv
save settings.definitionfile PATH.def
save settings.actionlistfile PATH.act
```

- `gui.run_script(files=[absolute.def, absolute.script], timeout_s=...)`. Dotted commands,
  spacje między argumentami; średniki scriptdescription opisują schemat. quitscript ustawia
  zachowanie na końcu, nie przerywa skryptu w tej linii. clear simulations usuwa stare wyniki.
- Po clear actions jawnie ponawiać T/V/f/dark. C–V: action cv.startv/stopv/increment/checkaction
  i workingpoint.frequency; save results.cv. C–f: action cf.startf 1, stopf 1e7,
  points.per.decade 30, checkaction 1; save results.cf. [Replay fazy 3](runs/phase-3/series.script).
- Light: generationfrominternalmodel, load spectrumfile, action light, intensity.T 100,
  spectrumcutoff.off; sprawdzić 1000 W/m² i niezerowe G(x). [Replay fazy 4](runs/phase-4/series.script).
- Batch Set-up → Add → bezpośredni T; indeks ring zależy od struktury. .sbf: Label 1:T,
  Steps3/250…350 daje 250/300/350 K; simultaneous=0 → nested product. Potwierdzać .sbf/raw.
  Recorder liczy ponownie, może ignorować zakresy Action List; kontrolować metryki niezależnie.
- Bezpieczne nazwy m20/p10 zamiast offset--0.2. Pełne ścieżki bez quotes działały w użytych
  folderach; ścieżki ze spacjami nieprzetestowane. Dobrać timeout, izolować przypadki serii.
- Pierwszy defekt: GUI seed lub sprawdzona .def. Kolejny: set layer1.defect1.duplicate, potem defect2;
  interface analogicznie IFdefect1/2. Powtórzony srhrecombination potrafi zgubić pierwszy defekt.
- Failure-safe: set errorhandling.appendtofile; .def/.act przed i po calculate; zachować standardowy
  SCAPSErrorLogFile.log. Nie podawać domniemanego argumentu log-path. [Faza 11](runs/phase-11/report.md).
- Exit6 i crash codes występowały po eksporcie. Żaden kod ani windows=[] nie dowodzi sukcesu
  lub końca procesu. Sprawdzać pliki i lifecycle oddzielnie; normal close, bez kill.

## Physics recipes i ograniczenia

- Kontakty: barrier Ec−Fn (n) / Fp−Ev (p); idealnie phi_m−chi / chi+Eg−phi_m.
  J0 zależy też od mobility/DOS/contact S; nie wymuszać free-electron Richardson A*.
- IV(T): właściwy znak forward, expm1 fit; ln(J0/T²) vs1/T → phi_eff=−kB×slope.
  Faza2 okno0.12–0.24 V, reverse200/220 K wykluczyć. Aeff i n są efektywne, zależne od okna.
- CV: c=C/A; n-Schottky reverse V>0 ma Nd=2/(q eps slope), p-Schottky Na=−2/(q eps slope).
  p–n wymaga obu doping/epsilon; homo thermal correction nie przenosi się automatycznie na hetero.
  Raw Napp/W często używa eps10; prawie płaskie CV nie daje wiarygodnego doping profile.
- AS: maksimum −dC/dlnf i local Debye fit porównywać osobno. Nc∝T^1.5, vth∝T^0.5 daje T²;
  ln(2πf/T²) vs1/T, nie samo lnf. Inflection frequency nie musi równać się en/(2π).
- Walter: [faza 7](runs/phase-7/report.md) podaje wzór/okno/baseline subtraction. Nt300K odzyskane
  ~25–26%, Gaussian sigma +57.67%; nie skalować do input. Gaussian sigma=Echar/sqrt(2), uniform
  Echar=pełna szerokość. Nakładające się piki wymagają joint fit; energy window ogranicza inversion.
- Rs/Gsh: Jterm=Jdev+Vint/Rsh, Vterm=Vint+Jterm Rs. Zadany zakres to Vint, export Vterm;
  porównywać przy wspólnym Vint/prądzie. Rs=0 script odrzucone; disable flags w input .def.
- Heterojunction: ΔEc=chi_left−chi_right; ΔEv=ΔEc+Eg_left−Eg_right. Dwa interface rows mają
  to samo x. Brak uniwersalnego progu spike kilku kT; FF samo nie mierzy strat mocy.
- Light IV musi bracketować J=0. Extrapolated Voc/FF odrzucić i rozszerzyć bezpieczny zakres.
  Voc(T→0) nie jest automatycznie interface Ea; QE cutoff nie musi równać się electrical Eg.
- Numeryka: clamp/termination w kT, nie V. Scan complete nie zastępuje current-budget/profile checks.
  Low80/40 terminal current unresolved; high doping i forward mogą mieć duży błąd bilansu.
- SCAPS nie rozwiązuje transientów ani finite-amplitude DLCP; linear AC nie zastępuje C(t)/C(Vac).
  [DLCP requirements](runs/phase-8/dlcp-requirements.md). Phase8 G nie jest golden conductance.

## Replay i kontrola

[Katalog](datasets/CATALOG.md) prowadzi do params; replay/analyze i ograniczenia są w raportach faz.
Fazy2–11: python runs/phase-N/analyze.py; phase1: analyze_gui.py; GB/G2: własne analyze.py.
Replay/generatory mogą nadpisywać pliki fazy: uruchamiać tylko przy świadomym odtwarzaniu pomiaru.
Faza12: python runs/phase-12/catalog.py --check, bez ponownego acquisition.
Python analysis/generators/--check bez build-lock; kompilatory, test executables i GPU przez owner build-lock.
Sprawdzać ID/warunki, counts/endpoints, raw→SI, .def/.act/batch overrides, SHA256, actual profile bias,
J=0 bracketing, current conservation i zakresy ekstrakcji. Mesh/frequency convergence, GUI reliability,
G anomalies, stałe SCAPS i quantum/low-T model validity pozostają otwarte.
