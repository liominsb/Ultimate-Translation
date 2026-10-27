"""验证「点别处自动关闭」：弹窗出现后点一下屏幕角落，看它是否消失。

只用于本机排查，不参与产品构建。
"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shot_popup import find_popup  # noqa: E402

user32 = ctypes.windll.user32
VK_MENU, VK_CONTROL, VK_A, VK_D, KEYUP = 0x12, 0x11, 0x41, 0x44, 2
MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP = 0x0002, 0x0004

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMP = os.path.join(ROOT, "_tmp")
SAMPLE = os.path.join(TMP, "dismiss.txt")


def chord(mod, vk):
    user32.keybd_event(mod, 0, 0, 0)
    user32.keybd_event(vk, 0, 0, 0)
    user32.keybd_event(vk, 0, KEYUP, 0)
    user32.keybd_event(mod, 0, KEYUP, 0)


def click(x, y):
    user32.SetCursorPos(x, y)
    time.sleep(0.05)
    user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)


def main():
    user32.SetProcessDPIAware()
    os.makedirs(TMP, exist_ok=True)
    with open(SAMPLE, "w", encoding="utf-8") as f:
        f.write("The build failed because the dependency lock file is out of date.")

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

    time.sleep(1.5)
    before = find_popup()
    print("弹窗出现后:", before)

    # 点屏幕左上角空白处（远离弹窗）
    click(40, 300)
    time.sleep(0.4)

    after = find_popup()
    print("点击别处后:", after)
    print("结果:", "已自动关闭" if (before and not after) else "未关闭")

    proc.terminate()


if __name__ == "__main__":
    main()
