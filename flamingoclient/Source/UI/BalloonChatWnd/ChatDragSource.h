/**
 *  从聊天记录里往外拖内容（图片、选中的文字与表情），ChatDragSource.h
 *
 *  拖出去的数据与复制时写进剪贴板的完全相同（见 ChatClipboardIo.h）：纯文字 + Flamingo
 *  聊天内容格式；拖的恰好是一张图片时再带一份文件列表（CF_HDROP），可以直接拖到桌面、
 *  资源管理器或其它程序里。允许的效果只有"复制"，不会把缓存里的原文件移走。聊天窗接到
 *  带 Flamingo 格式的内容时插入输入框，不直接发送（见 ChatDropController.h）。
 *
 *  本文件分两部分：
 *    · 纯判断 IsChatDragOrigin / IsBeyondDragThreshold：按下点能不能作为拖拽起点、移动
 *      距离够不够开始拖。由 FlamingoUnit 直接断言（Tests/ChatDragSourceTests.cpp）；
 *    · OLE：CreateChatDragDataObject 装配拖拽数据（同样有单元测试，在内存里直接查各种格式，
 *      不碰剪贴板），DoChatDragDrop 发起拖拽（模态，只能人工验证）。
 *
 *  典型用法（聊天气泡）：
 *      按下时：m_dragArmed = IsChatDragOrigin(kind, inSel, onImage, imageReady);
 *      移动时：if (m_dragArmed && IsBeyondDragThreshold(dx, dy, cx, cy)) { 释放捕获; DoChatDragDrop(segs); }
 *
 *  本文件不 include stdafx.h（PCH 设 NotUsing），与 FlamingoUnit 测试工程共用；包含它之前
 *  调用方须已引入 ATL 的 CString。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#pragma once

#include <windows.h>
#include <ole2.h>
#include <vector>

#include "ChatClipboardContent.h"   // ChatClipSegment

namespace balloonchatwnd {

/**
 *  按下鼠标的位置能不能作为一次拖拽的起点。
 *    kind：            气泡的消息类型（ChatMockData.h 的 ChatMsgKind 取值）。
 *    pressInSelection：文字气泡：按下点是否落在已有的文字选区内。
 *    pressOnImage：    图片气泡：按下点是否落在图片矩形内。
 *    imageReady：      图片气泡：本地原图是否可用（状态为就绪、且文件存在）。
 *  返回：可以作为拖拽起点时返回 true。纯函数，无副作用。
 *  规则：文字气泡（含大表情）只有按在选区内才算 —— 按在选区外仍是重新选择文字；图片气泡
 *        只有按在图片上、且原图可用才算；其余类型一律不算。
 */
bool IsChatDragOrigin(int kind, bool pressInSelection, bool pressOnImage, bool imageReady);

/**
 *  鼠标按下后移动的距离是否已超过系统的拖动阈值。超过才真正开始拖，避免手一抖把单击、
 *  双击当成拖拽。
 *    dx / dy：              相对按下点的位移（像素，可为负）。
 *    thresholdX / thresholdY：系统拖动阈值，取 GetSystemMetrics(SM_CXDRAG / SM_CYDRAG)，
 *                          含义是"按下点两侧各允许移动的像素数"。
 *  返回：任一方向的位移绝对值超过对应阈值时返回 true。纯函数，无副作用。
 */
bool IsBeyondDragThreshold(int dx, int dy, int thresholdX, int thresholdY);

/**
 *  为一次拖拽装配数据对象：纯文字、Flamingo 聊天内容格式；恰好只有一张图片时再带一份文件
 *  列表（CF_HDROP），并声明首选效果为"复制"（拖到资源管理器时复制文件而不是移动）。
 *    segs：片段序列，不能为空。
 *  @return 数据对象，引用计数为 1，调用方用完须 Release；segs 为空或创建失败时返回 NULL。
 *  前置条件：调用线程已初始化 COM（客户端启动时已 OleInitialize）。
 */
IDataObject* CreateChatDragDataObject(const std::vector<ChatClipSegment>& segs);

/**
 *  发起一次拖拽。模态：拖拽结束（松手或按 Esc）才返回。允许的效果只有"复制"。
 *    segs：片段序列；为空时不发起，直接返回 DROPEFFECT_NONE。
 *  @return 最终的拖放效果：DROPEFFECT_COPY 表示被某处接收；DROPEFFECT_NONE 表示取消或
 *          没有人接收。
 *  前置条件：调用线程已 OleInitialize；调用方须先释放鼠标捕获，否则拖拽期间收不到鼠标。
 */
DWORD DoChatDragDrop(const std::vector<ChatClipSegment>& segs);

} // namespace balloonchatwnd
