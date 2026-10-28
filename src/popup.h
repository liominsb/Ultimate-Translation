#pragma once
#include <windows.h>
#include <string>

// 悬浮译文框。原则：
//   1) 不抢焦点（WS_EX_NOACTIVATE + SW_SHOWNOACTIVATE），否则源程序的选区会丢
//   2) 不自动消失：译文留着给用户看和抄，点窗口外面才关
//   3) 单击整框 = 复制译文（没焦点就没法拖选，走「一键全复制」）
//   4) 用完即销毁，不做「隐藏常驻」，隐藏窗口仍占着 GDI 句柄和私有堆
//   5) 只用 GDI 双缓冲，不引入 D2D/D3D 渲染目标
void PopupInit(HINSTANCE hInst);

// 在鼠标附近弹窗。source 原文（灰字小字，可空），result 译文/提示语
void PopupShow(POINT screenPt, const std::wstring& source, const std::wstring& result);

// 只换内容不重建窗口：取词完成后把「正在读取…」替换成原文，比重建省一次 CreateWindow
void PopupSetContent(const std::wstring& source, const std::wstring& result);

// 流式更新译文，窗口位置不变，高度变了才重排
void PopupSetResult(const std::wstring& result);

// 状态提示（取词失败、语种不符等）：不可复制，且几秒后自动收
void PopupSetStatus(const std::wstring& hint);

void PopupHide();

// 弹窗是否还活着（用户可能已经点掉，此时流式结果要丢弃）
bool PopupIsVisible();
