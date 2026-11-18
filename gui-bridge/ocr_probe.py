"""Probe: local Windows OCR of one window -> text lines with boxes (no image sent to the agent)."""
import asyncio, ctypes, io, sys, time
from PIL import ImageGrab
from pywinauto import Application
from winrt.windows.media.ocr import OcrEngine
from winrt.windows.graphics.imaging import BitmapDecoder
from winrt.windows.storage.streams import InMemoryRandomAccessStream, DataWriter

ctypes.windll.shcore.SetProcessDpiAwareness(2)

async def ocr(img):
    buf = io.BytesIO(); img.save(buf, "PNG")
    stream = InMemoryRandomAccessStream(); w = DataWriter(stream)
    w.write_bytes(buf.getvalue()); await w.store_async(); stream.seek(0)
    bmp = await (await BitmapDecoder.create_async(stream)).get_software_bitmap_async()
    return await OcrEngine.try_create_from_user_profile_languages().recognize_async(bmp)

def capture(hwnd):
    """PrintWindow: grabs window content even when covered, no focus steal."""
    import win32gui, win32ui
    from PIL import Image
    l, t, r, b = win32gui.GetWindowRect(hwnd); w, h = r - l, b - t
    hdc = win32gui.GetWindowDC(hwnd); src = win32ui.CreateDCFromHandle(hdc); mem = src.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(src, w, h); mem.SelectObject(bmp)
    ctypes.windll.user32.PrintWindow(hwnd, mem.GetSafeHdc(), 2)
    img = Image.frombuffer("RGB", (w, h), bmp.GetBitmapBits(True), "raw", "BGRX", 0, 1)
    win32gui.DeleteObject(bmp.GetHandle()); mem.DeleteDC(); src.DeleteDC(); win32gui.ReleaseDC(hwnd, hdc)
    return img.resize((w * 2, h * 2))  # ponytail: 2x upscale helps OCR on small CVI fonts

pid, title = int(sys.argv[1]), sys.argv[2]
win = Application(backend="win32").connect(process=pid).window(title=title)
r = win.rectangle()
t = time.perf_counter()
res = asyncio.run(ocr(capture(win.handle)))
print(f"rect={r} ocr_ms={1000*(time.perf_counter()-t):.0f} lines={len(res.lines)}")
for ln in res.lines:
    b = ln.words[0].bounding_rect
    print(f"{int(b.x):4d},{int(b.y):4d} {ln.text}")
