/**
 *  帖子群窗口的表情选择浮层，见 ThreadFacePicker.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ThreadFacePicker.h"

#include <memory>
#include <gdiplus.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM

#include "../../Source/Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiEmojiPanel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   // DuiToolTipMgr（逐格提示）

namespace threadgroup {

namespace {

//格子布局（与主程序的表情浮层相同）：每行列数、格子边长（表情图 56 px，格子四周内缩 2 px 后正好 1/2 缩小）、面板四周内距
const int kCols = 12;
const int kCellSize = 32;
const int kPanelPad = 6;
const int kBytesPerPixel = 4;   //32 位位图每个像素的字节数

//GDI+ 只启动一次（解码 PNG 用；进程退出时随进程释放）
void EnsureGdiPlus()
{
    static ULONG_PTR s_token = 0;
    if (s_token != 0)
        return;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&s_token, &input, NULL);
}

/**
 *  把一张图片解码成 32 位预乘透明度的底朝上位图（DuiEmojiPanel 按预乘透明度混合，透明的四角才不会盖住悬停底色）。
 *  做法与理由同主程序 FaceImagePickerPopup.cpp 的 LoadFaceBitmap。
 *  @return 位图（调用方负责 DeleteObject）；读不出来时返回 NULL
 */
HBITMAP LoadFaceBitmap(const std::wstring& path)
{
    if (path.empty())
        return NULL;
    EnsureGdiPlus();
    Gdiplus::Bitmap bmp(path.c_str());
    if (bmp.GetLastStatus() != Gdiplus::Ok)
        return NULL;
    const int width = static_cast<int>(bmp.GetWidth());
    const int height = static_cast<int>(bmp.GetHeight());
    if (width <= 0 || height <= 0)
        return NULL;
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = height;   //正数：底朝上（DuiEmojiPanel 按这个方向解释）
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP hbm = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hbm == NULL || bits == NULL)
    {
        if (hbm != NULL)
            ::DeleteObject(hbm);
        return NULL;
    }
    const int stride = width * kBytesPerPixel;
    Gdiplus::Rect all(0, 0, width, height);
    Gdiplus::BitmapData data;
    ::ZeroMemory(&data, sizeof(data));
    if (bmp.LockBits(&all, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data) != Gdiplus::Ok)
    {
        ::DeleteObject(hbm);
        return NULL;
    }
    //GDI+ 的第 y 行是自上而下的第 y 行，目标位图底朝上，拷贝时上下翻转
    for (int y = 0; y < height; ++y)
    {
        const BYTE* src = static_cast<const BYTE*>(data.Scan0) + static_cast<INT_PTR>(data.Stride) * y;
        BYTE* dst = static_cast<BYTE*>(bits) + static_cast<INT_PTR>(stride) * (height - 1 - y);
        ::memcpy(dst, src, stride);
    }
    bmp.UnlockBits(&data);
    ::GdiFlush();
    return hbm;
}

} // namespace

CThreadFacePicker::CThreadFacePicker()
    : m_pickFn(NULL)
    , m_pickUser(NULL)
    , m_built(false)
    , m_panel(NULL)
    , m_lastTipIdx(-1)
{
}

CThreadFacePicker::~CThreadFacePicker()
{
    //提示管理器按控件指针登记，面板随控件树销毁之前先撤销
    if (m_panel != NULL)
    {
        balloonwjui::DuiToolTipMgr::Inst().Unregister(m_panel);
        m_panel = NULL;
    }
    for (size_t i = 0; i < m_bitmaps.size(); ++i)
    {
        if (m_bitmaps[i] != NULL)
            ::DeleteObject(m_bitmaps[i]);
    }
    m_bitmaps.clear();
}

void CThreadFacePicker::SetPickCallback(PickFn fn, void* user)
{
    m_pickFn = fn;
    m_pickUser = user;
}

void CThreadFacePicker::OpenAt(const RECT& anchorScreen, const std::vector<ThreadFaceEntry>& faces)
{
    BuildOnce(faces);
    if (m_faceIds.empty())
        LOG_WARNING("thread face picker has no face (library empty or all images failed to load).");
    //所有者传 NULL：面板的选中回调直达本浮层，不把子控件通知路由到帖子群窗口
    Show(anchorScreen, NULL);
}

void CThreadFacePicker::BuildOnce(const std::vector<ThreadFaceEntry>& faces)
{
    if (m_built)
        return;
    m_built = true;
    std::unique_ptr<balloonwjui::DuiEmojiPanel> panel(new balloonwjui::DuiEmojiPanel());
    panel->SetColumns(kCols);
    panel->SetCellSize(kCellSize);
    for (size_t i = 0; i < faces.size(); ++i)
    {
        HBITMAP hbm = LoadFaceBitmap(faces[i].m_path);
        if (hbm == NULL)
            continue;
        //sequence 只作占位（选中时按格子序号取表情编号）
        const std::wstring sequence = std::to_wstring(static_cast<long long>(faces[i].m_faceId));
        panel->AddEmojiBitmap(sequence.c_str(), hbm, faces[i].m_name.c_str());
        m_faceIds.push_back(faces[i].m_faceId);
        m_faceNames.push_back(faces[i].m_name);
        m_bitmaps.push_back(hbm);
    }
    panel->SetPickCallback(&CThreadFacePicker::OnPanelPick, this);
    const SIZE desired = panel->GetDesiredSize();
    m_panel = panel.get();
    SetContent(std::move(panel));
    SetSize(desired.cx + 2 * kPanelPad, desired.cy + 2 * kPanelPad);
    SetEdge(balloonwjui::DuiPopupHost::EdgeAbove);
    LOG_INFO("thread face picker built, %u face(s).", static_cast<unsigned>(m_faceIds.size()));
}

void CThreadFacePicker::OnPanelPick(void* user, LPCTSTR sequence, int index)
{
    (void)sequence;
    CThreadFacePicker* self = static_cast<CThreadFacePicker*>(user);
    if (self == NULL || index < 0 || index >= static_cast<int>(self->m_faceIds.size()))
        return;
    if (self->m_pickFn != NULL)
        self->m_pickFn(self->m_pickUser, self->m_faceIds[index]);
}

LRESULT CThreadFacePicker::OnPanelMouseMove(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    bHandled = FALSE;   //交给基类：悬停高亮照常进行
    if (m_panel == NULL)
        return 0;
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    const int idx = m_panel->HitTestIndex(pt);
    if (idx == m_lastTipIdx)
        return 0;
    m_lastTipIdx = idx;
    balloonwjui::DuiToolTipMgr& mgr = balloonwjui::DuiToolTipMgr::Inst();
    mgr.OnLeave(m_panel);
    if (idx >= 0 && idx < static_cast<int>(m_faceNames.size()) && !m_faceNames[idx].empty())
    {
        POINT screenPt = pt;
        ::ClientToScreen(m_hWnd, &screenPt);
        mgr.Register(m_panel, m_faceNames[idx].c_str());
        mgr.OnHover(m_panel, screenPt);
    }
    return 0;
}

LRESULT CThreadFacePicker::OnPanelMouseLeave(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = FALSE;
    m_lastTipIdx = -1;
    return 0;
}

} // namespace threadgroup
