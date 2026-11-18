"""Local Windows GUI bridge. Run with the project's Python; stdout is MCP only."""
import asyncio
import ctypes
import difflib
import io
import math
import os
from pathlib import Path
import re
import subprocess
import time
from datetime import datetime, timezone


def fix_ocr(text: str) -> str:
    """Correct digits only in numeric tokens; CVI sometimes reads a decimal as space."""
    text = re.sub(r"\b([0-9Ol]+) ([0-9Ol]{2})([Ee][+-]?[0-9Ol]+)\b",
                  r"\1.\2\3", text)

    def replace(match):
        token = match.group()
        return token.translate(str.maketrans("Ol", "01")) if any(c.isdigit() for c in token) else token

    return re.sub(r"(?<![\w.])[+-]?[0-9Ol]+(?:[.,][0-9Ol]+)?(?:[Ee][+-]?[0-9Ol]+)?(?![\w.])",
                  replace, text)


def diff_lines(previous: list[str], current: list[str]) -> list[str]:
    """Current additions/changes retain x,y format; removed lines start with '- '."""
    result = []
    for tag, a, b, c, d in difflib.SequenceMatcher(None, previous, current, autojunk=False).get_opcodes():
        if tag in ("delete", "replace"):
            result.extend("- " + line for line in previous[a:b])
        if tag in ("insert", "replace"):
            result.extend(current[c:d])
    return result


def fuzzy_match(text: str, candidates: list[str], nth: int = 0) -> int:
    """Return a candidate index, with equally good matches in supplied (screen) order."""
    def normalize(value):
        return " ".join(re.findall(r"\w+", value.casefold()))

    query = normalize(text)
    if not query or nth < 0:
        raise ValueError("Text must contain letters/digits; nth must be nonnegative")
    scores = [difflib.SequenceMatcher(None, query, normalize(c)).ratio() for c in candidates]
    best = max(scores, default=0)
    matches = [i for i, score in enumerate(scores) if score >= max(0.70, best - 0.02)]
    if nth >= len(matches):
        raise ValueError(f"No OCR match for {text!r}, nth={nth}")
    return matches[nth]


# DPI awareness must precede pywinauto and all window measurements.
if not ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4)):
    ctypes.windll.shcore.SetProcessDpiAwareness(2)

import win32api
import win32con
import win32gui
import win32process
import win32ui
from PIL import Image
from pywinauto import Desktop, keyboard, mouse
import pywintypes
from winrt.windows.globalization import Language
from winrt.windows.media.ocr import OcrEngine
from winrt.windows.graphics.imaging import BitmapDecoder
from winrt.windows.storage.streams import InMemoryRandomAccessStream, DataWriter

try:
    from mcp.server.fastmcp import FastMCP
    from mcp.server.fastmcp.exceptions import ToolError
except ModuleNotFoundError as exc:
    if exc.name != "mcp.server.fastmcp":
        raise
    # MCP 2 renamed the same high-level server; no SDK replacement is needed.
    from mcp.server.mcpserver import MCPServer as FastMCP
    from mcp.server.mcpserver.exceptions import ToolError

mcp = FastMCP("gui-bridge")
APPS = {
    "scaps": Path(r"D:\Aplications\SCAPS\install\scaps3312.exe"),
    "propho": Path(r"D:\STUDIA\Semiconductor-Studio\ProPho\propho_1v1.exe"),
    "origin": None,  # TODO: provide the installed Origin executable.
}
ROOT = Path(__file__).resolve().parent
_last_reads = {}
_shot_count = 0
_shot_budget = max(0, int(os.environ.get("GUI_BRIDGE_SHOT_BUDGET", "5")))


def app_path(app):
    if app not in APPS:
        raise ToolError(f"Unknown app: {app}; choose {', '.join(APPS)}")
    if APPS[app] is None:
        raise ToolError("TODO: Origin executable is not configured")
    return APPS[app]


def app_windows(app):
    exe = app_path(app)
    found = []
    for win in Desktop(backend="win32").windows(visible_only=True):
        if not win.window_text():
            continue
        handle = None
        try:
            handle = win32api.OpenProcess(0x1000, False, win.process_id())
            buf = ctypes.create_unicode_buffer(32768)
            size = ctypes.c_ulong(len(buf))
            if ctypes.windll.kernel32.QueryFullProcessImageNameW(int(handle), 0, buf, ctypes.byref(size)):
                if Path(buf.value) == exe:
                    found.append(win)
        except (OSError, win32gui.error):
            continue
        finally:
            if handle is not None:
                handle.Close()
    return found


def find_window(title):
    if not title.strip():
        raise ToolError("Window title must not be empty")
    visible = Desktop(backend="win32").windows(visible_only=True)
    matches = [w for w in visible if w.window_text() == title]
    if not matches:
        try:
            pattern = re.compile(title)
        except re.error as exc:
            raise ToolError(f"Invalid title regex: {exc}") from exc
        matches = [w for w in visible if pattern.search(w.window_text())]
    if len(matches) != 1:
        raise ToolError(f"Expected one window for {title!r}, found {len(matches)}")
    return matches[0]


def capture(win):
    """PrintWindow at native scale, cropped to the client area before OCR."""
    if win.is_minimized():
        raise ToolError("Restore the minimized window before capturing it")
    left, top, right, bottom = win32gui.GetWindowRect(win.handle)
    width, height = right - left, bottom - top
    hdc = win32gui.GetWindowDC(win.handle)
    src = mem = bmp = old = None
    try:
        src = win32ui.CreateDCFromHandle(hdc)
        mem = src.CreateCompatibleDC()
        bmp = win32ui.CreateBitmap()
        bmp.CreateCompatibleBitmap(src, width, height)
        old = mem.SelectObject(bmp)
        if not ctypes.windll.user32.PrintWindow(win.handle, mem.GetSafeHdc(), 2):
            raise ToolError("PrintWindow failed")
        img = Image.frombuffer("RGB", (width, height), bmp.GetBitmapBits(True), "raw", "BGRX", 0, 1)
        cx, cy = win32gui.ClientToScreen(win.handle, (0, 0))
        _, _, cw, ch = win32gui.GetClientRect(win.handle)
        return img.crop((cx - left, cy - top, cx - left + cw, cy - top + ch))
    finally:
        if old is not None:
            mem.SelectObject(old)
        if bmp is not None:
            win32gui.DeleteObject(bmp.GetHandle())
        if mem is not None:
            mem.DeleteDC()
        if src is not None:
            src.DeleteDC()
        win32gui.ReleaseDC(win.handle, hdc)


async def recognize(win, region=None):
    img = capture(win)
    ox = oy = 0
    if region is not None:
        ox, oy, right, bottom = region
        if not (0 <= ox < right <= img.width and 0 <= oy < bottom <= img.height):
            raise ToolError("region must be client [left, top, right, bottom] inside the window")
        img = img.crop(region)
    # shortcut: fixed 2x scale suits these small panels; tile if OCR's maximum size is exceeded.
    img = img.resize((img.width * 2, img.height * 2))
    engine = OcrEngine.try_create_from_language(Language("en-US"))
    if engine is None:
        engine = OcrEngine.try_create_from_user_profile_languages()
    if engine is None:
        raise ToolError("Install a Windows OCR language pack (preferably en-US)")
    if max(img.size) > OcrEngine.max_image_dimension:
        raise ToolError("Window too large for 2x OCR; supply a smaller region")
    buf = io.BytesIO()
    img.save(buf, "PNG")
    stream = InMemoryRandomAccessStream()
    writer = DataWriter(stream)
    bitmap = None
    try:
        writer.write_bytes(buf.getvalue())
        await writer.store_async()
        stream.seek(0)
        bitmap = await (await BitmapDecoder.create_async(stream)).get_software_bitmap_async()
        result = await engine.recognize_async(bitmap)
        lines, words = [], []
        for line in result.lines:
            line_words = []
            for word in line.words:
                b = word.bounding_rect
                line_words.append((word.text, ox + b.x / 2, oy + b.y / 2, b.width / 2, b.height / 2))
            if line_words:
                lines.append(f"{int(line_words[0][1])},{int(line_words[0][2])} {fix_ocr(line.text)}")
                words.append(line_words)
        return lines, words
    finally:
        if bitmap is not None:
            bitmap.close()
        writer.detach_stream()
        writer.close()
        stream.close()


@mcp.tool()
async def windows(app: str) -> list[dict]:
    """List visible top-level windows of scaps/propho/origin, including CVI panels."""
    return [{"title": w.window_text(), "class": w.class_name(), "pid": w.process_id()} for w in app_windows(app)]


@mcp.tool()
async def read(title: str, region: tuple[int, int, int, int] | None = None, diff: bool = True) -> str:
    """OCR lines in 1x client coordinates. region=[left,top,right,bottom]. Removed lines: '- '."""
    win = find_window(title)
    lines, _ = await recognize(win, region)
    cache_key = (win.window_text(), win.handle, region)
    previous = _last_reads.get(cache_key, [])
    _last_reads[cache_key] = lines
    return "\n".join(diff_lines(previous, lines) if diff else lines)


@mcp.tool()
async def click(title: str, text: str | None = None, x: int | None = None, y: int | None = None,
                nth: int = 0, offset: tuple[int, int] = (0, 0), real_mouse: bool = False) -> str:
    """Click OCR phrase or client x,y. Default sends messages; real_mouse=True moves the mouse.

    Delivery does not prove a UI change: use read to verify and explicitly retry with real_mouse.
    """
    win = find_window(title)
    if text is not None:
        if x is not None or y is not None:
            raise ToolError("Supply text OR both x and y")
        _, lines = await recognize(win)
        n = len(re.findall(r"\w+", text))
        candidates = []
        for words in lines:
            for size in range(max(1, n - 1), n + 2):
                for i in range(len(words) - size + 1):
                    span = words[i:i + size]
                    left, top = span[0][1:3]
                    right = max(w[1] + w[3] for w in span)
                    bottom = max(w[2] + w[4] for w in span)
                    candidates.append((" ".join(w[0] for w in span), int((left + right) / 2), int((top + bottom) / 2)))
        # OCR baselines vary by a pixel: group nearby rows before ordering left to right.
        candidates.sort(key=lambda c: (round(c[2] / 8), c[1]))
        try:
            match = candidates[fuzzy_match(text, [c[0] for c in candidates], nth)]
        except ValueError as exc:
            raise ToolError(str(exc)) from exc
        _, x, y = match
    elif x is None or y is None:
        raise ToolError("Supply text OR both x and y")
    x, y = x + offset[0], y + offset[1]
    _, _, width, height = win32gui.GetClientRect(win.handle)
    if not (0 <= x < width and 0 <= y < height):
        raise ToolError("Click is outside the client area")
    if real_mouse:
        # click_input() calls SetForegroundWindow, which Windows denies to background processes;
        # a raw click at screen coords activates the window itself.
        try:
            mouse.click(coords=win32gui.ClientToScreen(win.handle, (x, y)))
        except (RuntimeError, pywintypes.error) as exc:
            raise ToolError(str(exc)) from exc
    else:
        # Tk widgets have child HWNDs; send messages to the child beneath the OCR point.
        target = win
        point = (x, y)
        while child := win32gui.ChildWindowFromPointEx(target.handle, point, 7):
            if child == target.handle:
                break
            screen = win32gui.ClientToScreen(target.handle, point)
            target = Desktop(backend="win32").window(handle=child).wrapper_object()
            point = win32gui.ScreenToClient(child, screen)
        target.click(coords=point)
    return f"{x},{y} {'real mouse' if real_mouse else 'messages sent; verify with read'}"


@mcp.tool()
async def type_text(text: str) -> str:
    """Type literal Unicode into the currently focused control (uses real keyboard input)."""
    escaped = "".join("{" + char + "}" if char in "+^%~{}()" else char for char in text)
    keyboard.send_keys(escaped, with_spaces=True, with_tabs=True, with_newlines=True)
    return "Text sent to focused control"


@mcp.tool()
async def key(keys: str) -> str:
    """Send pywinauto key syntax to focused control, e.g. ^a, {ENTER}, %{F4}."""
    keyboard.send_keys(keys)
    return "Keys sent to focused control"


@mcp.tool()
async def wait(title: str, seconds: float = 5) -> str:
    """Wait up to seconds for a visible window; return its exact title."""
    if not math.isfinite(seconds) or not 0 <= seconds <= 120:
        raise ToolError("seconds must be between 0 and 120")
    deadline = time.monotonic() + seconds
    while True:
        try:
            return find_window(title).window_text()
        except ToolError:
            if time.monotonic() >= deadline:
                raise
            await asyncio.sleep(min(0.2, max(0, deadline - time.monotonic())))


@mcp.tool()
async def desktop_active() -> str:
    """'active' if real mouse/keyboard input works now, 'locked' if the Windows lock screen is in front.
    When locked: read/screenshot/run_script still work, click/type/key do not."""
    fg = win32gui.GetForegroundWindow()
    locked = fg and win32gui.GetClassName(fg) == "Windows.UI.Core.CoreWindow" and "LockApp" in _process_name(fg)
    return "locked" if locked else "active"


def _process_name(hwnd):
    pid = win32process.GetWindowThreadProcessId(hwnd)[1]
    out = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
    return out.split(",")[0].strip('"')


@mcp.tool()
async def run_script(files: list[str], timeout_s: int = 1800) -> str:
    """Batch-run SCAPS: `scaps3312.exe <files...>` (e.g. a .def then a .script; SCAPS loads .def, .act, .scaps,
    .spe, .gen, .sbf, .srf, then runs .script). End the script with `set quitscript quitSCAPS` so SCAPS exits.
    Works with a locked screen. Returns exit code and duration; results are whatever the script saved."""
    if app_windows("scaps"):
        raise ToolError("SCAPS already running; close('scaps') first")
    paths = [str(Path(f).resolve()) for f in files]
    missing = [p for p in paths if not Path(p).exists()]
    if missing:
        raise ToolError(f"Missing files: {missing}")
    exe = app_path("scaps")
    start = time.monotonic()
    proc = await asyncio.create_subprocess_exec(str(exe), *paths, cwd=str(exe.parent))
    try:
        rc = await asyncio.wait_for(proc.wait(), timeout_s)
    except asyncio.TimeoutError:
        # Leave SCAPS running so the agent can inspect it; report visible windows and any dialog text.
        report = [f"TIMEOUT after {timeout_s}s; SCAPS still running (close('scaps') when done)."]
        for w in app_windows("scaps"):
            title = w.window_text()
            report.append(f"window: {title}")
            if not title.startswith("SCAPS 3.3.12") and title != "scaps3312":
                try:
                    _, lines = await recognize(w)
                    report += ["  " + " ".join(word[0] for word in ln) for ln in lines[:15]]
                except Exception as exc:  # OCR of a transient dialog may fail; the title is still useful
                    report.append(f"  (OCR failed: {exc})")
        return "\n".join(report)
    return f"exit={rc} seconds={time.monotonic() - start:.1f}"


@mcp.tool()
async def launch(app: str) -> str:
    """Launch a mapped app, or reuse it. ProPho is found by title, not launcher PID."""
    exe = app_path(app)
    title = "SCAPS 3.3.12 Action Panel" if app == "scaps" else "(?i)Propho"
    if not app_windows(app):
        subprocess.Popen([str(exe)], cwd=exe.parent, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(150):
        found = app_windows(app)
        if any(re.search(title, w.window_text()) for w in found):
            return await wait(title, 0)
        if any(w.window_text() == "Unhandled exception in script" for w in found):
            raise ToolError("App failed to start: Unhandled exception in script (read this dialog)")
        await asyncio.sleep(0.2)
    raise ToolError(f"No main window after 30 seconds: {title}")


@mcp.tool()
async def close(app: str) -> str:
    """Request normal close of mapped app. Never kill; report any remaining dialogs."""
    found = app_windows(app)
    # Closing a CVI panel only hides it; the frame owns the application lifetime.
    main = [w for w in found if w.class_name().startswith("CVIRTLVFrame")] if app == "scaps" else []
    if not main:
        main = [w for w in found if (w.window_text() == "SCAPS 3.3.12 Action Panel" if app == "scaps"
                                   else "propho" in w.window_text().casefold())]
    if not main:
        main = found
    for win in main:
        win32gui.PostMessage(win.handle, win32con.WM_CLOSE, 0, 0)
    for _ in range(15):
        await asyncio.sleep(0.2)
        remaining = app_windows(app)
        if not remaining:
            return "Closed"
    return "Still open (check dialogs): " + ", ".join(w.window_text() for w in remaining)


@mcp.tool()
async def screenshot(title: str, reason: str) -> str:
    """Save native client PNG under shots/, with reason logged; per-process budget default 5."""
    global _shot_count
    if not reason.strip():
        raise ToolError("A screenshot reason is required")
    if _shot_count >= _shot_budget:
        raise ToolError(f"Screenshot budget exhausted ({_shot_budget} per process)")
    img = capture(find_window(title))
    directory = ROOT / "shots"
    directory.mkdir(exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    path = directory / f"{stamp}-{os.getpid()}.png"
    img.save(path)
    _shot_count += 1
    with (directory / "log.md").open("a", encoding="utf-8") as log:
        log.write(f"- {stamp} `{path.name}` {title.splitlines()!r}: {' '.join(reason.split())}\n")
    return str(path)


if __name__ == "__main__":
    mcp.run(transport="stdio")
