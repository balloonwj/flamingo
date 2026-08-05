/**
 *  FaceImagePickerPopup.cpp —— 图片表情选择浮层实现。
 *
 *  网格直接复用 balloonui 的 DuiEmojiPanel：从 Face/FaceConfig.xml 加载表情库，
 *  把每个 face<N>.png 解码成 HBITMAP 经 AddEmojiBitmap 喂进面板；面板自带 hover /
 *  点击 / 布局。点中某格 → 据格子索引取回 (faceId, 路径) 经回调交回聊天窗。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#include "stdafx.h"
#include "FaceImagePickerPopup.h"

#include <memory>
#include <gdiplus.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM

#include "../IULog.h"
#include "../Path.h"   // Hootina::CPath::GetAppPath
#include "../../balloonui/balloonui/Controls/Feedback/DuiEmojiPanel.h"
#include "../../balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   // DuiToolTipMgr（逐格 tooltip）

// ---- 文件作用域常量 -----------------------------------------------------

// 网格布局参数（逻辑像素）。
static const int kCols     = 12;   // 每行列数
// 单元格边长。DuiEmojiPanel 画格子时四周各留 2px 内缩，故图实际占 kCellSize-4；
// 取 32 使之为 28 —— face PNG 原生 56×56，56→28 是 1/2 整数倍缩小，重采样几乎
// 无损，且与输入框 / 气泡里的内联表情边长（ChatFaceTag.h 的 kFaceDisplayPx）一致。
// 早先的 30 源自"face PNG 约 28px"这一错误认识，会得到 26px 的非整数倍缩放。
static const int kCellSize = 32;
static const int kPanelPad = 6;    // 面板四周内距（让格子不贴浮层边框）

// ---- GDI+ 一次性初始化（解码 PNG 用；token 进程退出时随进程释放）---------

static void EnsureGdiPlusForFace()
{
    static ULONG_PTR s_token = 0;
    if (s_token != 0)
    {
        return;
    }
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&s_token, &input, NULL);
}

// 把一张图片文件按原生分辨率解码成"32bpp 预乘 alpha"的 DIBSection，透明通道
// 原样保留。失败返回 NULL；成功时所有权归调用方（用完 DeleteObject）。
//
// 之前这里用的是 Gdiplus::Bitmap::GetHBITMAP(Color(255,255,255))，那个接口会把
// alpha 直接压平到白色上，于是每个表情都自带一块不透明白底 —— 鼠标划过时格子的
// 高亮底色被这块白底盖住，只在四周露出一圈。DuiEmojiPanel 绘制时会识别 32bpp
// 的 DIBSection 并按预乘 alpha 混合（见其 MakeGdipBitmapFromHbm），所以这里换成
// 带 alpha 的 DIB 就能让表情正确地叠在高亮底色上。
//
// <u>位图必须建成"底朝上"（biHeight 传正数）</u>。实测发现：即便用负 biHeight 建成
// 顶朝下，GetObject 取回的 DIBSECTION::dsBmih.biHeight 仍是正数 —— 方向信息在这
// 一步就丢了。而 MakeGdipBitmapFromHbm 只能靠这个符号判断行序，于是一律按底朝上
// 解释；这里若产出顶朝下的数据，表情画出来就是上下颠倒的。
static HBITMAP LoadFaceBitmap(LPCTSTR path)
{
    if (path == NULL || *path == _T('\0'))
    {
        return NULL;
    }
    EnsureGdiPlusForFace();

    Gdiplus::Bitmap bmp(path);
    if (bmp.GetLastStatus() != Gdiplus::Ok)
    {
        return NULL;
    }
    const int nWidth = (int)bmp.GetWidth();
    const int nHeight = (int)bmp.GetHeight();
    if (nWidth <= 0 || nHeight <= 0)
    {
        return NULL;
    }

    // biHeight 取正 = 底朝上：内存里的第 0 行是图像的最后一行。理由见上面注释。
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = nWidth;
    bi.bmiHeader.biHeight      = nHeight;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hbm = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (hbm == NULL || pBits == NULL)
    {
        if (hbm != NULL)
        {
            ::DeleteObject(hbm);
        }
        return NULL;
    }

    // 32bpp 每行天然 4 字节对齐，行字节数就是 宽度 × 4。
    const int kBytesPerPixel = 4;
    const int nDstStride = nWidth * kBytesPerPixel;

    Gdiplus::Rect rcAll(0, 0, nWidth, nHeight);
    Gdiplus::BitmapData bd;
    ::ZeroMemory(&bd, sizeof(bd));
    if (bmp.LockBits(&rcAll, Gdiplus::ImageLockModeRead,
                     PixelFormat32bppPARGB, &bd) != Gdiplus::Ok)
    {
        ::DeleteObject(hbm);
        return NULL;
    }
    // GDI+ 的第 y 行是图像自上而下的第 y 行；目标 DIB 底朝上，故写到倒数第 y
    // 行去，等于在拷贝的同时完成上下翻转。
    for (int y = 0; y < nHeight; ++y)
    {
        const BYTE* pSrcRow = (const BYTE*)bd.Scan0 + (INT_PTR)bd.Stride * y;
        BYTE* pDstRow = (BYTE*)pBits + (INT_PTR)nDstStride * (nHeight - 1 - y);
        ::memcpy(pDstRow, pSrcRow, nDstStride);
    }
    bmp.UnlockBits(&bd);

    // GDI 对 DIBSection 的写入有缓存，交给别的 GDI / GDI+ 调用前须冲一下，
    // 否则首次绘制可能读到尚未落盘的像素。
    ::GdiFlush();
    return hbm;
}

// =======================================================================
// 构造 / 析构
// =======================================================================

CFaceImagePickerPopup::CFaceImagePickerPopup()
    : m_pickCb(NULL)
    , m_pickUd(NULL)
    , m_built(false)
    , m_pPanel(NULL)
    , m_lastTipIdx(-1)
{
}

CFaceImagePickerPopup::~CFaceImagePickerPopup()
{
    // tooltip 注册按裸指针匹配，须在面板（随内容树）销毁前取消，避免管理器
    // 留下悬空指针。
    if (m_pPanel != NULL)
    {
        balloonwjui::DuiToolTipMgr::Inst().Unregister(m_pPanel);
        m_pPanel = NULL;
    }

    // 控件树由 DuiHost 基类析构释放；本类持有的表情 HBITMAP 须显式删除。
    for (size_t i = 0; i < m_faceBitmaps.size(); ++i)
    {
        if (m_faceBitmaps[i] != NULL)
        {
            ::DeleteObject(m_faceBitmaps[i]);
        }
    }
    m_faceBitmaps.clear();
}

void CFaceImagePickerPopup::SetPickCallback(PickCallback cb, void* userdata)
{
    m_pickCb = cb;
    m_pickUd = userdata;
}

// =======================================================================
// 控件树构建
// =======================================================================

void CFaceImagePickerPopup::BuildContentOnce()
{
    if (m_built)
    {
        return;
    }

    // 加载表情库（Face/FaceConfig.xml）。去重后 id 与 face<N>.png 一一对应。
    tstring strConfig = Hootina::CPath::GetAppPath() + _T("Face\\FaceConfig.xml");
    m_faceList.LoadConfigFile(strConfig.c_str());

    std::unique_ptr<balloonwjui::DuiEmojiPanel> panel(new balloonwjui::DuiEmojiPanel());
    panel->SetColumns(kCols);
    panel->SetCellSize(kCellSize);

    // 逐个表情：解码 PNG → HBITMAP → 喂面板。sequence 仅作占位（本浮层不按文本
    // 插入，而是经索引回调 faceId）；tooltip 用表情库里的中文名。
    for (size_t i = 0; i < m_faceList.m_arrFaceInfo.size(); ++i)
    {
        CFaceInfo* lpInfo = m_faceList.m_arrFaceInfo[i];
        if (lpInfo == NULL)
        {
            continue;
        }
        HBITMAP hbm = LoadFaceBitmap(lpInfo->m_strFileName.c_str());
        if (hbm == NULL)
        {
            continue;   // 单张解码失败：跳过该格，不影响其余
        }

        CString seq;
        seq.Format(_T("%d"), lpInfo->m_nId);
        panel->AddEmojiBitmap(seq, hbm, lpInfo->m_strTip.c_str());

        m_faceInfos.push_back(lpInfo);
        m_faceBitmaps.push_back(hbm);
    }

    panel->SetPickCallback(&CFaceImagePickerPopup::OnPanelPick, this);

    SIZE ds = panel->GetDesiredSize();
    m_pPanel = panel.get();   // 留裸指针供逐格 tooltip / 命中测试（所有权随下行交给内容树）
    SetContent(std::move(panel));

    // 浮层尺寸 = 面板期望尺寸 + 四周内距。
    SetSize(ds.cx + 2 * kPanelPad, ds.cy + 2 * kPanelPad);
    // 工具栏在窗口底部，故面板优先朝上弹出（越界时 DuiPopupHost 自动翻边）。
    SetEdge(balloonwjui::DuiPopupHost::EdgeAbove);

    m_built = true;
}

// =======================================================================
// 打开
// =======================================================================

void CFaceImagePickerPopup::OpenAt(const RECT& anchorScreen, HWND /*owner*/)
{
    BuildContentOnce();

    if (m_faceInfos.empty())
    {
        LOG_WARNING(_T("图片表情面板无可用表情（表情库为空或全部解码失败）。"));
    }

    // owner 传 NULL：与 CEmojiPickerPopup 一致 —— 让面板自己的 pick 回调直达本浮层，
    // 不把子控件通知误路由到聊天窗。
    Show(anchorScreen, NULL);
    LOG_INFO(_T("图片表情选择面板已打开（共 %d 个表情）。"), (int)m_faceInfos.size());
}

// =======================================================================
// 面板选中回调
// =======================================================================

void CFaceImagePickerPopup::OnPanelPick(void* userdata, const wchar_t* /*sequence*/,
                                        int index)
{
    CFaceImagePickerPopup* self = (CFaceImagePickerPopup*)userdata;
    if (self == NULL)
    {
        return;
    }
    if (index < 0 || index >= (int)self->m_faceInfos.size())
    {
        return;
    }
    CFaceInfo* lpInfo = self->m_faceInfos[index];
    if (lpInfo == NULL)
    {
        return;
    }

    // 把 (faceId, 路径) 交回聊天窗（面板保持打开，便于连续选；关闭交给 ESC / 失焦）。
    if (self->m_pickCb != NULL)
    {
        self->m_pickCb(self->m_pickUd, lpInfo->m_nId, lpInfo->m_strFileName.c_str());
    }
}

// =======================================================================
// 逐格 tooltip
// =======================================================================

LRESULT CFaceImagePickerPopup::OnPanelMouseMove(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                LPARAM lParam, BOOL& bHandled)
{
    bHandled = FALSE;   // 透传给基类：hover 高亮等既有派发照常进行
    if (m_pPanel == NULL)
    {
        return 0;
    }

    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    int idx = m_pPanel->HitTestIndex(pt);
    if (idx == m_lastTipIdx)
    {
        return 0;   // 同格内移动：不重启 tooltip 计时
    }
    m_lastTipIdx = idx;

    balloonwjui::DuiToolTipMgr& mgr = balloonwjui::DuiToolTipMgr::Inst();
    mgr.OnLeave(m_pPanel);   // 关旧格 tip / 取消未弹出的计时

    if (idx >= 0 && idx < (int)m_faceInfos.size() && m_faceInfos[idx] != NULL)
    {
        // 表情库的 tip 形如 "/呲牙"，展示时去掉斜杠前缀（那是老协议的输入习惯）。
        CString tip = m_faceInfos[idx]->m_strTip.c_str();
        if (!tip.IsEmpty() && tip[0] == _T('/'))
        {
            tip = tip.Mid(1);
        }
        if (!tip.IsEmpty())
        {
            POINT screenPt = pt;
            ::ClientToScreen(m_hWnd, &screenPt);
            mgr.Register(m_pPanel, tip);     // 重复 Register 同控件 = 覆盖文字
            mgr.OnHover(m_pPanel, screenPt); // 重新计时，延时后在新格位置弹出
        }
    }
    return 0;
}

LRESULT CFaceImagePickerPopup::OnPanelMouseLeave(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                 LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = FALSE;   // 基类 DuiHost::OnMouseLeave 自会关 tooltip
    m_lastTipIdx = -1;
    return 0;
}
