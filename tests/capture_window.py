"""Capture an owned test window using PrintWindow without showing it on screen."""
import ctypes as C
from ctypes import wintypes as W
from pathlib import Path
import struct
import sys
import zlib
import gui_validation as g

hwnd = g.window_for(int(sys.argv[1]))
if not hwnd:
    raise RuntimeError("Test window not found")
g.user.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
g.user.GetDC.argtypes = [W.HWND]
g.user.GetDC.restype = W.HDC
g.user.ReleaseDC.argtypes = [W.HWND, W.HDC]
g.user.PrintWindow.argtypes = [W.HWND, W.HDC, W.UINT]
gd = C.WinDLL("gdi32")
gd.CreateCompatibleDC.argtypes = [W.HDC]
gd.CreateCompatibleDC.restype = W.HDC
gd.CreateCompatibleBitmap.argtypes = [W.HDC, C.c_int, C.c_int]
gd.CreateCompatibleBitmap.restype = W.HBITMAP
gd.SelectObject.argtypes = [W.HDC, W.HANDLE]
gd.SelectObject.restype = W.HANDLE
gd.GetDIBits.argtypes = [W.HDC, W.HBITMAP, W.UINT, W.UINT, C.c_void_p, C.c_void_p, W.UINT]
gd.DeleteDC.argtypes = [W.HDC]
gd.DeleteObject.argtypes = [W.HANDLE]
rect = W.RECT()
g.user.GetWindowRect(hwnd, C.byref(rect))
width, height = rect.right - rect.left, rect.bottom - rect.top
screen = g.user.GetDC(None)
dc = gd.CreateCompatibleDC(screen)
bitmap = gd.CreateCompatibleBitmap(screen, width, height)
old = gd.SelectObject(dc, bitmap)
try:
    if not g.user.PrintWindow(hwnd, dc, 2):
        raise RuntimeError("PrintWindow failed")
    header = struct.pack("<IiiHHIIiiII", 40, width, -height, 1, 32, 0, 0, 0, 0, 0, 0)
    info = C.create_string_buffer(header + bytes(12))
    pixels = C.create_string_buffer(width * height * 4)
    gd.GetDIBits(dc, bitmap, 0, height, pixels, info, 0)
    bgra = pixels.raw
    rgba = bytearray(len(bgra))
    rgba[0::4], rgba[1::4], rgba[2::4], rgba[3::4] = bgra[2::4], bgra[1::4], bgra[0::4], b"\xff" * (width * height)
    raw = b"".join(b"\0" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    def chunk(name, data):
        return struct.pack(">I", len(data)) + name + data + struct.pack(">I", zlib.crc32(name + data))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    Path(sys.argv[2]).write_bytes(png)
    print("Captured", width, height)
finally:
    gd.SelectObject(dc, old)
    gd.DeleteObject(bitmap)
    gd.DeleteDC(dc)
    g.user.ReleaseDC(None, screen)
