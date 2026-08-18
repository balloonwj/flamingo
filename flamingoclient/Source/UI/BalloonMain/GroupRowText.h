/**
 *  GroupRowText.h
 *
 *  群列表行的文案拼装。把"第一行显示什么、第二行显示什么"这件可预判的事抽成
 *  纯函数，供主面板"群"Tab 的列表（BalloonMainPanel::FillGroupsTree）与搜索
 *  下拉的群结果行（BalloonSearch/SearchDropdown）共用，避免两处各写一份格式串
 *  导致口径漂移；同时让 FlamingoUnit 测试工程能脱离窗口 / GDI 直接断言
 *  （对照 BadgeRender 的纯函数 + 单测组织方式）。
 *
 *  行的版式约定（两处一致）：
 *      第一行：群名（左） + 成员数（右）
 *      第二行：群号
 *
 *  典型用法：
 *      GroupRowTexts t = BuildGroupRowTexts(name, memberCount, groupId);
 *      tv->SetItemLabel(id, t.m_name);              // 第一行左
 *      tv->SetItemRightTopText(id, t.m_memberText); // 第一行右
 *      tv->SetItemSubLabel(id, t.m_groupNoText);    // 第二行
 *
 *  文字画在哪一像素、超长怎么截断都由各自的绘制层负责，本模块只出字符串、
 *  不碰任何绘制 / 窗口 API。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#pragma once

#include <atlstr.h>       // CString

namespace balloonmain {

/**
 *  一条群列表行要显示的三段文字。空串表示该段不显示，调用方据此跳过绘制。
 */
struct GroupRowTexts
{
    CString m_name;         // 第一行左侧：群名。群名缺失时已回落为占位名，恒非空
    CString m_memberText;   // 第一行右侧：成员数，形如 "12 人"。人数未知时为空串
    CString m_groupNoText;  // 第二行：群号，形如 "群号 700001"。群号非法时为空串
};

/**
 *  拼一条群列表行的显示文案。
 *    name         群名；调用方保留所有权，本函数只读不存。可传 NULL 或空串，此时
 *                 按群号回落为占位名 "群聊(<群号>)"，群号也非法时回落为 "群聊"。
 *                 传入的群名<u>原样</u>返回、不做长度截断 —— 截断是绘制层的事
 *                 （DrawText 的 DT_END_ELLIPSIS）。
 *    memberCount  群成员总数，单位为人；<= 0 视为"人数未知"，成员数文字返回空串
 *                 （服务端尚未下发成员数时，宁可不显示，也不显示一个骗人的 "0 人"）。
 *    groupId      群号（即群 id）；<= 0 视为非法，群号文字返回空串。
 *  返回：三段文案，见 GroupRowTexts。纯函数，无副作用。
 */
GroupRowTexts BuildGroupRowTexts(LPCTSTR name, int memberCount, __int64 groupId);

} // namespace balloonmain
