/**
 *  SignatureEditModel.cpp
 *
 *  签名提交判定纯逻辑的实现。只做字符串运算（trim / 比较 / 长度），不碰窗口、
 *  网络，与 FlamingoUnit 测试工程共用、不 include stdafx.h，PCH 设 NotUsing。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#include "SignatureEditModel.h"

namespace balloonmain {

SignCommitDecision DecideSignatureCommit(const CString& edited,
                                         const CString& current,
                                         int maxLen)
{
    SignCommitDecision d;
    d.trimmed = edited;
    d.trimmed.Trim();   // 去首尾空白后再比较 / 判长

    if (d.trimmed.GetLength() > maxLen)
    {
        // 超长：拦截（实际编辑框有 SetMaxLength 兜底，这里防御 + 便于测试）。
        d.action = kSignCommitTooLong;
        return d;
    }

    if (d.trimmed == current)
    {
        // 与原值相同（含两边都为空）：不发包，仅收起编辑。
        d.action = kSignCommitNoChange;
        return d;
    }

    // 与原值不同且未超长：发送更新（trim 后可能为空串，即"清空签名"，允许）。
    d.action = kSignCommitSend;
    return d;
}

} // namespace balloonmain
