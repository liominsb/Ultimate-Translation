"""截取悬浮弹窗区域的图（GDI PrintWindow），用于人工确认渲染效果。

只用于本机排查，不参与产品构建。
"""
import ctypes
import ctypes.wintypes as wt
import os
import struct
import subprocess
import sys
import time

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
VK_MENU, VK_CONTROL, VK_A, VK_D, KEYUP = 0x12, 0x11, 0x41, 0x44, 2

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SAMPLE = os.path.join(ROOT, "_probe", "sample.txt")
SHOT = os.path.join(ROOT, "_probe", "popup.bmp")


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
        ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
        ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG),
        ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD),
        ("biClrImportant", wt.DWORD),
    ]


def find_popup():
    hits = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        buf = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, buf, 256)
        if "TranslateBallPopup" in buf.value:
            hits.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    return hits


def capture(hwnd, path):
    r = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top
    if w <= 0 or h <= 0:
        return False

    hdc = user32.GetWindowDC(hwnd)
    memdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(memdc, bmp)
    # PW_RENDERFULLCONTENT = 2，能拿到被遮挡或半透明合成后的内容
    user32.PrintWindow(hwnd, memdc, 2)

    bi = BITMAPINFOHEADER()
    bi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.biWidth = w
    bi.biHeight = -h
    bi.biPlanes = 1
    bi.biBitCount = 32
    bi.biCompression = 0
    bi.biSizeImage = w * h * 4

    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(memdc, bmp, 0, h, buf, ctypes.byref(bi), 0)

    with open(path, "wb") as f:
        f.write(b"BM")
        f.write(struct.pack("<IHHI", 14 + 40 + w * h * 4, 0, 0, 54))
        f.write(struct.pack("<IiiHHIIiiII", 40, w, -h, 1, 32, 0, w * h * 4, 2835, 2835, 0, 0))
        f.write(buf.raw)

    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(memdc)
    user32.ReleaseDC(hwnd, hdc)
    return True


def chord(mod, vk):
    user32.keybd_event(mod, 0, 0, 0)
    user32.keybd_event(vk, 0, 0, 0)
    user32.keybd_event(vk, 0, KEYUP, 0)
    user32.keybd_event(mod, 0, KEYUP, 0)


def main():
    # 必须的：不声明 DPI 感知，GetWindowRect 返回的是缩放后的逻辑坐标，
    # 而 PrintWindow 画的是物理像素，截出来就会被裁掉右边和下边
    user32.SetProcessDPIAware()

    text = ("This API is deprecated and will be removed in the next major release. "
            "Please migrate to the v2 endpoint.")
    if len(sys.argv) > 1:
        text = sys.argv[1]

    os.makedirs(os.path.dirname(SAMPLE), exist_ok=True)
    with open(SAMPLE, "w", encoding="utf-8") as f:
        f.write(text)

    subprocess.run(["taskkill", "/IM", "notepad.exe", "/F"], capture_output=True)
    time.sleep(0.4)
    proc = subprocess.Popen(["notepad.exe", SAMPLE])
    time.sleep(2.0)

    hwnd = user32.FindWindowW("Notepad", None)
    if hwnd:
        user32.ShowWindow(hwnd, 9)
        user32.SetForegroundWindow(hwnd)
        time.sleep(0.6)

    chord(VK_CONTROL, VK_A)
    time.sleep(0.3)
    chord(VK_MENU, VK_D)

    time.sleep(2.5)
    hits = find_popup()
    print("popups:", hits)
    if hits:
        ok = capture(hits[0], SHOT)
        print("capture:", ok, SHOT)

    proc.terminate()


if __name__ == "__main__":
    main()
