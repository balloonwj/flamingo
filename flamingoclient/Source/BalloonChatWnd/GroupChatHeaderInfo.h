/**
 *  GroupChatHeaderInfo.h
 *
 *  群聊窗头部左侧的信息块 —— 群名 + (N 人) + 📌 群公告。对照 design 08。
 *  纯绘制控件，三项内容由宿主窗口在群资料 / 群成员 / 群公告到达时写进来。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonchatwnd {

/**
 *  GroupChatHeaderInfo —— 群聊头部左侧信息块（群名 / 人数 / 公告）。
 */
class GroupChatHeaderInfo : public balloonwjui::DuiControl
{
public:
    GroupChatHeaderInfo();

    /**
     *  设置群名（第一行主标题）。
     *    name：群名；传 NULL 或空串时该行不画。
     */
    void SetGroupName(LPCTSTR name);

    /**
     *  设置成员数（群名右侧的「(N 人)」）。
     *    count：成员数；<= 0 时不画这一段（群成员应答还没回来时即为此态）。
     */
    void SetMemberCount(int count);

    /**
     *  设置公告摘要（第二行，前面带一枚图钉）。
     *
     *  取的是<u>最新一条已发布公告的标题</u>而不是正文 —— 这一行只有一行的宽度，
     *  标题本就是为"一句话说清楚"写的，正文塞进来多半只能看到开头几个字。
     *  数据来自群资料里的公告快照（announcetitle），开窗时随 1303 一起到达。
     *    title：公告标题；传 NULL 或空串时整行不画，群名行随之垂直居中。
     */
    void SetAnnouncementTitle(LPCTSTR title);

    // DuiControl 覆写：绘制群名 + 人数 + 公告。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    CString m_groupName;      // 群名
    int     m_memberCount;    // 成员数；<= 0 表示还不知道，不画「(N 人)」
    CString m_announceTitle;  // 最新公告标题；空表示本群没有公告，整行不画
};

} // namespace balloonchatwnd
