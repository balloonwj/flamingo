/**
 *  ChatFileCard.cpp
 *
 *  文件卡片的尺寸计算、绘制与点击判断，见 ChatFileCard.h。绘制代码原样取自 ChatBubble.cpp
 *  的文件气泡分支，几何常量、配色与文字均未改动。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-01
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖，与同目录 ChatBubble.cpp 相同 ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限
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

#include <gdiplus.h>

#include "ChatFileCard.h"
#include "ChatPaintUtil.h"     // AddRoundRect / DrawGdiText / MeasureTextWidth
#include "ChatFileVisual.h"    // FileTypeColor（与消息记录窗共用同一套配色）
#include "ChatFileAction.h"    // ChatFileActionKind

namespace balloonchatwnd {

namespace {

using chatpaint::AddRoundRect;
using chatpaint::DrawGdiText;
using chatpaint::MeasureTextWidth;

// 文件卡片几何常量（逻辑像素），与聊天窗文件气泡一致。
const int kFilePadH         = 14;   // 卡片水平内边距（每侧）
const int kFileIconSize     = 40;   // 类型图标边长（圆角方块）
const int kFileIconTop      = 10;   // 图标距卡片顶
const int kFileNameTop      = 12;   // 文件名距卡片顶
const int kFileNameH        = 20;   // 文件名行高
const int kFileStatusTop    = 34;   // "大小 · 状态"行距卡片顶
const int kFileStatusH      = 18;   // 状态行高
const int kFileHeaderH      = 60;   // 头部区高（图标 + 名 + 状态）
const int kFileProgressH    = 5;    // 进度条厚
const int kFileBtnTopGap    = 10;   // 按钮行距上方
const int kFileBtnH         = 34;   // 按钮高
const int kFileBtnBottomPad = 12;   // 按钮行下边距
const int kFileBadgeR       = 9;    // 图标角标半径

// 文件名的颜色，与聊天气泡正文同色
const COLORREF kFileCardText = RGB(0x2A, 0x2A, 0x2A);

} // anonymous namespace

FileCardLinks::FileCardLinks()
    : m_count(0)
{
    for (int i = 0; i < kMaxChatFileActions; ++i)
    {
        ::SetRectEmpty(&m_rect[i]);
        m_action[i] = 0;
    }
}

bool FileCardSkipsReject(const ChatMessage& m)
{
    return m.fromMe || m.fileDirectDownload;
}

bool FileCardCanResend(const ChatMessage& m)
{
    return m.fromMe
        && !m.fileClientMsgId.empty()
        && !m.fileLocalPath.IsEmpty();
}

int FileCardHeight(const ChatMessage& m)
{
    //是否带进度条、是否有按钮行都随状态变化；fromMe 只影响待接收态的按钮个数，只要还有
    //按钮，行高就是同一份
    const int st = m.fileState;
    int h = kFileHeaderH;
    if (FileStateHasProgress(st))
    {
        h += kFileProgressH;
    }
    if (FileStateButtonCount(st, FileCardSkipsReject(m)) > 0)
    {
        h += kFileBtnTopGap + kFileBtnH + kFileBtnBottomPad;
    }
    return h;
}

void PaintFileCardShapes(Gdiplus::Graphics& g, const RECT& card, const ChatMessage& m)
{
    const int  bubbleX = card.left;
    const int  bubbleY = card.top;
    const int  bubbleW = card.right - card.left;
    const int  st      = m.fileState;
    const bool deleted = (st == kFileDeleted);
    const int  iconX   = bubbleX + kFilePadH;
    const int  iconY   = bubbleY + kFileIconTop;

    // ---- 类型图标（圆角方块）：正常用类型色，已删除用灰 ----
    COLORREF ftc = deleted ? RGB(0xB8, 0xB5, 0xB1) : FileTypeColor(m.fileType);
    {
        Gdiplus::GraphicsPath ftPath;
        AddRoundRect(ftPath, (Gdiplus::REAL)iconX, (Gdiplus::REAL)iconY,
                     (Gdiplus::REAL)kFileIconSize, (Gdiplus::REAL)kFileIconSize, 6.0f);
        Gdiplus::SolidBrush ftBrush(Gdiplus::Color(255,
            GetRValue(ftc), GetGValue(ftc), GetBValue(ftc)));
        g.FillPath(&ftBrush, &ftPath);
    }

    // 已删除：图标上画一个白色垃圾桶轮廓（盖 + 提手 + 桶身 + 竖纹）。
    if (deleted)
    {
        Gdiplus::Pen   trash(Gdiplus::Color(255, 255, 255, 255), 1.6f);
        Gdiplus::REAL  cx = (Gdiplus::REAL)(iconX + kFileIconSize / 2);
        Gdiplus::REAL  ty = (Gdiplus::REAL)(iconY + 14);   // 盖的 y
        g.DrawLine(&trash, cx - 9, ty, cx + 9, ty);                 // 盖
        g.DrawLine(&trash, cx - 3, ty - 3, cx + 3, ty - 3);         // 提手
        g.DrawLine(&trash, cx - 3, ty - 3, cx - 3, ty);
        g.DrawLine(&trash, cx + 3, ty - 3, cx + 3, ty);
        const Gdiplus::REAL bx = cx - 7;
        const Gdiplus::REAL bw = 14;
        const Gdiplus::REAL byTop = ty + 3;
        const Gdiplus::REAL bh = 12;
        g.DrawLine(&trash, bx, byTop, bx + 1.5f, byTop + bh);       // 桶身左壁
        g.DrawLine(&trash, bx + bw, byTop, bx + bw - 1.5f, byTop + bh);  // 右壁
        g.DrawLine(&trash, bx + 1.5f, byTop + bh, bx + bw - 1.5f, byTop + bh);  // 底
        g.DrawLine(&trash, cx - 3, byTop + 2, cx - 3, byTop + bh - 2);   // 竖纹
        g.DrawLine(&trash, cx + 3, byTop + 2, cx + 3, byTop + bh - 2);
    }

    // ---- 状态角标（图标左下角小圆）：完成绿✓、失败红×、已取消灰停 ----
    if (st == kFileNormal || st == kFileFailed || st == kFileCancelled)
    {
        const Gdiplus::REAL bcx = (Gdiplus::REAL)(iconX + 3);
        const Gdiplus::REAL bcy = (Gdiplus::REAL)(iconY + kFileIconSize - 3);
        const Gdiplus::REAL r   = (Gdiplus::REAL)kFileBadgeR;
        COLORREF badgeCol = (st == kFileNormal) ? RGB(0x3F, 0xB9, 0x50)
                          : (st == kFileFailed) ? RGB(0xE5, 0x39, 0x35)
                          :                       RGB(0x9A, 0x97, 0x92);
        Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255, 255));
        g.FillEllipse(&white, bcx - r - 1.5f, bcy - r - 1.5f, (r + 1.5f) * 2, (r + 1.5f) * 2);
        Gdiplus::SolidBrush bb(Gdiplus::Color(255,
            GetRValue(badgeCol), GetGValue(badgeCol), GetBValue(badgeCol)));
        g.FillEllipse(&bb, bcx - r, bcy - r, r * 2, r * 2);
        Gdiplus::Pen mark(Gdiplus::Color(255, 255, 255, 255), 1.8f);
        if (st == kFileNormal)
        {
            g.DrawLine(&mark, bcx - 4, bcy, bcx - 1, bcy + 3);       // ✓
            g.DrawLine(&mark, bcx - 1, bcy + 3, bcx + 4, bcy - 3);
        }
        else if (st == kFileFailed)
        {
            g.DrawLine(&mark, bcx - 3, bcy - 3, bcx + 3, bcy + 3);   // ×
            g.DrawLine(&mark, bcx + 3, bcy - 3, bcx - 3, bcy + 3);
        }
        else
        {
            Gdiplus::SolidBrush sq(Gdiplus::Color(255, 255, 255, 255));
            g.FillRectangle(&sq, bcx - 3, bcy - 3, 6.0f, 6.0f);       // 停（白方块）
        }
    }

    // ---- 进度条（上传 / 下载 / 失败 / 已取消）----
    if (FileStateHasProgress(st))
    {
        const int barX = bubbleX + kFilePadH;
        const int barY = bubbleY + kFileHeaderH;
        const int barW = bubbleW - kFilePadH * 2;
        {
            Gdiplus::GraphicsPath track;
            AddRoundRect(track, (Gdiplus::REAL)barX, (Gdiplus::REAL)barY,
                         (Gdiplus::REAL)barW, (Gdiplus::REAL)kFileProgressH, 2.5f);
            Gdiplus::SolidBrush trackBrush(Gdiplus::Color(255, 0xE3, 0xE0, 0xDC));
            g.FillPath(&trackBrush, &track);
        }
        int pct = m.fileProgress;
        if (pct < 0)
        {
            pct = 0;
        }
        if (pct > 100)
        {
            pct = 100;
        }
        const int fillW = barW * pct / 100;
        if (fillW > 0)
        {
            COLORREF pc = (st == kFileUploading)   ? RGB(0xFF, 0x7A, 0x45)   // 橙
                        : (st == kFileDownloading) ? RGB(0x2F, 0x80, 0xED)   // 蓝
                        : (st == kFileFailed)      ? RGB(0xE5, 0x39, 0x35)   // 红
                        :                            RGB(0xB0, 0xAD, 0xA8);  // 灰（已取消）
            Gdiplus::GraphicsPath fill;
            AddRoundRect(fill, (Gdiplus::REAL)barX, (Gdiplus::REAL)barY,
                         (Gdiplus::REAL)fillW, (Gdiplus::REAL)kFileProgressH, 2.5f);
            Gdiplus::SolidBrush fb(Gdiplus::Color(255,
                GetRValue(pc), GetGValue(pc), GetBValue(pc)));
            g.FillPath(&fb, &fill);
        }
    }
}

void PaintFileCardText(HDC hdc, Gdiplus::Graphics& g, const RECT& card, const ChatMessage& m,
                       int hoverIdx, FileCardLinks& links)
{
    const int  bubbleX = card.left;
    const int  bubbleY = card.top;
    const int  bubbleW = card.right - card.left;
    const int  st      = m.fileState;
    const bool deleted = (st == kFileDeleted);
    const int  iconX   = bubbleX + kFilePadH;
    const int  iconY   = bubbleY + kFileIconTop;
    const int  textX   = iconX + kFileIconSize + 12;          // 名 / 状态左
    const int  textR   = bubbleX + bubbleW - kFilePadH;       // 名 / 状态右

    // 类型字（已删除处是垃圾桶，不画类型字）。
    if (!deleted)
    {
        CString typeUpper = m.fileType;
        typeUpper.MakeUpper();
        RECT ftRc = { iconX, iconY, iconX + kFileIconSize, iconY + kFileIconSize };
        DrawGdiText(hdc, (LPCWSTR)typeUpper, ftRc, 10, true, RGB(255, 255, 255),
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // 文件名（已删除：灰 + 删除线）。
    const COLORREF nameColor = deleted ? RGB(0x9A, 0x97, 0x92) : kFileCardText;
    RECT fnRc = { textX, bubbleY + kFileNameTop, textR, bubbleY + kFileNameTop + kFileNameH };
    DrawGdiText(hdc, (LPCWSTR)m.fileName, fnRc, 14, false, nameColor,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                false, false, deleted /*strike*/);

    // 状态行："{大小} · {状态}"：大小段灰，状态段按态着色；失败 / 删除前带小图标。
    const int statusY = bubbleY + kFileStatusTop;
    const int statusB = statusY + kFileStatusH;
    CString sizePrefix = m.fileSize + _T(" · ");
    RECT spRc = { textX, statusY, textR, statusB };
    DrawGdiText(hdc, (LPCWSTR)sizePrefix, spRc, 11, false, RGB(0x8A, 0x8A, 0x8A),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    int sx = textX + MeasureTextWidth(hdc, (LPCWSTR)sizePrefix, 11);

    if (st == kFileFailed)
    {
        // 警告三角（红底白叹号）。
        const Gdiplus::REAL tcx = (Gdiplus::REAL)(sx + 6);
        const Gdiplus::REAL tcy = (Gdiplus::REAL)((statusY + statusB) / 2);
        Gdiplus::PointF tri[3] = {
            Gdiplus::PointF(tcx, tcy - 6),
            Gdiplus::PointF(tcx - 6, tcy + 5),
            Gdiplus::PointF(tcx + 6, tcy + 5) };
        Gdiplus::SolidBrush warn(Gdiplus::Color(255, 0xE5, 0x39, 0x35));
        g.FillPolygon(&warn, tri, 3);
        Gdiplus::Pen excl(Gdiplus::Color(255, 255, 255, 255), 1.4f);
        g.DrawLine(&excl, tcx, tcy - 2, tcx, tcy + 1);
        sx += 16;
    }
    else if (deleted)
    {
        // 信息圆（灰底白 i）。
        const Gdiplus::REAL icx = (Gdiplus::REAL)(sx + 6);
        const Gdiplus::REAL icy = (Gdiplus::REAL)((statusY + statusB) / 2);
        Gdiplus::SolidBrush info(Gdiplus::Color(255, 0x9A, 0x97, 0x92));
        g.FillEllipse(&info, icx - 6, icy - 6, 12.0f, 12.0f);
        Gdiplus::Pen ip(Gdiplus::Color(255, 255, 255, 255), 1.4f);
        g.DrawLine(&ip, icx, icy - 1, icx, icy + 3);
        Gdiplus::SolidBrush dot(Gdiplus::Color(255, 255, 255, 255));
        g.FillRectangle(&dot, icx - 0.7f, icy - 4.0f, 1.4f, 1.4f);
        sx += 16;
    }

    CString  statusText;
    COLORREF statusColor = RGB(0x8A, 0x8A, 0x8A);
    bool     statusBold  = false;
    if (st == kFileUploading)
    {
        statusText.Format(_T("传送中 %d%%"), m.fileProgress);
        statusColor = RGB(0xFF, 0x6A, 0x3D);
        statusBold  = true;
    }
    else if (st == kFileDownloading)
    {
        statusText.Format(_T("下载中 %d%%"), m.fileProgress);
    }
    else if (st == kFileNormal)
    {
        statusText  = m.fromMe ? _T("传送完成") : _T("已完成");
        statusColor = RGB(0xFF, 0x7A, 0x45);
        statusBold  = true;
    }
    else if (st == kFileFailed)
    {
        statusText  = _T("传输失败");
        statusColor = RGB(0xE5, 0x39, 0x35);
        statusBold  = true;
    }
    else if (st == kFileCancelled)
    {
        statusText = _T("已取消");
    }
    else if (st == kFilePendingAccept && m.fileDirectDownload)
    {
        //群聊或聊天记录里的文件还没下载：没有接收流程，只说明本地还没有这份文件
        statusText = _T("未下载");
    }
    else if (st == kFilePendingAccept)
    {
        //还没开始下载，提示用户需要自己决定收不收
        statusText  = _T("等待接收");
        statusColor = RGB(0xFF, 0x7A, 0x45);
        statusBold  = true;
    }
    else if (st == kFileRejected)
    {
        statusText  = _T("已拒绝接收");
        statusColor = RGB(0x9A, 0x97, 0x92);
    }
    else   // deleted
    {
        statusText  = _T("文件已删除，无法下载");
        statusColor = RGB(0x9A, 0x97, 0x92);
    }

    // 本端发出的文件：只要对方有过动作，状态行就改说对方那边的进展 —— 自己传完了
    // 不等于对方收了，后者才是发送方真正关心的。覆盖上面按 fileState 算出的文字。
    if (m.fromMe && m.filePeerState != 0)
    {
        if (m.filePeerState == kFilePeerReceiving)
        {
            statusText  = _T("对方正在接收");
            statusColor = RGB(0xFF, 0x7A, 0x45);
            statusBold  = true;
        }
        else if (m.filePeerState == kFilePeerReceived)
        {
            statusText  = _T("对方已接收");
            statusColor = RGB(0x3B, 0xA5, 0x5D);   // 绿：这条文件已经安全送达
            statusBold  = true;
        }
        else if (m.filePeerState == kFilePeerRejected)
        {
            statusText  = _T("对方已拒绝接收");
            statusColor = RGB(0x9A, 0x97, 0x92);
            statusBold  = false;
        }
    }

    RECT stRc = { sx, statusY, textR, statusB };
    DrawGdiText(hdc, (LPCWSTR)statusText, stRc, 11, statusBold, statusColor,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

    // ---- 底部操作：文字超链接（无按钮底 / 边框）。按各自文字宽度从左到右排布，
    //      整行高度作点击矩形（便于点击），写进 links。----
    links = FileCardLinks();
    int acts[kMaxChatFileActions];
    links.m_count = FileButtonActions(st, FileCardSkipsReject(m), acts);
    if (links.m_count <= 0)
    {
        links.m_count = 0;
        return;
    }
    const int kLinkPx  = 13;   // 超链接文字字号
    const int kLinkGap = 18;   // 相邻超链接水平间距
    const int rowTop    = bubbleY + kFileHeaderH
                        + (FileStateHasProgress(st) ? kFileProgressH : 0)
                        + kFileBtnTopGap;
    const int rowBottom = rowTop + kFileBtnH;
    int linkX = bubbleX + kFilePadH;
    for (int bi = 0; bi < links.m_count; ++bi)
    {
        const int act = acts[bi];
        CString   label;
        COLORREF  lblColor = RGB(0xFF, 0x7A, 0x45);   // 默认主题橙（可点链接）
        switch (act)
        {
        //完成态：用关联程序打开本地文件
        case kChatFileOpen:
            label = _T("打开文件");
            break;
        //完成态：在资源管理器里定位本地文件
        case kChatFileOpenFolder:
            label = _T("打开所在文件夹");
            break;
        //完成态：把本地文件另存到别处
        case kChatFileSaveAs:
            label = _T("另存为");
            break;
        //传输中：取消这次上传或下载，灰色链接
        case kChatFileCancel:
            label    = _T("取消");
            lblColor = RGB(0x8A, 0x8A, 0x8A);
            break;
        //待接收：开始下载。单聊里对方发来的写「接收」；自己发出的、群聊与聊天记录里的
        //文件谈不上接收，只是把它从服务器取回本地，写「下载」。主动作用主题橙突出
        case kChatFileAccept:
            label = FileCardSkipsReject(m) ? _T("下载") : _T("接收");
            break;
        //单聊待接收：拒绝接收，次要动作用灰色链接
        case kChatFileReject:
            label    = _T("拒绝");
            lblColor = RGB(0x8A, 0x8A, 0x8A);
            break;
        //失败、已取消、已拒绝：重新发送或重新下载（\x21BB 为循环箭头）。是重发还是重下按
        //手头数据判定（见 FileCardCanResend）。失败态用红，其余用主题橙
        case kChatFileRetry:
        default:
            label    = FileCardCanResend(m) ? _T("\x21BB 重新发送") : _T("\x21BB 重新下载");
            lblColor = (st == kFileFailed) ? RGB(0xE5, 0x39, 0x35) : RGB(0xFF, 0x7A, 0x45);
            break;
        }
        const int linkW = MeasureTextWidth(hdc, (LPCWSTR)label, kLinkPx);
        RECT linkRc = { linkX, rowTop, linkX + linkW, rowBottom };
        // 悬停的链接：颜色加深（×4/5）+ 加下划线，作悬停反馈。
        const bool hover = (bi == hoverIdx);
        const COLORREF drawColor = hover
            ? RGB(GetRValue(lblColor) * 4 / 5, GetGValue(lblColor) * 4 / 5,
                  GetBValue(lblColor) * 4 / 5)
            : lblColor;
        //文字垂直居中于按钮行；点击矩形取满行高，整列可点。
        DrawGdiText(hdc, (LPCWSTR)label, linkRc, kLinkPx, false, drawColor,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP,
                    false /*italic*/, hover /*underline*/, false /*strike*/);
        links.m_rect[bi]   = linkRc;
        links.m_action[bi] = act;
        linkX += linkW + kLinkGap;
    }
}

int FileCardLinkAt(const FileCardLinks& links, POINT pt)
{
    for (int bi = 0; bi < links.m_count && bi < kMaxChatFileActions; ++bi)
    {
        if (::PtInRect(&links.m_rect[bi], pt))
        {
            return bi;
        }
    }
    return -1;
}

} // namespace balloonchatwnd
