/**
 *  ChatHeaderInfo.h
 *
 *  单聊窗头部左侧的信息块 —— 对方昵称 + 在线状态胶囊 + 个性签名。对照
 *  design 07 的聊天头部。纯绘制控件，数据取自 ChatMockData。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../balloonui/balloonui/DuiControl.h"

namespace balloonchatwnd {

/**
 *  ChatHeaderInfo —— 聊天头部左侧信息块（昵称 / 状态 / 签名）。
 */
class ChatHeaderInfo : public balloonwjui::DuiControl
{
public:
    ChatHeaderInfo();

    // 注入对方真实在线状态（v2 presence_type，见 net/Protocol.h）。一旦调用即切到"真实
    // 数据"模式：OnPaint 不再用 ChatMockData 的 mock 状态，而按本值画状态点 / 文字。
    // 单聊窗打开时由 CBalloonChatWindow 从通讯录缓存注入，收到 1109 推送时再更新。
    void SetPresence(int presence);

    // 注入对方真实个性签名（同样切到"真实数据"模式；空串表示该联系人无签名，则不画签名行，
    // 避免真实状态旁边残留 mock 签名）。
    void SetSignature(const CString& signature);

    // 设置对方是否正在输入（收到 cmd_typing_notify 推送时由单聊窗调用）。为 true 时
    // 状态胶囊改画成主题色的"正在输入…"，取代在线 / 离开 / 忙碌那一档文字；为 false
    // 时恢复按 presence 画。本方法只改数据，重绘由调用方 Invalidate 触发。
    //   typing：对方当前是否正在输入。
    void SetTyping(bool typing);

    // DuiControl 覆写：绘制昵称 + 状态胶囊 + 签名。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：整块可点击 —— 点击发 DUIN_CLICK（ctrlId=kIdHeaderInfo），
    // 单聊窗据此打开对方的联系人资料窗。
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;

private:
    bool    m_hasRealPeer = false;  // 是否已注入真实对端数据（决定走真实值还是回退 mock）
    int     m_presence    = 0;      // 对方在线状态(v2 presence_type)；仅 m_hasRealPeer 时有意义
    CString m_signature;            // 对方个性签名；仅 m_hasRealPeer 时有意义
    bool    m_typing      = false;  // 对方是否正在输入；为 true 时状态胶囊改画"正在输入…"
};

} // namespace balloonchatwnd
