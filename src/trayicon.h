#pragma once
#include <windows.h>

// 运行时用 GDI 画一个托盘图标（深色圆角底 + 白字「译」）。
// 不打包 .ico 文件：省一个资源文件，也避免 exe 里塞二进制。
// 返回的 HICON 由调用方 DestroyIcon 释放。
HICON CreateTrayIcon(int size = 32);
