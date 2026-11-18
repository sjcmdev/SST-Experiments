PHASE GB: Batch and Recorder through the GUI, plus how their results are shown. Run only when desktop_active() == 'active'. The owner marked this as IMPORTANT. ScapsLab v4 is "Batch and recorder" and is designed from these findings.

Device: use runs/phase-1 n-Schottky (or phase-G pn-gui.def). Click like a human, time each task, and use hwnd addressing. Remember the Set problem twice hint and that Save Action List needs Add Comment → OK.

1. **Batch set-up (GUI):**
   - Open the batch panel. Sweep one parameter: temperature 250, 300 and 350 K. Then a second parameter: layer doping 1e15, 1e16 and 1e17 cm⁻³ (or thickness).
   - Then a 2-parameter batch (T × doping, 3×3).
   - Note how parameters are chosen: the list, units, how the values are entered, and whether a log sweep is possible.
   - Note how you can tell what the batch will compute before running it.
2. **Run the batch** with I-V dark (and C-V for one batch). Observe:
   - progress feedback;
   - what happens on a convergence failure in one point (does it stop or block with a modal, is it skipped and marked?);
   - the time per point.
3. **Recorder:**
   - Set it to record Voc, Jsc, FF and eta under light, plus C at 0 V, or whatever the recorder offers, as a function of the batch parameter.
   - Note where the recorder lives, how its quantities are chosen and how they map to batch parameters.
   - Note whether the recorder can be set up after the batch has run, or only before.
4. **Display:**
   - How do batch results appear: overlaid curves, legend, colours, can you tell which curve is which parameter value?
   - The recorder plot: axes, units, log scale, multiple quantities.
   - Switching between batch members.
   - Copying or exporting a single member versus all of them.
   - Save the recorder table and the batch curves through the GUI. Check the files (format, parameter columns, provenance, units, asterisks or garbage).
5. **Script comparison:** do the same 1-D T batch through a script (the knowledge.md recipes). Compare effort and results; the numbers must match.
6. **Physics check table:** J(V) at a fixed V vs T, with ln J vs 1/T giving the barrier ≈ 0.75 eV for the n-Schottky device. Also C(0 V) vs doping, with 1/C² ∝ 1/N.

Log every friction in annoyances.md (category, time lost, severity, SST proposal) and tag the rows `batch`, `recorder` or `display`. The report must end with a section titled "ScapsLab v4 requirements": a concrete list of what our Batch/Recorder UI must do better, each item tied to an observed annoyance.

Save datasets as in AGENT.md, with a `--check`. Python checks run without build-lock. Close SCAPS normally.
