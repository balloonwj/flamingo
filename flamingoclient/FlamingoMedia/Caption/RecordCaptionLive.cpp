/**
 *  录屏时屏幕上的实时字幕，见 RecordCaptionLive.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "RecordCaptionLive.h"

#include <string.h>

#include "MediaCaptionBridge.h"
#include "../../Source/Common/I18n/I18n.h"
#include "../../Source/Common/Log/IULog.h"

namespace flamingomedia {

namespace {

//单调递增的毫秒时钟
int64_t NowMs()
{
    return static_cast<int64_t>(::GetTickCount64());
}

} // namespace

CRecordCaptionLive::CRecordCaptionLive()
    : m_active(false)
    , m_enabled(true)
    , m_micOn(false)
    , m_systemOn(false)
    , m_areaVisible(false)
    , m_posted(false)
{
    memset(&m_area, 0, sizeof(m_area));
}

CRecordCaptionLive::~CRecordCaptionLive()
{
    Shutdown();
}

bool CRecordCaptionLive::Init()
{
    if (IsWindow())
        return true;
    if (Create(HWND_MESSAGE) == NULL)
    {
        LOG_ERROR("record caption live: create message window failed.");
        return false;
    }
    return true;
}

void CRecordCaptionLive::Shutdown()
{
    Stop();
    m_overlay.Destroy();
    if (IsWindow())
        DestroyWindow();
}

void CRecordCaptionLive::Start(const std::wstring& selfName, bool micOn, bool systemOn)
{
    dropPending();
    m_board.Clear();
    m_sentences.Reset();
    m_selfName = selfName;
    m_micOn = micOn;
    m_systemOn = systemOn;
    m_enabled = true;
    m_active = true;
    m_areaVisible = false;
    m_overlay.Hide();
    LOG_INFO("record caption live: started, mic=%d, system=%d.", micOn ? 1 : 0, systemOn ? 1 : 0);
}

void CRecordCaptionLive::Stop()
{
    if (!m_active)
        return;
    m_active = false;
    dropPending();
    m_board.Clear();
    m_overlay.Hide();
    LOG_INFO("record caption live: stopped.");
}

bool CRecordCaptionLive::IsActive() const
{
    return m_active;
}

void CRecordCaptionLive::SetSources(bool micOn, bool systemOn)
{
    m_micOn = micOn;
    m_systemOn = systemOn;
    //「没有声音可识别」的提示随之出现或消失
    refresh();
}

void CRecordCaptionLive::SetEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (!on)
    {
        //关闭：清掉正在显示的字幕，关闭前排进队列、还没显示的结果也不再显示
        dropPending();
        m_board.Clear();
        m_overlay.Hide();
    }
    LOG_INFO("record caption live: %s.", on ? "enabled" : "disabled");
}

bool CRecordCaptionLive::IsEnabled() const
{
    return m_enabled;
}

void CRecordCaptionLive::Tick(const RECT& area, bool visible)
{
    m_area = area;
    m_areaVisible = visible;
    if (!m_active)
        return;
    m_board.Expire(NowMs());
    //每次都重画：录制区域可能随被录的窗口移动
    refresh();
}

void CRecordCaptionLive::OnRecordingPartial(int source, const wchar_t* text)
{
    enqueue(source, false, text);
}

void CRecordCaptionLive::OnRecordingFinal(int source, const wchar_t* text)
{
    enqueue(source, true, text);
}

void CRecordCaptionLive::enqueue(int source, bool final, const wchar_t* text)
{
    if (text == NULL)
        return;
    Pending p;
    p.m_source = source;
    p.m_final = final;
    p.m_text = text;
    bool post = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.push_back(p);
        if (!m_posted)
        {
            m_posted = true;
            post = true;
        }
    }
    if (post && IsWindow())
        PostMessage(kMsgRecordCaptionPending);
}

void CRecordCaptionLive::dropPending()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pending.clear();
}

void CRecordCaptionLive::refresh()
{
    if (!m_active || !m_enabled || !m_areaVisible)
    {
        m_overlay.Hide();
        return;
    }
    std::vector<screenshare::ShareCaptionLine> lines = m_board.Visible();
    //没有字幕、麦克风与电脑声音也都没在录：显示一行提示，告诉用户打开其中一路之后才会有字幕（用户 2026-10-06 定）
    if (lines.empty())
    {
        if (RecordCaptionHasSource(m_micOn, m_systemOn))
        {
            m_overlay.Hide();
            return;
        }
        screenshare::ShareCaptionLine line;
        line.m_text = TR(L"没有可识别的声音：打开麦克风或电脑声音后开始生成字幕");
        //按中间结果的样式（颜色稍淡）画，与真正的字幕区分开
        line.m_partial = true;
        lines.push_back(line);
    }
    //置顶、排除在采集之外（录制条的做法），没有所有者窗口
    m_overlay.Update(m_area, true, NULL, lines, CMediaCaptionBridge::Instance().Acquire());
}

LRESULT CRecordCaptionLive::OnPendingMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    std::vector<Pending> items;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        items.swap(m_pending);
        m_posted = false;
    }
    if (!m_active || !m_enabled)
        return 0;
    const int64_t now = NowMs();
    const std::wstring systemLabel = TR(L"电脑声音");
    bool changed = false;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const Pending& p = items[i];
        const uint32_t id = m_sentences.Take(p.m_source, p.m_final);
        const std::wstring speaker = RecordCaptionSpeaker(p.m_source, m_micOn, m_systemOn, m_selfName, systemLabel);
        if (m_board.OnCaption(p.m_source, speaker, p.m_text, p.m_final, id, now))
            changed = true;
    }
    if (changed)
        refresh();
    return 0;
}

} // namespace flamingomedia
