/**
 *  AutoReplySettingsRows.cpp
 *
 *  「离开 / 忙碌时自动回复」输入框行的实现，用途与用法见 AutoReplySettingsRows.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "stdafx.h"
#include "AutoReplySettingsRows.h"

#include "../BalloonMain/AutoReplyPolicy.h"   // kAutoReplyMaxChars / ClampAutoReplyText
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSwitch.h"

namespace balloonsettings {

std::unique_ptr<balloonwjui::DuiVBox> BuildAutoReplyEditRow(const AutoReplyEditSpec& spec)
{
    std::unique_ptr<balloonwjui::DuiVBox> row(new balloonwjui::DuiVBox());
    row->SetGap(0);

    //多行输入框：自动换行，回车插入换行（DuiEdit 默认是单行，构造后切成多行）；限长与发送前的截断同一个上限
    std::unique_ptr<balloonwjui::DuiEdit> edit(new balloonwjui::DuiEdit());
    edit->SetCtrlId(spec.m_editId);
    edit->SetMultiLine(true);
    edit->SetWordWrap(true);
    edit->SetMaxLength((int)balloonmain::kAutoReplyMaxChars);
    if (spec.m_placeholder != NULL)
    {
        edit->SetPlaceholder(spec.m_placeholder);
    }
    //初始文字不发通知：这是把配置里的值显示出来，不是用户改了内容
    edit->SetTextNoNotify(spec.m_text.c_str());
    edit->SetEnabled(spec.m_on);
    row->AddChild(std::move(edit), balloonwjui::DuiLayout::Hint().Fixed(kAutoReplyEditH));

    //下方间距：输入框不贴着本组的分隔线
    row->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                  balloonwjui::DuiLayout::Hint().Fixed(kAutoReplyEditBottomGap));
    return row;
}

void ApplyAutoReplyInputEnabled(balloonwjui::DuiControl* root, UINT switchId, UINT editId)
{
    if (root == NULL)
    {
        return;
    }
    balloonwjui::DuiSwitch* sw = dynamic_cast<balloonwjui::DuiSwitch*>(root->FindCtrlById(switchId));
    balloonwjui::DuiControl* edit = root->FindCtrlById(editId);
    if (sw == NULL || edit == NULL)
    {
        return;
    }
    //置灰后 DuiEdit 改用禁用底色并不再接受输入，用户能直接看出这段文字此刻不会发出去
    edit->SetEnabled(sw->IsChecked());
}

std::wstring AutoReplyTextForConfig(const wchar_t* editText)
{
    return balloonmain::ClampAutoReplyText((editText != NULL) ? std::wstring(editText) : std::wstring());
}

} // namespace balloonsettings
