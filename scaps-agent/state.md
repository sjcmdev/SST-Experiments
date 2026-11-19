# State

2026-11-19 ? phase-3 retry complete: wymagane C-V/Mott-Schottky/C-f/Arrhenius zapisane i sprawdzone. SCAPS close=Closed, windows=[].

Report: runs/phase-3/report.md; summary.json; analysis.png. Eight datasets/phase-3-* raw, CSV SI, params, exact definitions/settings/scripts/hashes.

Replay: run_script([absolute runs/phase-3/n-schottky-input.def, absolute runs/phase-3/series.script]). Series loads defect-seed.def internally. Dark, left contact bias. C-V1kHz/1MHz?2?+0.4V/121points300K; baseC-f1Hz?10MHz/211points0V; acceptorEc?0.4eV,Nt1e21m?3,sigma1e?19m?,5T240/270/300/330/360K.

Nd9.944715e21m?3,Vbi0.565289V; derivative-inflection ArrheniusEa0.4028921eV; localDebyeEa0.4004878eV. f_inf300K5583.3Hz vs en/2pi3034.7Hz; do not equate these.

Final replay exit0xC0000409 after complete exports; all files independently checked. GUI input inactive despite active desktop, repeated Set problem/hwnd/restart. No successful manual runs claimed. First defect.add_default failed; failed-add-default.script preserved; seed fallback works. Manual?3.5.2 temperature scaling logged docs-needed.

PASS this job after final replay: python -m py_compile runs/phase-3/analyze.py; python runs/phase-3/analyze.py. Bytecode in repo runs/phase-3/__pycache__; Python without lock per phase brief. No C++ target/product changes or git.

Phase3 measurements complete. Open investigations only: GUI input, first-defect add_default syntax, process error after export; high-forward-current transport not independently checked. Previous report in report-first-attempt.md. Do not rerun other phases implicitly.
