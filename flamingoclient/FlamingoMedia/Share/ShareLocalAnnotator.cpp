/**
 *  本机标注，见 ShareLocalAnnotator.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "ShareLocalAnnotator.h"

#include <memory>
#include <string>
#include "ShareAnnotationRender.h"
#include "ShareSources.h"   // ApplyBarCaptureState：「允许截图」（2026-10-08）
#include "ShareAnnotModePolicy.h"   // AnnotSelectCursorName：选择工具的光标（2026-10-07）
#include "../../Source/Common/Log/IULog.h"

namespace screenshare {

namespace {

//激光笔淡出的推进定时器：每 50 毫秒一次（与屏幕共享控制器一致）
const UINT_PTR kTickTimerId = 701;
const UINT kTickMs = 50;
//本机会话的编号种子（没有别人，从 1 起即可）
const uint32_t kLocalIdSeed = 1;
//GetAsyncKeyState 返回值里「此刻按着」的那一位
const SHORT kKeyDownBit = static_cast<SHORT>(0x8000);

//UTF-8 转宽字符
std::wstring toWide(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), NULL, 0);
    if (n <= 0)
        return std::wstring();
    std::wstring w(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

//宽字符转 UTF-8
std::string toUtf8(const std::wstring& w)
{
    if (w.empty())
        return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), NULL, 0, NULL, NULL);
    if (n <= 0)
        return std::string();
    std::string s(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, NULL, NULL);
    return s;
}

} // namespace

CShareLocalAnnotator::CShareLocalAnnotator()
    : m_listener(NULL)
    , m_cursor(NULL)
    , m_cursorTool(0)
    , m_cursorColor(0)
    , m_barVisible(false)
    , m_pointerMode(false)
    , m_hoverTol(0)
    , m_hoverValid(false)
{
    memset(&m_anchor, 0, sizeof(m_anchor));
}

CShareLocalAnnotator::~CShareLocalAnnotator()
{
    Stop();
    if (m_cursor != NULL)
        ::DestroyIcon(m_cursor);
}

bool CShareLocalAnnotator::Start(IShareLocalAnnotatorListener* listener, double widthScale)
{
    if (!IsWindow() && Create(HWND_MESSAGE) == NULL)
    {
        LOG_ERROR("localannot: create message window failed.");
        return false;
    }
    m_listener = listener;
    m_session.Start(NULL, kAnnotHostSeat, kLocalIdSeed);
    m_session.SetWidthScale(widthScale);
    ApplyAnnotBarState(m_state, m_session);
    m_editRef = AnnotationRef();
    SetTimer(kTickTimerId, kTickMs, NULL);
    LOG_INFO("localannot: started, widthScale=%.2f.", widthScale);
    return true;
}

void CShareLocalAnnotator::Stop()
{
    m_textDialog.Close();
    m_bar.Hide();
    m_barVisible = false;
    if (m_session.IsRunning())
        m_session.Stop();
    m_listener = NULL;
    if (IsWindow())
    {
        KillTimer(kTickTimerId);
        DestroyWindow();
    }
}

bool CShareLocalAnnotator::IsRunning() const
{
    return m_session.IsRunning();
}

void CShareLocalAnnotator::ShowBar(const RECT& anchor)
{
    if (!IsWindow())
        return;
    m_anchor = anchor;
    //重新显示工具条时从画图开始（2026-10-07）
    m_pointerMode = false;
    m_bar.SetPointerMode(false);
    m_bar.SetLocalMode(true);
    m_bar.SetState(m_state);
    m_bar.SetDrawEnabled(true);
    m_bar.Show(anchor, m_hWnd, true);
    m_barVisible = true;
}

void CShareLocalAnnotator::BringBarToTop()
{
    if (m_barVisible && m_bar.IsWindow())
        ::SetWindowPos(m_bar.m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

bool CShareLocalAnnotator::IsPointerMode() const
{
    return m_pointerMode;
}

void CShareLocalAnnotator::setPointerMode(bool on)
{
    m_bar.SetPointerMode(on);
    if (on == m_pointerMode)
        return;
    m_pointerMode = on;
    if (on)
        m_session.CancelPointer();
    LOG_INFO("localannot: pointer mode %s.", on ? "on" : "off");
    if (m_listener != NULL)
        m_listener->OnLocalAnnotatorPointerMode(on);
    changed();
}

void CShareLocalAnnotator::ReapplyBarCapture()
{
    if (m_bar.IsWindow())
        ApplyBarCaptureState(m_bar.m_hWnd);
}

void CShareLocalAnnotator::HideBar()
{
    m_session.CancelPointer();
    m_session.ClearSelection();
    m_textDialog.Close();
    m_bar.Hide();
    m_barVisible = false;
    changed();
}

void CShareLocalAnnotator::MoveBar(const RECT& anchor)
{
    if (!m_barVisible)
        return;
    if (anchor.left == m_anchor.left && anchor.top == m_anchor.top && anchor.right == m_anchor.right &&
        anchor.bottom == m_anchor.bottom)
        return;
    m_anchor = anchor;
    m_bar.MoveTo(anchor);
}

bool CShareLocalAnnotator::IsBarVisible() const
{
    return m_barVisible;
}

ShareAnnotationSession& CShareLocalAnnotator::Session()
{
    return m_session;
}

const ShareAnnotationSession& CShareLocalAnnotator::Session() const
{
    return m_session;
}

void CShareLocalAnnotator::changed()
{
    if (m_listener != NULL)
        m_listener->OnLocalAnnotationsChanged();
}

void CShareLocalAnnotator::OnAnnotPointer(int phase, const AnnotationPoint& pt, int tolerance, double aspect,
                                          POINT screenPt)
{
    if (!m_session.IsRunning())
        return;
    const int64_t now = static_cast<int64_t>(::GetTickCount64());
    m_session.SetContentAspect(aspect);
    //键盘焦点可能在被录的程序上，取异步的按键状态（2026-10-07）
    m_session.SetShiftDown((::GetAsyncKeyState(VK_SHIFT) & kKeyDownBit) != 0);
    if (phase == annot_pointer_leave)
    {
        m_hoverValid = false;
        return;
    }
    //记下光标位置：选择工具按它决定光标是四向移动还是十字（2026-10-07）
    m_hoverPt = pt;
    m_hoverTol = tolerance;
    m_hoverValid = true;
    //「鼠标」状态（2026-10-07）：不画；白板画布仍会交来鼠标动作，这里一律不处理
    if (m_pointerMode)
        return;
    //文字工具：按下处弹出输入框，确定后才添加
    if (m_session.Tool() == annot_tool_text)
    {
        if (phase == annot_pointer_down)
        {
            m_textPoint = pt;
            m_editRef = AnnotationRef();
            m_textDialog.Open(screenPt, m_hWnd, true);
        }
        return;
    }
    AnnotationStroke pending;
    switch (phase)
    {
    //悬停：只有折线、曲线在添加顶点时要重画预览
    case annot_pointer_hover:
        m_session.PointerHover(pt, now);
        if (!m_session.PendingShape(pending))
            return;
        break;
    //按下
    case annot_pointer_down:
        m_session.PointerDown(pt, tolerance, now);
        break;
    //拖动
    case annot_pointer_move:
        m_session.PointerMove(pt, tolerance, now);
        break;
    //松开
    case annot_pointer_up:
        m_session.PointerUp(pt, now);
        break;
    //双击或右键：选择工具下修改点中对象的文字，其余完成折线、曲线
    case annot_pointer_finish:
        if (m_session.Tool() == kAnnotSelect)
        {
            AnnotationRef ref;
            std::string current;
            if (m_session.EditableAt(pt, tolerance, ref, current))
            {
                const AnnotationStroke* s = m_session.Model().Find(ref);
                if (s != NULL)
                {
                    m_editRef = ref;
                    m_textDialog.Open(screenPt, m_hWnd, true, MakeAnnotTextEditSpec(s->m_tool, toWide(current)));
                }
            }
            return;
        }
        m_session.FinishPending(now);
        break;
    //未知动作：忽略
    default:
        return;
    }
    changed();
}

bool CShareLocalAnnotator::OnAnnotKey(UINT vk)
{
    if (!m_session.IsRunning() || m_session.Selection().empty())
        return false;
    //Delete：删除选中的对象
    if (vk == VK_DELETE)
    {
        m_session.DeleteSelection();
        changed();
        return true;
    }
    //Esc：取消选择
    if (vk == VK_ESCAPE)
    {
        m_session.ClearSelection();
        changed();
        return true;
    }
    return false;
}

HCURSOR CShareLocalAnnotator::AnnotCursor()
{
    const int tool = m_session.Tool();
    const uint32_t color = m_session.Color();
    //「鼠标」状态（2026-10-07）：系统箭头（共享光标，不缓存、不释放）
    if (m_pointerMode)
        return ::LoadCursor(NULL, IDC_ARROW);
    //选择工具：2026-10-07 起悬停在对象上为四向移动光标，空白处为十字（可以框选）
    if (tool == kAnnotSelect)
        return ::LoadCursor(NULL, AnnotSelectCursorName(m_hoverValid && m_session.ObjectAt(m_hoverPt, m_hoverTol)));
    if (m_cursor != NULL && tool == m_cursorTool && color == m_cursorColor)
        return m_cursor;
    if (m_cursor != NULL)
        ::DestroyIcon(m_cursor);
    m_cursor = CreateAnnotationCursor(tool, color);
    m_cursorTool = tool;
    m_cursorColor = color;
    return m_cursor;
}

LRESULT CShareLocalAnnotator::OnCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    if (!m_session.IsRunning())
        return 0;
    AnnotMenuChoice choice;
    choice.m_command = static_cast<int>(wParam);
    choice.m_value = static_cast<int>(lParam);
    //选择工具下有选中对象：样式类命令改到选中的对象上，工具保持为选择工具
    int attribute = 0;
    int value = 0;
    if (m_session.Tool() == kAnnotSelect && !m_session.Selection().empty() &&
        ApplyAnnotChoiceInSelectMode(choice, m_state, attribute, value))
    {
        m_session.RestyleSelection(attribute, value);
        m_bar.SetState(m_state);
        changed();
        return 0;
    }
    //选了工具（或会切到某个工具的样式）：从「鼠标」状态回到画图（2026-10-07）
    if (m_pointerMode && AnnotChoiceLeavesPointer(choice))
        setPointerMode(false);
    //工具、颜色与各项样式：记进状态后整体交给会话
    if (ApplyAnnotChoice(choice, m_state))
    {
        ApplyAnnotBarState(m_state, m_session);
        m_bar.SetState(m_state);
        changed();
        return 0;
    }
    switch (choice.m_command)
    {
    //数字标号从头编号
    case annot_bar_badge_reset:
        m_session.ResetBadgeNumber();
        break;
    //撤销本人最近的一笔
    case annot_bar_undo:
        m_session.UndoOwn();
        break;
    //清空全部笔迹
    case annot_bar_clear:
        m_session.ClearAll();
        break;
    //删除选中的对象
    case annot_bar_delete:
        m_session.DeleteSelection();
        break;
    //退出标注：交给调用方（隐藏工具条、放开鼠标）
    case annot_bar_exit:
        if (m_listener != NULL)
            m_listener->OnLocalAnnotatorExit();
        return 0;
    //「鼠标」（2026-10-07）：1 进入「鼠标」状态，0 回到画图
    case annot_bar_pointer:
        setPointerMode(choice.m_value != 0);
        return 0;
    //权限与未知命令：本机模式下没有
    default:
        return 0;
    }
    changed();
    return 0;
}

LRESULT CShareLocalAnnotator::OnTextMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)bHandled;
    std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
    const AnnotationRef editRef = m_editRef;
    m_editRef = AnnotationRef();
    if (!text || !m_session.IsRunning())
        return 0;
    //双击修改已有对象的文字
    if (editRef.IsSet())
    {
        if (m_session.SetObjectText(editRef, toUtf8(*text)))
            changed();
        return 0;
    }
    if (!text->empty() && m_session.AddText(m_textPoint, toUtf8(*text)))
        changed();
    return 0;
}

LRESULT CShareLocalAnnotator::OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    if (wParam != kTickTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    if (!m_session.IsRunning())
        return 0;
    //激光笔淡出：有画完的激光笔时每一拍都重画（与屏幕共享控制器一致），淡完后由 Tick 删掉
    bool fading = false;
    const std::vector<AnnotationStroke>& strokes = m_session.Model().Strokes();
    for (size_t i = 0; i < strokes.size() && !fading; ++i)
        fading = (strokes[i].m_tool == annot_tool_laser && strokes[i].m_finished);
    //拖动形状中按下或松开 Shift、鼠标不动（2026-10-07）：预览也要跟着变成正形或恢复
    const bool shiftChanged = m_session.SetShiftDown((::GetAsyncKeyState(VK_SHIFT) & kKeyDownBit) != 0);
    if (m_session.Tick(static_cast<int64_t>(::GetTickCount64())) || fading || shiftChanged)
        changed();
    return 0;
}

} // namespace screenshare
