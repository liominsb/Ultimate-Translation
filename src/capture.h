#pragma once
#include <string>

// 取当前前台窗口里被鼠标选中的文本。
//
// 实现方式：备份剪贴板 -> 模拟 Ctrl+C -> 等剪贴板变化 -> 读文本 -> 还原。
// 之所以不用 UI Automation：Chrome 需要开可访问性、PDF 阅读器和游戏基本取不到，
// 而 Ctrl+C 在 Chrome / VSCode / Office / Windows Terminal 里都稳定可用。
//
// 空串代表没取到（没选中 / 目标程序不支持复制 / 超时）。
// 超时给到 350ms：Chromium 系（Chrome / Electron）写剪贴板比记事本慢半拍。
std::wstring CaptureSelectedText(int timeoutMs = 350);
