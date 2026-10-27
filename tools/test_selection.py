"""模拟真实用户操作：记事本打开文本 -> Ctrl+A 全选 -> Alt+D 触发划词翻译 -> 截图。

只用于本机验证，不参与产品构建。
"""
import ctypes
import os
import subprocess
import time

user32 = ctypes.windll.user32
VK_MENU = 0x12
VK_CONTROL = 0x11
VK_A = 0x41
VK_D = 0x44
KEYUP = 2

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SAMPLE = os.path.join(ROOT, "_probe", "sample.txt")
SHOT = os.path.join(ROOT, "_probe", "shot.png")

TEXT = ("The deployment pipeline failed because the container image "
        "could not be pulled from the private registry.")


def press(vk):
    user32.keybd_event(vk, 0, 0, 0)
    user32.keybd_event(vk, 0, KEYUP, 0)


def chord(mod, vk):
    user32.keybd_event(mod, 0, 0, 0)
    user32.keybd_event(vk, 0, 0, 0)
    user32.keybd_event(vk, 0, KEYUP, 0)
    user32.keybd_event(mod, 0, KEYUP, 0)


def main():
    os.makedirs(os.path.dirname(SAMPLE), exist_ok=True)
    with open(SAMPLE, "w", encoding="utf-8") as f:
        f.write(TEXT)

    # 先清干净已有记事本，否则 Win11 的多标签记事本会把文件塞进旧窗口，
    # 前台窗口还是浏览器，Ctrl+A 会选中整个网页
    subprocess.run(["taskkill", "/IM", "notepad.exe", "/F"], capture_output=True)
    time.sleep(0.4)

    proc = subprocess.Popen(["notepad.exe", SAMPLE])
    time.sleep(2.0)

    # 强制把记事本拉到前台，不然按键会发到别的窗口
    hwnd = user32.FindWindowW("Notepad", None)
    if hwnd:
        user32.ShowWindow(hwnd, 9)          # SW_RESTORE
        user32.SetForegroundWindow(hwnd)
        time.sleep(0.6)

    chord(VK_CONTROL, VK_A)   # 全选
    time.sleep(0.4)

    chord(VK_MENU, VK_D)      # Alt+D 触发翻译
    time.sleep(3.0)           # 等首字上屏

    # 截图。PowerShell 进程默认非 DPI 感知，只能按逻辑分辨率截，
    # 高缩放下会截不完整，但足够看清弹窗是否出现。
    w = user32.GetSystemMetrics(0)
    h = user32.GetSystemMetrics(1)

    ps = (
        "Add-Type -AssemblyName System.Drawing;"
        "$b=New-Object System.Drawing.Bitmap %d,%d;"
        "$g=[System.Drawing.Graphics]::FromImage($b);"
        "$g.CopyFromScreen(0,0,0,0,$b.Size);"
        "$b.Save('%s');"
        "$g.Dispose();$b.Dispose()" % (w, h, SHOT.replace("\\", "\\\\"))
    )
    subprocess.run(["powershell", "-NoProfile", "-Command", ps], check=False)

    print("screenshot:", SHOT, os.path.exists(SHOT))
    time.sleep(0.5)
    proc.terminate()


if __name__ == "__main__":
    main()
