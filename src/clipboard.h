#pragma once
#include <string>

// 剪贴板纯文本读写。取词（Ctrl+C 模拟）和「单击复制译文」共用一份实现，
// 免得两处各写一遍 OpenClipboard / SetClipboardData 的所有权处理。

// 只读 CF_UNICODETEXT。取不到就返回空串，调用方负责重试。
std::wstring ReadClipboardText();

// 写纯文本。空串会清空剪贴板。失败（被别人占用）时静默返回，不抛错。
void WriteClipboardText(const std::wstring& s);
