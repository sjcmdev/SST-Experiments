# State

2026-11-19 00:40 CET — phase-1: cztery pomiary GUI wykonane; formalny build/test zablokowany przez globalny build-lock. SCAPS zamknięty, gui.windows(scaps)=[]. Pulpit podczas pracy active.

Raport: runs/phase-1/report-gui.md. Wykresy: gui-bands.png, gui-iv.png; metryki: gui-summary.json.

Do odtworzenia: najpierw runs/phase-1/session-gui.scaps (zachowuje convergence/mesh), potem jedna definicja:
- n-Schottky: runs/phase-1/n-schottky-gui-fixed.def; phi_m4.80 eV, Nd1e16 cm^-3.
- n-ohmic: runs/phase-1/n-ohmic-gui.def; phi_m4.05 eV.
- p-Schottky: runs/phase-1/p-schottky-gui.def; phi_m4.42 eV, Na1e16 cm^-3.
- p-ohmic: runs/phase-1/p-ohmic-gui.def; phi_m5.17 eV.

Wszystkie: single layer2 µm, Eg1.12,chi4.05 eV,eps11.7,Nc=Nv1e19 cm^-3,mu_n=mu_p50 cm²/Vs,brak defektów. Back flatband, front fixed work function, tunneling off. T300 K,dark,workpoint0 V,frequency field1 MHz. Zakres n−0.6…+0.4 V,p−0.4…+0.6 V,krok0.02 V. Ostatnia sesja zapisana z I–V wyłączonym po equilibrium p-ohmic; p-ohmic-gui.act zachowuje wcześniejszy wybór I–V=1.

Datasets: datasets/phase-1-{n-schottky,n-ohmic,p-schottky,p-ohmic}-gui. Każdy raw IV/EB, CSV SI, params.json,device.def,session.scaps. Single shot IDs2,4,6,8. n-schottky-gui-incomplete to INVALID po failure+0.82 V; nie używać jako golden. n-schottky-gui.def to błędny wstępny zapis front flatband=1; nie używać zamiast n-schottky-gui-fixed.def. Starszych plików bez gui nie nadpisano.

Fizyka: phi_B0.75 eV dla obu Schottky,0 dla obu ohmic; rectification1603 vs≈1; n_eff1.03677,J0_eff0.007447 A/m². Richardson A*=120 daje0.027163 A/m²; rozbieżność−72.58% związana z ograniczeniem transportu. Equilibrium drift-diffusion estimate0.007050 A/m². Szczegóły i ograniczenia w raporcie.

Generator analyze_gui.py wykonany; wykresy obejrzane. Próby Python byte-compilation i --check przez globalny build-lock zatrzymano po kilku minutach bez uzyskania locka. Build/test NIE wykonane; brak PASS. Nie ma builda w repo ani TEMP. Następne wymagane działanie przed golden approval:
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh python runs/phase-1/analyze_gui.py --check

Na początku kolejnej sesji desktop_active(). Dalszy plan faz zgodnie z nowym zadaniem; nie kontynuowano phase-G ani kolejnej fazy w tym runie. Bez git i manuala SCAPS.
