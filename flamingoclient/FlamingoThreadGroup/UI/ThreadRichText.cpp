/**
 *  帖子群窗口里只读富文本的建法与填充，见 ThreadRichText.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ThreadRichText.h"

#include "../Core/ThreadText.h"   // ParseWire：把线上正文拆成文字段与表情段
#include "../../Source/Common/Log/IULog.h"   // LOG_INFO：点 @XXX 的日志
#include "../../../third-party/balloonui/balloonui/DuiHost.h"   // GetHost()->m_hWnd：点 @ 时把坐标换算成屏幕坐标

using balloonwjui::DuiRichEdit;

namespace threadgroup {

namespace {

//只读展示时文字与控件边界之间不留内边距（所在容器已经留了）
const int kNoMargin = 0;

//「@名字」的配色，取值与普通群气泡相同（Source/UI/BalloonChatWnd/ChatBubble.cpp 的 kAtOtherBg 等；帖子详情的底色
//与对方气泡一样是浅色，用「对方气泡」那一组）
const COLORREF kAtOtherBg = RGB(0xE4, 0xEE, 0xFB);   //@ 别人的底色
const COLORREF kAtOtherFg = RGB(0x2F, 0x6F, 0xD0);   //@ 别人的文字
const COLORREF kAtMeBg = RGB(0xFF, 0xDD, 0xCB);      //@ 到本人的底色
const COLORREF kAtMeFg = RGB(0xD9, 0x53, 0x1E);      //@ 到本人的文字

//表情编号对应的图片路径；没有时为空串
std::wstring FacePathOf(const std::vector<ThreadFaceEntry>& faces, int faceId)
{
    for (size_t i = 0; i < faces.size(); ++i)
    {
        if (faces[i].m_faceId == faceId)
            return faces[i].m_path;
    }
    return std::wstring();
}

} // namespace

CThreadAtRichEdit::CThreadAtRichEdit()
    : m_atClickFn(NULL)
    , m_atClickUser(NULL)
    , m_pressedAtUser(0)
{
}

void CThreadAtRichEdit::SetAtClickHandler(ThreadAtClickFn fn, void* user)
{
    m_atClickFn = fn;
    m_atClickUser = user;
}

void CThreadAtRichEdit::AddAtRanges(int base, const std::vector<ThreadAtDisplay>& ranges)
{
    for (size_t i = 0; i < ranges.size(); ++i)
    {
        ThreadAtDisplay r = ranges[i];
        r.m_start += base;
        r.m_end += base;
        m_atRanges.push_back(r);
    }
}

const std::vector<ThreadAtDisplay>& CThreadAtRichEdit::AtRanges() const
{
    return m_atRanges;
}

int64_t CThreadAtRichEdit::HitTestAt(POINT pt)
{
    if (m_atRanges.empty())
        return 0;
    const int index = CharAtPoint(pt);
    if (index < 0)
        return 0;
    return AtUserAtChar(m_atRanges, index);
}

int CThreadAtRichEdit::CharAtPoint(POINT pt)
{
    POINTL ptl;
    ptl.x = pt.x;
    ptl.y = pt.y;
    //富文本控件的 EM_CHARFROMPOS 要传坐标结构体的指针，返回离点最近的字边界（插入点）的位置
    int index = static_cast<int>(SendMessageToEngine(EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&ptl)));
    if (index < 0)
        return -1;
    //点在这个字的左边或上方：点击落在一个字的右半边时最近的边界是下一个字的左边，点实际在前一个字上
    POINTL pos;
    pos.x = 0;
    pos.y = 0;
    SendMessageToEngine(EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&pos), index);
    if (index > 0 && (pt.x < pos.x || pt.y < pos.y))
        --index;
    return PointOnChar(pt, index) ? index : -1;
}

bool CThreadAtRichEdit::PointOnChar(POINT pt, int index)
{
    //富文本控件的 EM_POSFROMCHAR：wParam 为坐标结构体的指针（由它填写字的左上角），lParam 为字的位置
    POINTL left;
    left.x = 0;
    left.y = 0;
    SendMessageToEngine(EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&left), index);
    if (pt.x < left.x || pt.y < left.y)
        return false;
    POINTL next;
    next.x = 0;
    next.y = 0;
    SendMessageToEngine(EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&next), index + 1);
    //下一个字在同一行：点要在这个字与下一个字之间
    if (next.y == left.y)
        return pt.x < next.x;
    //这个字是一行的最后一个字：右边界按一个行高算（中文的字宽与行高相近），点也不能低于这一行
    const LONG lineHeight = next.y - left.y;
    return lineHeight > 0 && pt.x < left.x + lineHeight && pt.y < next.y;
}

bool CThreadAtRichEdit::HasSelection()
{
    CHARRANGE range;
    range.cpMin = 0;
    range.cpMax = 0;
    SendMessageToEngine(EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
    return range.cpMax != range.cpMin;
}

void CThreadAtRichEdit::LogAtClick(const char* what, POINT pt)
{
    if (m_atClickFn == NULL || m_atRanges.empty())
        return;
    const int index = CharAtPoint(pt);
    const RECT rc = GetRect();
    LOG_INFO("thread at click %s, pt=(%d,%d), rect=(%d,%d,%d,%d), char=%d, user=%lld, ranges=%u.", what,
             static_cast<int>(pt.x), static_cast<int>(pt.y), static_cast<int>(rc.left), static_cast<int>(rc.top),
             static_cast<int>(rc.right), static_cast<int>(rc.bottom), index,
             static_cast<long long>((index >= 0) ? AtUserAtChar(m_atRanges, index) : 0),
             static_cast<unsigned>(m_atRanges.size()));
}

void CThreadAtRichEdit::FireAtClick(int64_t userId, POINT pt)
{
    balloonwjui::DuiHost* host = GetHost();
    if (m_atClickFn == NULL || host == NULL || !::IsWindow(host->m_hWnd))
        return;
    POINT screen = pt;
    ::ClientToScreen(host->m_hWnd, &screen);
    m_atClickFn(m_atClickUser, userId, screen);
}

bool CThreadAtRichEdit::OnLButtonDown(POINT pt, UINT mkFlags)
{
    LogAtClick("ldown", pt);
    m_pressedAtUser = (m_atClickFn != NULL) ? HitTestAt(pt) : 0;
    return DuiRichEdit::OnLButtonDown(pt, mkFlags);
}

bool CThreadAtRichEdit::OnLButtonUp(POINT pt, UINT mkFlags)
{
    const bool handled = DuiRichEdit::OnLButtonUp(pt, mkFlags);
    const int64_t pressed = m_pressedAtUser;
    m_pressedAtUser = 0;
    //按下与松开都在同一个人的 @ 上、且没有拖选出文字，才算点了它（拖选复制照常）
    if (pressed > 0 && HitTestAt(pt) == pressed && !HasSelection())
        FireAtClick(pressed, pt);
    return handled;
}

bool CThreadAtRichEdit::OnRButtonDown(POINT pt, UINT mkFlags)
{
    LogAtClick("rdown", pt);
    const int64_t userId = (m_atClickFn != NULL) ? HitTestAt(pt) : 0;
    if (userId > 0)
    {
        FireAtClick(userId, pt);
        return true;
    }
    return DuiRichEdit::OnRButtonDown(pt, mkFlags);
}

bool CThreadAtRichEdit::OnSetCursor(POINT pt)
{
    if (m_atClickFn != NULL && HitTestAt(pt) > 0)
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    return DuiRichEdit::OnSetCursor(pt);
}

std::unique_ptr<CThreadAtRichEdit> MakeReadOnlyRichText(HFONT font, COLORREF color, COLORREF bg)
{
    std::unique_ptr<CThreadAtRichEdit> edit(new CThreadAtRichEdit());
    edit->SetReadOnly(true);
    edit->SetMultiLine(true);
    edit->SetWordWrap(true);
    edit->SetShowBorder(false);
    //不显示光标但仍可拖选复制（不能改用 SetFocusable(false)，那样文字选不中）
    edit->SetShowCaret(false);
    edit->SetBackgroundColor(bg);
    edit->SetTextColor(color);
    edit->SetMargins(kNoMargin, kNoMargin, kNoMargin, kNoMargin);
    edit->SetVScrollPolicy(DuiRichEdit::kScrollBarNever);
    edit->SetHScrollPolicy(DuiRichEdit::kScrollBarNever);
    edit->SetDragDropEnabled(false);
    //高度随内容：布局用 Hint().Auto() 时按内容报告的高度占位
    edit->SetAutoGrow(true);
    edit->SetAutoGrowRange(0, 0);
    if (font != NULL)
        edit->SetDefaultFontFromHFONT(font);
    return edit;
}

void FillWireText(DuiRichEdit* edit, const std::wstring& wire, const std::vector<ThreadFaceEntry>& faces,
                  const std::wstring& unknownFace, int facePx)
{
    if (edit == NULL)
        return;
    edit->SetText(L"");
    AppendWireText(edit, wire, faces, unknownFace, facePx);
}

void AppendWireText(DuiRichEdit* edit, const std::wstring& wire, const std::vector<ThreadFaceEntry>& faces,
                    const std::wstring& unknownFace, int facePx)
{
    if (edit == NULL)
        return;
    const std::vector<TextSegment> segments = ParseWire(wire);
    for (size_t i = 0; i < segments.size(); ++i)
    {
        const TextSegment& seg = segments[i];
        if (!seg.m_face)
        {
            AppendPlainText(edit, seg.m_text);
            continue;
        }
        const std::wstring path = FacePathOf(faces, seg.m_faceId);
        if (path.empty())
        {
            AppendPlainText(edit, unknownFace);
            continue;
        }
        //插在文档末尾；标记记下表情编号（只读展示用不到，复制时也只会复制出占位字符）
        const int end = edit->GetTextLength();
        edit->SetSel(end, end);
        if (!edit->InsertTaggedImage(path.c_str(), static_cast<DWORD_PTR>(seg.m_faceId), facePx))
            AppendPlainText(edit, unknownFace);
    }
}

void AppendPlainText(DuiRichEdit* edit, const std::wstring& text)
{
    if (edit == NULL || text.empty())
        return;
    edit->AppendText(text.c_str());
}

size_t HighlightAtSpans(DuiRichEdit* edit, int base, const std::wstring& wire, const std::vector<ThreadAtSpan>& at,
                        int64_t myUserId, const std::vector<ThreadFaceEntry>& faces, const std::wstring& unknownFace)
{
    if (edit == NULL || at.empty() || wire.empty())
        return 0;
    //表情库里有的表情插成图片（占 1 个字符），没有的显示成替代文字，与 AppendWireText 的规则一致
    std::set<int> knownFaces;
    for (size_t i = 0; i < faces.size(); ++i)
    {
        if (!faces[i].m_path.empty())
            knownFaces.insert(faces[i].m_faceId);
    }
    const std::vector<ThreadAtDisplay> ranges =
        AtSpansToDisplay(wire, at, myUserId, knownFaces, static_cast<int>(unknownFace.size()));
    for (size_t i = 0; i < ranges.size(); ++i)
    {
        CHARFORMAT2W cf;
        ::ZeroMemory(&cf, sizeof(cf));
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_BACKCOLOR | CFM_COLOR;
        cf.crBackColor = ranges[i].m_isMe ? kAtMeBg : kAtOtherBg;
        cf.crTextColor = ranges[i].m_isMe ? kAtMeFg : kAtOtherFg;
        edit->SetSel(base + ranges[i].m_start, base + ranges[i].m_end);
        edit->SendMessageToEngine(EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    }
    //帖子群窗口的只读富文本：同时记下各段的位置与被 @ 的人，点 @XXX 时据此弹成员菜单
    CThreadAtRichEdit* atEdit = dynamic_cast<CThreadAtRichEdit*>(edit);
    if (atEdit != NULL)
        atEdit->AddAtRanges(base, ranges);
    //不留选区：只读框不显示光标，留着选区会画出一块反色
    const int end = edit->GetTextLength();
    edit->SetSel(end, end);
    return ranges.size();
}

} // namespace threadgroup
