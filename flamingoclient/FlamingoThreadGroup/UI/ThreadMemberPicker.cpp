/**
 *  发帖窗口「@ 成员」的选人浮层，见 ThreadMemberPicker.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ThreadMemberPicker.h"

#include <memory>

#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TR_NOOP：界面文字的多语言查找
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiListBox.h"

using balloonwjui::DuiControl;
using balloonwjui::DuiEdit;
using balloonwjui::DuiLabel;
using balloonwjui::DuiLayout;
using balloonwjui::DuiListBox;
using balloonwjui::DuiVBox;

namespace threadgroup {

namespace {

//---- 尺寸，单位：像素 ----
const int kPopupWidth = 240;        //浮层宽
const int kPopupHeight = 300;       //浮层高
const int kPad = 8;                 //浮层内边距
const int kGap = 6;                 //搜索框与列表之间的距离
const int kSearchHeight = 30;       //搜索框高
const int kItemHeight = 30;         //列表每行高

//---- 颜色 ----
const COLORREF kBg = RGB(0xFF, 0xFF, 0xFF);         //浮层底色
const COLORREF kHintColor = RGB(0x9A, 0x9A, 0x9A);  //提示文字

//---- 控件 id（浮层内部用，通知发回浮层自己） ----
const UINT kIdMemberSearch = 7901;  //搜索框
const UINT kIdMemberList = 7902;    //成员列表

//---- 界面文字（文件作用域的字符串只做标记，使用处再调 TR） ----
const wchar_t* const kSearchPlaceholder = TR_NOOP(L"搜索成员");
const wchar_t* const kLoadingText = TR_NOOP(L"正在加载成员…");
const wchar_t* const kNoMemberText = TR_NOOP(L"群里还没有其他成员");
const wchar_t* const kNotFoundText = TR_NOOP(L"没有找到");

//转成小写，用于不区分大小写的查找
std::wstring ToLower(const std::wstring& text)
{
    std::wstring lower = text;
    if (!lower.empty())
        ::CharLowerBuffW(&lower[0], static_cast<DWORD>(lower.size()));
    return lower;
}

} // namespace

CThreadMemberPicker::CThreadMemberPicker()
    : m_pickFn(NULL)
    , m_pickUser(NULL)
    , m_built(false)
    , m_loaded(false)
    , m_search(NULL)
    , m_list(NULL)
    , m_hint(NULL)
{
}

void CThreadMemberPicker::SetPickCallback(PickFn fn, void* user)
{
    m_pickFn = fn;
    m_pickUser = user;
}

void CThreadMemberPicker::SetMembers(const std::vector<MemberPickEntry>& members, bool loaded)
{
    m_members = members;
    m_loaded = loaded;
    if (m_built)
        ApplyFilter();
}

void CThreadMemberPicker::OpenAt(const RECT& anchorScreen)
{
    BuildOnce();
    if (m_search != NULL)
        m_search->SetTextNoNotify(L"");
    ApplyFilter();
    //所有者传 NULL：控件通知发回本浮层（UI 约定第 11 条）
    Show(anchorScreen, NULL);
    if (m_search != NULL)
        m_search->SetFocus();
}

void CThreadMemberPicker::BuildOnce()
{
    if (m_built)
        return;
    m_built = true;
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kBg);
    root->SetPadding(kPad, kPad, kPad, kPad);
    root->SetGap(kGap);

    std::unique_ptr<DuiEdit> search(new DuiEdit());
    search->SetCtrlId(kIdMemberSearch);
    search->SetPlaceholder(TR(kSearchPlaceholder));
    m_search = search.get();
    root->AddChild(std::unique_ptr<DuiControl>(search.release()), Hint().Fixed(kSearchHeight));

    std::unique_ptr<DuiListBox> list(new DuiListBox());
    list->SetCtrlId(kIdMemberList);
    list->SetItemHeight(kItemHeight);
    m_list = list.get();
    root->AddChild(std::unique_ptr<DuiControl>(list.release()), Hint().Weight(1));

    std::unique_ptr<DuiLabel> hint(new DuiLabel());
    hint->SetTextColor(kHintColor);
    hint->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    m_hint = hint.get();
    root->AddChild(std::unique_ptr<DuiControl>(hint.release()), Hint().Weight(1));

    SetContent(std::unique_ptr<DuiControl>(root.release()));
    SetSize(kPopupWidth, kPopupHeight);
    SetEdge(balloonwjui::DuiPopupHost::EdgeAbove);
}

void CThreadMemberPicker::ApplyFilter()
{
    if (m_list == NULL || m_hint == NULL)
        return;
    const std::wstring keyword = (m_search != NULL) ? ToLower(std::wstring(m_search->GetText().GetString())) : std::wstring();
    m_list->DeleteAllItems();
    for (size_t i = 0; i < m_members.size(); ++i)
    {
        if (!keyword.empty() && ToLower(m_members[i].m_name).find(keyword) == std::wstring::npos)
            continue;
        m_list->AddItem(m_members[i].m_name.c_str(), static_cast<LPARAM>(i));
    }
    //列表为空时换成提示：还在加载、群里没有别人、搜不到
    const bool empty = (m_list->GetItemCount() == 0);
    const wchar_t* hintText = !m_loaded ? kLoadingText : (keyword.empty() ? kNoMemberText : kNotFoundText);
    m_hint->SetText(TR(hintText));
    if (m_list->IsVisible() == empty || m_hint->IsVisible() != empty)
    {
        //列表与提示显隐互换：SetVisible 不会重排，对父容器按当前矩形重排（UI 约定第 5 条）
        m_list->SetVisible(!empty);
        m_hint->SetVisible(empty);
        DuiControl* parent = m_list->GetParent();
        if (parent != NULL)
            parent->ForceLayout(parent->GetRect());
    }
    m_list->Invalidate();
}

void CThreadMemberPicker::PickRow(int row)
{
    if (m_list == NULL || row < 0 || row >= m_list->GetItemCount())
        return;
    const size_t index = static_cast<size_t>(m_list->GetItemParam(row));
    if (index >= m_members.size())
        return;
    LOG_INFO("thread member picked, userid=%lld.", static_cast<long long>(m_members[index].m_userId));
    if (m_pickFn != NULL)
        m_pickFn(m_pickUser, m_members[index].m_userId);
}

LRESULT CThreadMemberPicker::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    //搜索框的文字变了：重新过滤（通知码与控件 id 一起判，UI 约定第 4 条）
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED && pNotify->ctrlId == kIdMemberSearch)
    {
        ApplyFilter();
        return 0;
    }
    //列表选中了一行：回调发帖窗口（它投递消息，等这条通知处理完再插入并关掉浮层）
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED && pNotify->ctrlId == kIdMemberList && pNotify->extra >= 0)
    {
        PickRow(static_cast<int>(pNotify->extra));
        return 0;
    }
    //搜索框里按了回车：选第一行
    if (pNotify->code == static_cast<UINT>(DuiEdit::DUIN_EDIT_ENTER) && pNotify->ctrlId == kIdMemberSearch)
    {
        PickRow(0);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

} // namespace threadgroup
