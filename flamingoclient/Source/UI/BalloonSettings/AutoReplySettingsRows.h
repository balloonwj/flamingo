/**
 *  AutoReplySettingsRows.h
 *
 *  系统设置「聊天」页里自动回复的输入框行：「离开时自动回复」「忙碌时自动回复」两个开关行的下方，
 *  各有一行占满内容区宽度的多行输入框，填回复的文字。开关关闭时输入框置灰、不能编辑。
 *  开关行本身由设置窗口的 AddSwitchRow 生成，与本页其它开关同一套样式；本文件只负责输入框那一行，
 *  以及开关与输入框的联动。单独成文件是为了让 FlamingoUnit 测试工程不建设置窗口也能构造这些控件、
 *  核对联动与排版（Tests/AutoReplySettingsRowsTests.cpp）。
 *
 *  2026-10-03 与用户确认：两种状态各一个开关、各一段文字；输入框单独占一行、多行显示，
 *  设置窗口为此加高（见 BalloonSystemSettingDlg.cpp 的 kDlgClientH）。
 *
 *  典型用法（设置窗口里）：
 *      AddSwitchRow(page, _T("离开时自动回复"), ..., kIdAutoReplyAway, on, false);
 *      balloonsettings::AutoReplyEditSpec spec;
 *      spec.m_editId = kIdAutoReplyAwayText;
 *      spec.m_on = on;
 *      spec.m_text = cfg->GetAutoReplyAwayText();
 *      spec.m_placeholder = balloonmain::kDefaultAwayReplyText;
 *      page->AddChild(balloonsettings::BuildAutoReplyEditRow(spec),
 *                     balloonwjui::DuiLayout::Hint().Fixed(balloonsettings::kAutoReplyEditRowH));
 *      //开关切换之后
 *      balloonsettings::ApplyAutoReplyInputEnabled(content, kIdAutoReplyAway, kIdAutoReplyAwayText);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>
#include <memory>
#include <string>

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"   // DuiVBox

namespace balloonwjui { class DuiControl; }

namespace balloonsettings {

//多行输入框的高度（像素）：默认字号下放得下两到三行，100 个字以内的回复不用滚动就能看全
const int kAutoReplyEditH = 56;

//输入框下方到本组分隔线的间距（像素），让输入框不贴着分隔线
const int kAutoReplyEditBottomGap = 10;

//输入框那一行的总高度（像素）= 输入框高 + 下方间距，设置窗口按它给这一行定高、算窗口高度
const int kAutoReplyEditRowH = kAutoReplyEditH + kAutoReplyEditBottomGap;

/**
 *  构造输入框那一行所需的参数。
 */
struct AutoReplyEditSpec
{
    UINT           m_editId;        // 输入框的控件 id；文字变化时发 DUIN_VALUECHANGED，失去焦点时发 DUIN_KILLFOCUS
    bool           m_on;            // 对应开关的初始状态；为 false 时输入框置灰
    std::wstring   m_text;          // 输入框的初始文字，即配置里存的原文；为空时显示占位提示
    const wchar_t* m_placeholder;   // 占位提示：输入框为空时显示的灰字，即此时会发送的默认文字；可为 NULL

    AutoReplyEditSpec()
        : m_editId(0)
        , m_on(false)
        , m_placeholder(NULL)
    {
    }
};

/**
 *  构造一行占满宽度的多行输入框（自动换行、回车换行），下方留 kAutoReplyEditBottomGap 的间距。
 *  输入框限长 kAutoReplyMaxChars 个字符，初始是否可编辑跟随 spec.m_on。
 *    spec：构造参数，只读。
 *  @return 新建的竖直容器（调用方拥有），高度应按 kAutoReplyEditRowH 给定。
 */
std::unique_ptr<balloonwjui::DuiVBox> BuildAutoReplyEditRow(const AutoReplyEditSpec& spec);

/**
 *  按开关的当前状态设置输入框能否编辑：开关打开时可编辑，关闭时置灰。只影响这一组，
 *  另一组不受影响。
 *    root：    在其下按 id 查找控件的根，不持有；为 NULL 或找不到控件时什么都不做。
 *    switchId：开关的控件 id。
 *    editId：  输入框的控件 id。
 */
void ApplyAutoReplyInputEnabled(balloonwjui::DuiControl* root, UINT switchId, UINT editId);

/**
 *  把输入框里的文字整理成存进配置的原文：截到 kAutoReplyMaxChars 个字符以内（粘贴的长文字
 *  不一定受输入框限长约束），其余原样保留 —— 去首尾空白、空白改用默认文字在发送时才做。
 *    editText：输入框里的文字；NULL 按空串处理。
 *  @return 要存进配置的文字。
 */
std::wstring AutoReplyTextForConfig(const wchar_t* editText);

} // namespace balloonsettings
