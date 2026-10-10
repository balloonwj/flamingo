/**
 *  GroupChatHeaderInfo.h
 *
 *  群聊窗头部左侧的信息块 —— 群简介。对照 design 08。
 *  纯绘制控件，群简介由宿主窗口在群资料到达（1303 应答、1318 资料变更推送）时写进来。
 *  2026-10-03 起不再画群名与「(N 人)」：窗口标题栏已经显示群名，头部再画一遍是重复的，群名很长时还会压到右侧的
 *  图标按钮上（按用户要求去掉，空出的位置留空）。
 *  2026-10-04 起这一行由「📌 最新公告标题」改为群简介（按用户要求）：只占一行，过长时末尾以省略号截断，
 *  始终不越过右侧的图标按钮。公告仍可在右侧边栏的「公告」页签查看。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonchatwnd {

/**
 *  把群简介整理成头部能显示的一行文字。纯函数，便于单元测试直接断言。
 *
 *  群简介在群管理窗里是多行输入框填写的，可能含换行与制表符；头部只有一行，
 *  这里把每一段连续的换行 / 回车 / 制表符换成一个空格，再去掉首尾的空白
 *  （半角空格与全角空格 U+3000）。其余字符（包括 &）原样保留。
 *    intro：群简介原文；传 NULL 视同空串。
 *  返回：整理后的一行文字；原文为空或只有空白时返回空串。
 */
CString MakeHeaderIntroLine(LPCTSTR intro);

/**
 *  GroupChatHeaderInfo —— 群聊头部左侧信息块（群简介）。
 *
 *  宽度由所在的头部横向布局决定（weight=1，右边界止于第一个图标按钮），
 *  文字只在本控件矩形内绘制，过长时末尾以省略号截断。
 */
class GroupChatHeaderInfo : public balloonwjui::DuiControl
{
public:
    GroupChatHeaderInfo();

    /**
     *  设置群简介（一行，灰色，在信息块里垂直居中）。
     *
     *  数据来自群资料里的 intro 字段，开窗时随 1303 到达，群主或管理员在群管理窗里
     *  修改后随 1318 推送到达。内部先经 MakeHeaderIntroLine 整理成一行。
     *    intro：群简介原文；传 NULL、空串或只有空白时整行不画，信息块为空白。
     *  副作用：请求重绘本控件。
     */
    void SetGroupIntro(LPCTSTR intro);

    // DuiControl 覆写：绘制群简介这一行；没有群简介时什么也不画。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    CString m_introLine;  // 整理成一行后的群简介；空表示本群没有填写群简介，整行不画
};

} // namespace balloonchatwnd
