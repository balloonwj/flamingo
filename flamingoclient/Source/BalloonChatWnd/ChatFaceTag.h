/**
 *  ChatFaceTag.h
 *
 *  气泡聊天窗里"内联表情"在 DuiRichEditHost 中以"带标记内联图"（
 *  DuiRichEditHost::InsertTaggedImage，标记落在 REOBJECT.dwUser）承载。本文件约定
 *  该标记的编码，使发送时能把内嵌对象区分成"系统表情"与"截图等普通内联图"：
 *    · dwUser == 0                  —— 非表情（如截图）；
 *    · dwUser == kFaceTagBase+faceId —— 一个系统表情（faceId 见 Face/FaceConfig.xml，0..142）。
 *  基址取一个远离 0、不易与其它 dwUser 用途相撞的值；faceId 用加法编码而非直接存
 *  faceId，是为了让 faceId==0（face0.png）也能与"无标记(0)"区分开。
 *
 *  同时约定内联表情在输入框 / 气泡里的统一显示边长，保证输入框所见即所得与气泡
 *  渲染一致。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#pragma once

#include <windows.h>   // DWORD_PTR

namespace balloonchatwnd {

// 系统表情标记基址（"0F1C" 取意 face）。faceId 范围 0..142，远不及 10 万的安全区间。
const DWORD_PTR kFaceTagBase = 0x0F1C0000;

// 内联表情统一显示边长（逻辑像素）。
//
// Bin/Face 下的 face<N>.png 原生是 56×56（143 张里 141 张 56×56、2 张 48×48），
// 取 28 是因为 56→28 正好是 1/2 整数倍缩小 —— 每个目标像素恰好对应源图 2×2 的
// 方块，重采样几乎无损；换成 24 之类的非整数倍会引入插值模糊。28px 与正文行高
// 也仍然协调。
//
// 早期这里的注释误写成"face PNG 原生约 28px"，据此把显示边长定成了 24，属于建立
// 在错误前提上的取值，现已一并纠正。
const int kFaceDisplayPx = 28;

// faceId → dwUser 标记。
inline DWORD_PTR FaceTagFromId(int faceId)
{
    return kFaceTagBase + (DWORD_PTR)faceId;
}

// 某 dwUser 标记是否为系统表情。
inline bool IsFaceTag(DWORD_PTR tag)
{
    return tag >= kFaceTagBase && tag < kFaceTagBase + 100000;
}

// dwUser 标记 → faceId（调用前须 IsFaceTag 为真）。
inline int FaceIdFromTag(DWORD_PTR tag)
{
    return (int)(tag - kFaceTagBase);
}

} // namespace balloonchatwnd
