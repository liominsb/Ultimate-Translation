"""复现「按住 Alt 触发」的取词场景，在 Chromium 里验证修复。

Windows 记事本对 Alt+Ctrl+C 是宽容的，浏览器不认，
所以必须在 Chromium 系窗口里测才有意义。

只用于本机排查，不参与产品构建。
"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shot_popup import capture, find_popup  # noqa: E402

user32 = ctypes.windll.user32
VK_MENU, VK_CONTROL, VK_A, VK_D, KEYUP = 0x12, 0x11, 0x41, 0x44, 2

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMP = os.path.join(ROOT, "_tmp")
PAGE = os.path.join(TMP, "alt_test.html")
SHOT = os.path.join(TMP, "alt_shot.bmp")

EDGE_CANDIDATES = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
]

TEXT = "The cache is invalidated whenever the configuration changes."


def find_window(substr):
    hits = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        n = user32.GetWindowTextLengthW(hwnd)
        if n:
            b = ctypes.create_unicode_buffer(n + 1)
            user32.GetWindowTextW(hwnd, b, n + 1)
            if substr in b.value:
                hits.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    return hits


def chord(mod, vk):
    user32.keybd_event(mod, 0, 0, 0)
    user32.keybd_event(vk, 0, 0, 0)
    user32.keybd_event(vk, 0, KEYUP, 0)
    user32.keybd_event(mod, 0, KEYUP, 0)


def main():
    user32.SetProcessDPIAware()
    os.makedirs(TMP, exist_ok=True)
    with open(PAGE, "w", encoding="utf-8") as f:
        f.write("<html><body style='margin:80px;font:28px Microsoft YaHei'>"
                + TEXT + "</body></html>")

    edge = next((p for p in EDGE_CANDIDATES if os.path.exists(p)), None)
    if not edge:
        print("Edge not found")
        return

    proc = subprocess.Popen([edge, "--new-window", "--no-first-run",
                             "--no-default-browser-check", PAGE])
    time.sleep(4.0)

    wins = find_window("alt_test")
    if not wins:
        print("browser window not found")
        return
    hwnd = wins[0]
    user32.ShowWindow(hwnd, 9)
    user32.SetForegroundWindow(hwnd)
    time.sleep(1.0)

    chord(VK_CONTROL, VK_A)   # 全选页面文本
    time.sleep(0.5)

    # 关键场景：Alt 按下不松，直接触发 D，模拟真实按键顺序
    user32.keybd_event(VK_MENU, 0, 0, 0)
    user32.keybd_event(VK_D, 0, 0, 0)
    user32.keybd_event(VK_D, 0, KEYUP, 0)
    time.sleep(0.12)          # 这 120ms 内 Alt 一直是按下的
    user32.keybd_event(VK_MENU, 0, KEYUP, 0)

    time.sleep(2.0)
    hits = find_popup()
    print("popups:", hits)
    if hits:
        capture(hits[0], SHOT)
        print("shot:", SHOT)

    proc.terminate()


if __name__ == "__main__":
    main()
