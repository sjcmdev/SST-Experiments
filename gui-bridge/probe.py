"""Probe: how much of a GUI app is visible as text via UIA / win32 backends."""
import sys, time
from pywinauto import Application

exe, title_re = sys.argv[1], sys.argv[2]
out = {}
for backend in ("uia", "win32"):
    try:
        app = Application(backend=backend).connect(title_re=title_re, timeout=5)
    except Exception:
        app = Application(backend=backend).start(exe, work_dir=exe.rsplit("\\", 1)[0])
        time.sleep(6)
        app = Application(backend=backend).connect(title_re=title_re, timeout=20)
    win = app.top_window()
    ctrls = win.descendants()
    named = [c for c in ctrls if c.window_text().strip()]
    print(f"== {backend}: window={win.window_text()!r} controls={len(ctrls)} named={len(named)}")
    for c in ctrls[:80]:
        ei = c.element_info
        print(f"  {ei.control_type if backend=='uia' else ei.class_name!s:20.20} | {c.window_text()[:50]!r}")
