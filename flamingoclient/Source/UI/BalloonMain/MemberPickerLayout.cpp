/**
 *  MemberPickerLayout.cpp
 *
 *  选人对话框的控件树构建，以及「入群后可查看的聊天记录」选项组的取值规则。说明见 MemberPickerLayout.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用。
 *  在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-04
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（与 MemberPickerRow.cpp 相同）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include "MemberPickerLayout.h"
#include "MemberPickerTreeView.h"   // 左侧候选树（滚轮上抛 + 复选框点击判定）
#include "../../Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找
#include "../../Feature/ThreadGroup/ThreadGroupGate.h"   // kGroupTypeNormal / kGroupTypeThread：建群请求的群类型
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"      // DuiVBox / DuiHBox / Hint
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"   // GetDefaultFontForDpi：量单选按钮文字宽度用的默认字体
#include "../../../third-party/balloonui/balloonui/DuiDpi.h"      // DuiDpi::kDefaultDpi
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView

namespace balloonmain {

namespace {

// ---- 尺寸（逻辑像素）与颜色 ----------------------------------------------
const int kRootPadding   = 16;   // 对话框内容四周的留白
const int kRootGap       = 12;   // 上半区与底部栏之间的间距
const int kColumnGap     = 16;   // 左右两列之间的间距
const int kInColumnGap   = 8;    // 列内各行之间的间距
const int kEditRowH      = 34;   // 搜索 / 群名输入框高（单行）
const int kMemberRowH    = 44;   // 候选树的行高
const int kSectionTitleH = 20;   // 「已选成员」与选项组标题的行高
const int kBottomBarH    = 40;   // 底部栏高
const int kBottomGap     = 10;   // 底部栏内各项之间的间距
const int kCancelBtnW    = 88;   // 取消按钮宽
const int kConfirmBtnW   = 110;  // 确认按钮宽
const COLORREF kSectionColor = RGB(0x9A, 0x9A, 0x9A);   // 标题、计数、说明文字的灰色
const COLORREF kEditBgColor  = RGB(255, 255, 255);      // 输入框底色

// 选项组
const int kHistoryGroupGap  = 6;    // 选项组内各行之间的间距
const int kRadioRowH        = 24;   // 「全部聊天记录」「不同步」单选按钮的行高
const int kCountRowH        = 30;   // 「最近 N 条」一行的行高（放得下条数输入框）
const int kCountRowGap      = 6;    // 「最近 N 条」一行里单选按钮、输入框、后缀之间的间距
const int kRadioTextLeft    = 28;   // 单选按钮文字的左起点：左留白 6 + 圆点 16 + 间距 6（balloonui DuiButton.cpp 的 kGlyphPadPx、kGlyphSizePx）
const int kRadioTextSlack   = 4;    // 量出的文字宽度之外再留的余量，避免取整误差把最后一个字挤成省略号
const int kCountEditW       = 64;   // 条数输入框宽，放得下 5 位数字
const int kHintRowH         = 34;   // 说明行高：两行小字（中文一行放得下，译文变长时折成两行）
const int kCountMaxDigits   = 5;    // 条数输入框最多几位数字（上限 10000 是 5 位）

// 群类型选项组
const int kGroupTypeRadioGap   = 16;   // 「普通群」与「帖子群」两个单选按钮之间的距离
const int kGroupTypeRadioGroup = 1;    // 两个单选按钮的组号（balloonui 在同一父容器、同一组号内互斥）
const int kGroupTypeHintH      = 44;   // 说明行高：两行字（2026-10-07 实机截图，34 高时第二行被裁掉下半截）

/**
 *  只接受数字的条数输入框。balloonui 的 DuiEdit 没有「只能输入数字」的开关，这里子类化后在 OnChar 里
 *  丢弃数字以外的可见字符；退格、回车等控制字符照常交给基类。粘贴进来的非数字仍可能出现，由
 *  ParseInviteHistoryLimit 在提交时判为不合法。
 */
class InviteCountEdit : public balloonwjui::DuiEdit
{
public:
    /**
     *  键盘输入的字符。
     *  @param ch 输入的字符
     *  @return 已处理返回 true（数字以外的可见字符直接丢弃并返回 true）
     */
    bool OnChar(TCHAR ch) override
    {
        if (ch >= _T(' ') && (ch < _T('0') || ch > _T('9')))
            return true;
        return balloonwjui::DuiEdit::OnChar(ch);
    }
};

//一行灰色的小标题 / 说明标签
std::unique_ptr<balloonwjui::DuiLabel> MakeGreyLabel(const CString& text, UINT ctrlId, UINT align)
{
    std::unique_ptr<balloonwjui::DuiLabel> label(new balloonwjui::DuiLabel());
    if (ctrlId != 0)
        label->SetCtrlId(ctrlId);
    label->SetText(text);
    label->SetTextColor(kSectionColor);
    label->SetTextAlign(align);
    return label;
}

//一个单选按钮（Ghost 变体只画小圆点，不铺整格浅色底，与成员行复选框一致）。三个单选按钮不在同一个父容器里，
//balloonui 只在同一父容器内互斥，所以组号留 0，互斥由 SelectInviteHistoryMode 负责
std::unique_ptr<balloonwjui::DuiButton> MakeHistoryRadio(UINT ctrlId, const CString& text)
{
    std::unique_ptr<balloonwjui::DuiButton> radio(new balloonwjui::DuiButton());
    radio->SetButtonType(balloonwjui::DuiButton::StyleRadio);
    radio->SetVariant(balloonwjui::DuiButton::Variant::Ghost);
    radio->SetCtrlId(ctrlId);
    radio->SetText(text);
    return radio;
}

//单选按钮正好放下文字的宽度：圆点与左留白、文字宽度（按控件默认字体，96 dpi 即逻辑像素）、余量
int RadioWidthForText(const CString& text)
{
    SIZE size = { 0, 0 };
    HDC hdc = ::GetDC(NULL);
    if (hdc != NULL)
    {
        HFONT font = balloonwjui::DuiResMgr::Inst().GetDefaultFontForDpi(balloonwjui::DuiDpi::kDefaultDpi);
        HGDIOBJ oldFont = (font != NULL) ? ::SelectObject(hdc, font) : NULL;
        ::GetTextExtentPoint32W(hdc, (LPCWSTR)text, text.GetLength(), &size);
        if (oldFont != NULL)
            ::SelectObject(hdc, oldFont);
        ::ReleaseDC(NULL, hdc);
    }
    return kRadioTextLeft + size.cx + kRadioTextSlack;
}

//「群类型」选项组（docs/帖子群方案.md 7.2）：标题、同一行的「普通群」「帖子群」两个单选按钮、说明行。
//两个单选按钮在同一个父容器里，用 balloonui 的组号互斥
std::unique_ptr<balloonwjui::DuiVBox> BuildGroupTypeGroup(MemberPickerLayoutRefs& refs)
{
    std::unique_ptr<balloonwjui::DuiVBox> group(new balloonwjui::DuiVBox());
    group->SetGap(kHistoryGroupGap);
    group->AddChild(MakeGreyLabel(TR(L"群类型"), kPickerIdGroupTypeTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                    balloonwjui::DuiLayout::Hint().Fixed(kSectionTitleH));

    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetGap(kGroupTypeRadioGap);
    //两个单选按钮平分这一行：右栏有三百多像素宽，放下译文绰绰有余。不按测出的文字宽度占位——2026-10-07 实机截图里
    //按钮画出的字比 96 dpi 默认字体测出的宽，最后一个字被截掉一半
    std::unique_ptr<balloonwjui::DuiButton> normal = MakeHistoryRadio(kPickerIdGroupTypeNormal, TR(L"普通群"));
    normal->SetRadioGroup(kGroupTypeRadioGroup);
    normal->SetCheck(true, false);   // notify=false：设初值不触发 VALUECHANGED
    refs.m_groupTypeNormal = normal.get();
    row->AddChild(std::move(normal), balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiButton> thread = MakeHistoryRadio(kPickerIdGroupTypeThread, TR(L"帖子群"));
    thread->SetRadioGroup(kGroupTypeRadioGroup);
    refs.m_groupTypeThread = thread.get();
    row->AddChild(std::move(thread), balloonwjui::DuiLayout::Hint().Weight(1));
    group->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kRadioRowH));

    group->AddChild(MakeGreyLabel(TR(L"帖子群没有聊天消息，成员发帖并在帖子下留言讨论；建好后不能改成普通群。"),
                                  kPickerIdGroupTypeHint, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX),
                    balloonwjui::DuiLayout::Hint().Fixed(kGroupTypeHintH));
    return group;
}

//「入群后可查看的聊天记录」选项组：标题、三个单选项（全部 / 最近 N 条 / 不同步）、说明行。
//2026-10-04 起：此前是「同步全部」勾选框加一行「仅同步最近 N 条」，不同步要在输入框里填 0，用户反馈让人迷惑
std::unique_ptr<balloonwjui::DuiVBox> BuildInviteHistoryGroup(const MemberPickerLayoutSpec& spec,
                                                              MemberPickerLayoutRefs& refs)
{
    std::unique_ptr<balloonwjui::DuiVBox> group(new balloonwjui::DuiVBox());
    group->SetGap(kHistoryGroupGap);
    InviteHistoryControls& controls = refs.m_history;

    group->AddChild(MakeGreyLabel(TR(L"入群后可查看的聊天记录"), kPickerIdHistoryTitle,
                                  DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                    balloonwjui::DuiLayout::Hint().Fixed(kSectionTitleH));

    //「全部聊天记录」：默认选中
    std::unique_ptr<balloonwjui::DuiButton> all = MakeHistoryRadio(kPickerIdHistoryAll, TR(L"全部聊天记录"));
    all->SetCheck(true, false);   // notify=false：设初值不触发 VALUECHANGED
    controls.m_all = all.get();
    group->AddChild(std::move(all), balloonwjui::DuiLayout::Hint().Fixed(kRadioRowH));

    //「最近 [ 100 ] 条」：单选按钮按文字宽度占位（见 InviteRecentRadioWidth），后缀用带上下文的 TRC：
    //各语言的语序不同（如日文「直近 [N] 件」），「条」单独作为原文也容易与别处的同名文字共用译文
    std::unique_ptr<balloonwjui::DuiHBox> recentRow(new balloonwjui::DuiHBox());
    recentRow->SetGap(kCountRowGap);
    std::unique_ptr<balloonwjui::DuiButton> recent =
        MakeHistoryRadio(kPickerIdHistoryRecent, TRC(L"邀请入群同步条数输入框之前的文字", L"最近"));
    controls.m_recent = recent.get();
    recentRow->AddChild(std::move(recent), balloonwjui::DuiLayout::Hint().Fixed(InviteRecentRadioWidth()));
    std::unique_ptr<balloonwjui::DuiEdit> count(new InviteCountEdit());
    count->SetCtrlId(kPickerIdHistoryCount);
    count->SetBgColor(kEditBgColor);
    count->SetMaxLength(kCountMaxDigits);
    CString defaultCount;
    defaultCount.Format(_T("%d"), kInviteHistoryDefaultCount);
    count->SetTextNoNotify(defaultCount);
    controls.m_count = count.get();
    recentRow->AddChild(std::move(count), balloonwjui::DuiLayout::Hint().Fixed(kCountEditW));
    recentRow->AddChild(MakeGreyLabel(TRC(L"邀请入群同步条数输入框之后的文字", L"条"), 0,
                                      DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                        balloonwjui::DuiLayout::Hint().Weight(1));
    group->AddChild(std::move(recentRow), balloonwjui::DuiLayout::Hint().Fixed(kCountRowH));

    //「不同步」：新成员只能看到入群之后的消息
    std::unique_ptr<balloonwjui::DuiButton> none =
        MakeHistoryRadio(kPickerIdHistoryNone, TR(L"不同步，只能看到入群之后的消息"));
    controls.m_none = none.get();
    group->AddChild(std::move(none), balloonwjui::DuiLayout::Hint().Fixed(kRadioRowH));

    std::unique_ptr<balloonwjui::DuiLabel> hint = MakeGreyLabel(
        InviteHistoryHintText(spec.m_groupAllowsHistory, spec.m_inviteConfirm), kPickerIdHistoryHint,
        DT_LEFT | DT_TOP | DT_WORDBREAK);
    controls.m_hint = hint.get();
    group->AddChild(std::move(hint), balloonwjui::DuiLayout::Hint().Fixed(kHintRowH));

    ApplyInviteHistoryEnableState(controls, spec.m_groupAllowsHistory);
    return group;
}

//底部栏：已选 N 人 | 取消 | 确认（文字按用途）。计数文案由对话框的 RebuildSelected 填，这里不写初值
std::unique_ptr<balloonwjui::DuiHBox> BuildBottomBar(const MemberPickerLayoutSpec& spec, MemberPickerLayoutRefs& refs)
{
    std::unique_ptr<balloonwjui::DuiHBox> bottom(new balloonwjui::DuiHBox());
    bottom->SetGap(kBottomGap);

    std::unique_ptr<balloonwjui::DuiLabel> countLabel = MakeGreyLabel(CString(), kPickerIdCount,
                                                                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    refs.m_countLabel = countLabel.get();
    bottom->AddChild(std::move(countLabel), balloonwjui::DuiLayout::Hint().Weight(1));

    std::unique_ptr<balloonwjui::DuiButton> cancel(new balloonwjui::DuiButton());
    cancel->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    cancel->SetVariant(balloonwjui::DuiButton::Variant::Default);
    cancel->SetCtrlId(kPickerIdCancel);
    cancel->SetText(TR(L"取消"));
    bottom->AddChild(std::move(cancel), balloonwjui::DuiLayout::Hint().Fixed(kCancelBtnW));

    std::unique_ptr<balloonwjui::DuiButton> confirm(new balloonwjui::DuiButton());
    confirm->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    confirm->SetVariant(balloonwjui::DuiButton::Variant::Primary);
    confirm->SetCtrlId(kPickerIdConfirm);
    confirm->SetText(spec.m_confirmText);
    refs.m_confirmButton = confirm.get();
    bottom->AddChild(std::move(confirm), balloonwjui::DuiLayout::Hint().Fixed(kConfirmBtnW));
    return bottom;
}

} // namespace

std::unique_ptr<balloonwjui::DuiVBox> BuildMemberPickerLayout(const MemberPickerLayoutSpec& spec,
                                                              MemberPickerLayoutRefs& refs)
{
    refs = MemberPickerLayoutRefs();

    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kRootPadding);
    root->SetGap(kRootGap);

    // ---- 上半区：左(搜索+成员列表) | 右(群名+已选成员+同步选项) ----
    std::unique_ptr<balloonwjui::DuiHBox> top(new balloonwjui::DuiHBox());
    top->SetGap(kColumnGap);

    // 左列
    {
        std::unique_ptr<balloonwjui::DuiVBox> left(new balloonwjui::DuiVBox());
        left->SetGap(kInColumnGap);

        std::unique_ptr<balloonwjui::DuiEdit> search(new balloonwjui::DuiEdit());
        search->SetCtrlId(kPickerIdSearch);
        search->SetPlaceholder(TR(L"搜索联系人"));
        search->SetBgColor(kEditBgColor);
        refs.m_searchInput = search.get();
        left->AddChild(std::move(search), balloonwjui::DuiLayout::Hint().Fixed(kEditRowH));

        std::unique_ptr<balloonwjui::DuiScrollView> scroll(new balloonwjui::DuiScrollView());
        scroll->SetCtrlId(kPickerIdMemberScroll);
        // 内容是一棵单列 DuiTreeView。它自己不滚、GetDesiredSize 也返 0，故关掉
        // 自动测高，改由对话框的 SyncTreeHeight 按树的内容总高显式设给滚动区。
        scroll->SetAutoContentHeight(false);
        std::unique_ptr<MemberPickerTreeView> tree(new MemberPickerTreeView());
        tree->SetRowHeight(kMemberRowH);
        refs.m_memberTree = tree.get();
        scroll->SetContent(std::unique_ptr<balloonwjui::DuiControl>(tree.release()));
        refs.m_memberScroll = scroll.get();
        left->AddChild(std::move(scroll), balloonwjui::DuiLayout::Hint().Weight(1));

        top->AddChild(std::move(left), balloonwjui::DuiLayout::Hint().Weight(1));
    }

    // 右列
    {
        std::unique_ptr<balloonwjui::DuiVBox> right(new balloonwjui::DuiVBox());
        right->SetGap(kInColumnGap);

        //群名输入框只有"新建群聊"用得上。其余用途是往已有的群里选人，
        //群名早就定了，这一行整个不构建（而不是藏起来），免得留一条空白带。
        if (spec.m_showNameInput)
        {
            std::unique_ptr<balloonwjui::DuiEdit> name(new balloonwjui::DuiEdit());
            name->SetCtrlId(kPickerIdName);
            name->SetPlaceholder(TR(L"群聊名称（选填）"));
            name->SetBgColor(kEditBgColor);
            refs.m_nameInput = name.get();
            right->AddChild(std::move(name), balloonwjui::DuiLayout::Hint().Fixed(kEditRowH));
        }

        //「群类型」选项组排在群名输入框下方，按内容高度占位；帖子群功能不可用时整组不构建
        if (spec.m_showGroupType)
            right->AddChild(BuildGroupTypeGroup(refs), balloonwjui::DuiLayout::Hint().Auto());

        right->AddChild(MakeGreyLabel(TR(L"已选成员"), 0, DT_LEFT | DT_VCENTER | DT_SINGLELINE),
                        balloonwjui::DuiLayout::Hint().Fixed(kSectionTitleH));

        std::unique_ptr<balloonwjui::DuiScrollView> chip(new balloonwjui::DuiScrollView());
        chip->SetCtrlId(kPickerIdChipScroll);
        chip->SetAutoContentHeight(true);
        chip->SetContent(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiVBox()));
        refs.m_chipScroll = chip.get();
        right->AddChild(std::move(chip), balloonwjui::DuiLayout::Hint().Weight(1));

        //「入群后可查看的聊天记录」选项组只有邀请入群用得上，排在已选成员列表下方、按内容高度占位
        if (spec.m_showInviteHistory)
            right->AddChild(BuildInviteHistoryGroup(spec, refs), balloonwjui::DuiLayout::Hint().Auto());

        top->AddChild(std::move(right), balloonwjui::DuiLayout::Hint().Weight(1));
    }

    root->AddChild(std::move(top), balloonwjui::DuiLayout::Hint().Weight(1));
    root->AddChild(BuildBottomBar(spec, refs), balloonwjui::DuiLayout::Hint().Fixed(kBottomBarH));
    return root;
}

void ApplyInviteHistoryEnableState(const InviteHistoryControls& controls, bool groupAllowsHistory)
{
    if (!controls.IsBuilt())
        return;
    //群不允许看历史：以群设置为准，整组不可操作；否则只有选中「最近 N 条」时条数输入框可编辑
    controls.m_all->SetEnabled(groupAllowsHistory);
    controls.m_recent->SetEnabled(groupAllowsHistory);
    controls.m_none->SetEnabled(groupAllowsHistory);
    controls.m_count->SetEnabled(groupAllowsHistory && controls.m_recent->IsChecked());
}

int InviteHistoryModeOf(const InviteHistoryControls& controls)
{
    if (!controls.IsBuilt())
        return kInviteHistoryModeAll;
    if (controls.m_recent->IsChecked())
        return kInviteHistoryModeRecent;
    if (controls.m_none->IsChecked())
        return kInviteHistoryModeNone;
    return kInviteHistoryModeAll;
}

void SelectInviteHistoryMode(const InviteHistoryControls& controls, int mode, bool groupAllowsHistory)
{
    if (!controls.IsBuilt())
        return;
    if (mode != kInviteHistoryModeRecent && mode != kInviteHistoryModeNone)
        mode = kInviteHistoryModeAll;
    //notify=false：程序设置选中状态不触发 VALUECHANGED，免得对话框的处理函数再进来一次
    controls.m_all->SetCheck(mode == kInviteHistoryModeAll, false);
    controls.m_recent->SetCheck(mode == kInviteHistoryModeRecent, false);
    controls.m_none->SetCheck(mode == kInviteHistoryModeNone, false);
    ApplyInviteHistoryEnableState(controls, groupAllowsHistory);
}

int InviteRecentRadioWidth()
{
    //按控件默认字体（96 dpi，即逻辑像素）量「最近」的宽度；与 DuiButton 绘制时取的是同一个字体
    return RadioWidthForText(TRC(L"邀请入群同步条数输入框之前的文字", L"最近"));
}

int SelectedGroupType(const balloonwjui::DuiButton* normal, const balloonwjui::DuiButton* thread)
{
    (void)normal;
    //只有「帖子群」被选中才建帖子群；没有构建选项组（功能不可用）时一律普通群
    if (thread != NULL && thread->IsChecked())
        return balloonthread::kGroupTypeThread;
    return balloonthread::kGroupTypeNormal;
}

void SetPickerConfirmBusy(balloonwjui::DuiButton* confirm, bool busy, const CString& idleText)
{
    if (confirm == NULL)
        return;
    confirm->SetText(busy ? CString(TR(L"正在创建…")) : idleText);
    confirm->SetEnabled(!busy);
    confirm->Invalidate();
}

int ParseInviteHistoryLimit(bool groupAllowsHistory, int mode, const CString& countText, int& outLimit)
{
    //群不允许看历史、或选了「全部」：请求里不带条数
    if (!groupAllowsHistory || (mode != kInviteHistoryModeRecent && mode != kInviteHistoryModeNone))
    {
        outLimit = kInviteHistoryAll;
        return kInviteHistoryOk;
    }
    //选了「不同步」：0，输入框里是什么都不看
    if (mode == kInviteHistoryModeNone)
    {
        outLimit = 0;
        return kInviteHistoryOk;
    }

    CString text = countText;
    text.Trim();
    if (text.IsEmpty())
        return kInviteHistoryEmpty;
    for (int i = 0; i < text.GetLength(); ++i)
    {
        if (text[i] < _T('0') || text[i] > _T('9'))
            return kInviteHistoryNotNumber;
    }
    //位数先挡一道，免得超长的数字串转换时溢出
    if (text.GetLength() > kCountMaxDigits)
        return kInviteHistoryTooLarge;
    const int value = _ttoi(text);
    if (value > kInviteHistoryMax)
        return kInviteHistoryTooLarge;
    if (value == 0)
        return kInviteHistoryZero;
    outLimit = value;
    return kInviteHistoryOk;
}

CString InviteHistoryHintText(bool groupAllowsHistory, bool inviteConfirm)
{
    CString text;
    if (!groupAllowsHistory)
        text = TR(L"本群已设置为新成员不可查看入群前的聊天记录");
    if (inviteConfirm)
    {
        if (!text.IsEmpty())
            text += _T("\n");
        text += TR(L"对方同意后才会加入群聊");
    }
    return text;
}

CString InviteHistoryErrorText(int error)
{
    switch (error)
    {
    //没有填条数
    case kInviteHistoryEmpty:
        return CString(TR(L"请填写要同步的消息条数"));
    //填的不是非负整数
    case kInviteHistoryNotNumber:
        return CString(TR(L"消息条数只能填数字"));
    //超过上限
    case kInviteHistoryTooLarge:
        return CString(TRF(L"消息条数不能超过 {0}", kInviteHistoryMax).c_str());
    //选了「最近 N 条」却填了 0
    case kInviteHistoryZero:
        return CString(TR(L"条数至少为 1，不想同步请选「不同步」"));
    //合法：没有提示
    default:
        return CString();
    }
}

CString InviteResultTipText(int invited, int pending)
{
    CString text;
    if (invited > 0)
        text = TRF(L"已向 {0} 人发出入群邀请，对方同意后加入群聊", invited).c_str();
    if (pending > 0)
    {
        if (!text.IsEmpty())
            text += _T("\n");
        text += TR(L"已提交邀请，等待群主或管理员审批");
    }
    return text;
}

} // namespace balloonmain
