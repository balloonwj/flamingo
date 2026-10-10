/**
 *  带悬停说明的搜索框实现，说明见 HistoryTipSearchBox.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
//本文件被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用、不走预编译头，自己引入 balloonui 头文件约定要求的 ATL/WTL
#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>
#include <gdiplus.h>
#include "HistoryTipSearchBox.h"

#include "../../Common/I18n/I18n.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"

namespace balloonhistory {

namespace {

//完整搜索语法的说明，每行一条规则（'\n' 分行，悬停提示按多行显示）
const wchar_t* const kSyntaxTip = TR_NOOP(L"空格分隔多个词：同时包含才算命中\n-词：排除含这个词的消息\n“带 空格”：引号括起的部分是一个词\nfrom:名字：只看这个人发的\ntype:图片 / 文件 / 语音 / 链接 / 文字 / Pin\nafter:2026-10-01  before:2026-10-31（含当天）");

//文件名搜索的说明
const wchar_t* const kFileNameTip = TR_NOOP(L"空格分隔多个词：文件名同时包含才算命中\n-词：排除文件名含这个词的\n“带 空格”：引号括起的部分是一个词");

} // namespace

HistoryTipSearchBox::HistoryTipSearchBox(LPCTSTR tip)
{
    SetTipText(tip);
}

HistoryTipSearchBox::~HistoryTipSearchBox()
{
    balloonwjui::DuiToolTipMgr::Inst().Unregister(this);
}

void HistoryTipSearchBox::SetTipText(LPCTSTR tip)
{
    if (tip == NULL || tip[0] == 0)
    {
        balloonwjui::DuiToolTipMgr::Inst().Unregister(this);
        return;
    }
    balloonwjui::DuiToolTipMgr::Inst().Register(this, tip);
}

LPCTSTR HistorySearchSyntaxTip()
{
    return TR(kSyntaxTip);
}

LPCTSTR HistoryFileNameSearchTip()
{
    return TR(kFileNameTip);
}

} // namespace balloonhistory
