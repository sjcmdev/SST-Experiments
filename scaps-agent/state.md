# State

2026-11-19 — phase 12 complete: synthesis. Desktop active; SCAPS close=Closed, windows=[].
No new SCAPS acquisition, no product edits, no git. One phase only.

Deliverables:
- datasets/CATALOG.md: all 74 top-level dataset directories, including INVALID/EXPLORATORY and derived data.
- runs/phase-12/capabilities.md: tested/documented/unsupported map against SST techniques.
- runs/phase-12/pain-points.md: ranked top-10 with SST proposals and phase evidence.
- knowledge.md: 135-line cookbook, replacing 283 lines; original preserved as runs/phase-12/knowledge-before.md.
- runs/phase-12/report.md, catalog.py, inventory.json, source-sha256.json, validation.txt.

Validation in this job: py_compile catalog.py built bytecode in repo runs/phase-12/__pycache__.
Full catalog.py --check PASS: 74 rows, all 87 params JSON parsed, 1306 dataset source hashes,
CSV/.def presence, documentation links/encoding/archive, catalog reproducibility and physics summary arithmetic.
Python ran without build-lock per brief. No C++ target changed or built; no earlier phase acquisition/analysis rerun.
Three manual lookups logged docs-needed. The first documentation check found the missing validation.txt;
created it and reran successfully. Cookbook encoding damage in the first shell pipeline was corrected.

Continue by reading catalog and the phase report, then choose exact source datasets via params and flags.
For phase11 replay use its report/state archived in journal and case inputs/scripts: baseline/thin are accepted
Core comparisons; low80 terminal comparator intentionally failed, and high-forward outputs are diagnostics.
No .def needs loading for this synthesis. SCAPS has no active setup now; load a chosen phase input only for
an explicitly requested new measurement. Replays/generators may overwrite their phase outputs.

Open: GUI input reliability, G/Z anomalies, full mesh/frequency convergence, low-T/high-forward validity,
Walter inversion bias, quantum calibration, native fitter and external-Rs/Gsh AC validation.
No finite-amplitude DLCP, C(t)/DLTS or general external-RLC golden data supplied by this campaign.
Next phase only on owner instruction.
