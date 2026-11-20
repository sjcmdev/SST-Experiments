# Shim: chain scripts point at $G/server.py; the real GUI-Bridge server lives in gui-bridge/.
import os, runpy, sys
d = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gui-bridge")
sys.path.insert(0, d)
sys.argv[0] = os.path.join(d, "server.py")
runpy.run_path(sys.argv[0], run_name="__main__")
