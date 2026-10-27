#pragma once
#include <windows.h>
#include <string>

// 悬浮译文框。原则：
//   1) 不抢焦点（WS_EX_NOACTIVATE + SW_SHOWNOACTIVATE），否则源程序的选区会丢
//   2) 用完即销毁，不做「隐藏常驻」，隐藏窗口仍占着 GDI 句柄和私有堆
//   3) 只用 GDI 双缓冲，不引入 D2D/D3D 渲染目标
void PopupInit(HINSTANCE hInst);

// 在鼠标附近弹窗。source 原文（灰字小字，可空），result 译文/提示语
void PopupShow(POINT screenPt, const std::wstring& source, const std::wstring& result);

// 流式更新译文，窗口位置不变，高度随内容重新计算
void PopupSetResult(const std::wstring& result);

void PopupHide();

// 弹窗是否还活着（用户可能已经点掉或超时关闭，此时流式结果要丢弃）
bool PopupIsVisible();
