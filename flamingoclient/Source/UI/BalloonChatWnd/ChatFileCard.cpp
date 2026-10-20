/**
 *  ChatFileCard.cpp
 *
 *  文件卡片的尺寸计算、绘制与点击判断，见 ChatFileCard.h。2026-10-14 起按聊天卡片美化方案换成新版式
 *  （docs/聊天卡片美化方案.md 3.2～3.7、5.1）：白色底板、文档图标、状态标签、品牌橙进度条与底部一行。
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
#include "ChatCardModel.h"     // ChatCardButtonsOf / ChatCardTypeStyleOf：底部按钮与类型标签
#include "ChatCardPaint.h"     // 卡片底板、状态标签、进度条与底部一行的绘制
#include "ChatCardStyle.h"     // 卡片样式常量
#include "FileBubbleFormat.h"  // FormatFileSize：进度条下方「已传 / 总大小」的写法
#include "ChatCardModel.h"     // ChatCardButtonsOf / ChatCardTypeStyleOf：底部按钮与类型标签
#include "ChatCardPaint.h"     // 卡片底板、状态标签、进度条与底部一行的绘制
#include "ChatCardStyle.h"     // 卡片样式常量
#include "FileBubbleFormat.h"  // FormatFileSize：进度条下方「已传 / 总大小」的写法
#include "ChatFileVisual.h"    // FileTypeColor（与消息记录窗共用同一套配色）
#include "ChatFileAction.h"    // ChatFileActionKind
#include "ChatDirectFileModel.h"   // DirectFileStatusKey：在线直传特有的状态文字（2026-10-04 起）
#include "../BalloonMain/P2PBadge.h"   // ShouldShowP2PBadge：链路是否直连
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonchatwnd {

namespace {

using chatpaint::AddRoundRect;
using chatpaint::DrawGdiText;
using chatpaint::MeasureTextWidth;

// ---- 文件卡片几何（逻辑像素，docs/聊天卡片美化方案.md 3.2～3.7，2026-10-14 起）----

const int kFileDocW = 36;            // 文档图标宽
const int kFileDocH = 44;            // 文档图标高
const int kFileDocFold = 12;         // 文档图标右上角折角的边长
const int kFileDocFoldInner = 8;     // 折角内侧的弯折处距折角两端的长度
const int kFileDocRadius = 4;        // 文档图标其余三个角的圆角半径
const int kFileDocTextPx = 9;        // 文档图标上类型字（扩展名）的字号，加粗
const int kFileDocTextBottom = 4;    // 类型字底边距文档图标底边
const int kFileDocTextH = 14;        // 类型字的行高
const BYTE kFileDocFoldAlpha = 97;   // 折角上白色叠加的不透明度（约 38%）
const COLORREF kFileDocText = RGB(0xFF, 0xFF, 0xFF);     // 类型字颜色
const COLORREF kFileGoneColor = RGB(0xB8, 0xB5, 0xB1);   // 本机没有这份文件时文档图标的灰色
const COLORREF kFileDeletedName = RGB(0x9A, 0x97, 0x92); // 服务器上已删除的文件名颜色（另加删除线）
const int kFileSubtitleRowH = 20;    // 副标题行（大小 + 状态标签 + P2P 徽章）的行高，与状态标签同高
// 头部高：文档图标与「标题 + 副标题行」取较高者，两者在其中垂直居中
const int kFileHeaderH = (kFileDocH > cardstyle::kTitleLineH + kFileSubtitleRowH)
                       ? kFileDocH : (cardstyle::kTitleLineH + kFileSubtitleRowH);
// 进度块：头部与进度条的间距、进度条、进度条与下方一行文字的间距、那一行文字
const int kFileProgressBlockH = cardstyle::kProgressTopGap + cardstyle::kProgressH + cardstyle::kProgressTextGap
                              + cardstyle::kProgressTextLineH;
// 卡片底部：下内边距、分隔线与底部一行。底部一行常驻（左边是类型标签「文件」），没有按钮时也有
const int kFileBottomH = cardstyle::kCardPadBottom + cardstyle::kFooterDividerH + cardstyle::kFooterH;
const int kFullPercent = 100;        // 进度的满值
const wchar_t* const kSizeSeparator = L" / ";   // 进度条下方「已传 / 总大小」中间的分隔
const wchar_t* const kPercentSuffix = L"%";     // 百分比后缀

// 已删除时文档图标上白色垃圾桶的几何（相对文档图标中心与盖子所在的 y）
const float kTrashStroke = 1.6f;     // 线宽
const float kTrashLidHalf = 9.0f;    // 盖子半宽
const float kTrashHandleHalf = 3.0f; // 提手半宽
const float kTrashHandleH = 3.0f;    // 提手高
const float kTrashBodyHalf = 7.0f;   // 桶身上沿半宽
const float kTrashBodyTaper = 1.5f;  // 桶身每侧向下收窄的量
const float kTrashBodyGap = 3.0f;    // 盖子与桶身上沿的间距
const float kTrashBodyH = 12.0f;     // 桶身高
const float kTrashRibInset = 2.0f;   // 竖纹上下各缩进的量
const int kTrashLidTop = 14;         // 盖子距文档图标顶边

// 不透明的 GDI+ 颜色
Gdiplus::Color FileGdip(COLORREF c, BYTE alpha)
{
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

// 文档图标左上角：在头部里垂直居中
POINT FileDocOrigin(const RECT& card)
{
    POINT pt;
    pt.x = card.left + cardstyle::kCardPadH;
    pt.y = card.top + cardstyle::kCardPadTop + (kFileHeaderH - kFileDocH) / 2;
    return pt;
}

// 进度条矩形（卡片显示进度时有效）
RECT FileProgressRect(const RECT& card)
{
    const int top = card.top + cardstyle::kCardPadTop + kFileHeaderH + cardstyle::kProgressTopGap;
    const RECT rc = { card.left + cardstyle::kCardPadH, top, card.right - cardstyle::kCardPadH, top + cardstyle::kProgressH };
    return rc;
}

// 进度（0~100）
int FilePercent(const ChatMessage& m)
{
    int pct = m.fileProgress;
    if (pct < 0)
        pct = 0;
    if (pct > kFullPercent)
        pct = kFullPercent;
    return pct;
}

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
    //是否带进度块随状态变化；头部与卡片底部（含常驻的底部一行）固定
    int h = cardstyle::kCardPadTop + kFileHeaderH;
    if (FileCardShowsProgress(m.fileState, m.fileDirect))
    {
        h += kFileProgressBlockH;
    }
    h += kFileBottomH;
    return h;
}

CString FileCardLinkLabel(const ChatMessage& m, int act)
{
    switch (act)
    {
    //完成态：用关联程序打开本地文件
    case kChatFileOpen:
        return TR(L"打开文件");
    //完成态：在资源管理器里定位本地文件
    case kChatFileOpenFolder:
        return TR(L"打开所在文件夹");
    //完成态：把本地文件另存到别处
    case kChatFileSaveAs:
        return TR(L"另存为");
    //传输中：取消这次上传或下载
    case kChatFileCancel:
        return TR(L"取消");
    //待接收：单聊里对方发来的写「接收」；自己发出的、群聊与聊天记录里的文件谈不上接收，
    //只是把它从服务器取回本地，写「下载」
    case kChatFileAccept:
        return FileCardSkipsReject(m) ? TR(L"下载") : TR(L"接收");
    //单聊待接收：拒绝接收
    case kChatFileReject:
        return TR(L"拒绝");
    //在线直传等待对方接收（发送方）：改为上传服务器，按离线文件发送
    case kChatFileSendOffline:
        return TR(L"发送离线文件");
    //失败、已取消、已拒绝：重新发送或重新下载（\x21BB 为循环箭头），按手头数据判定（见 FileCardCanResend）
    case kChatFileRetry:
    default:
        return FileCardCanResend(m) ? TR(L"\x21BB 重新发送") : TR(L"\x21BB 重新下载");
    }
}

int FileCardActionLevel(int act)
{
    switch (act)
    {
    //接收 / 下载、发送离线文件：改变状态的操作
    case kChatFileAccept:
    case kChatFileSendOffline:
        return kCardButtonPrimary;
    //打开文件、重新发送 / 重新下载：查看类的操作
    case kChatFileOpen:
    case kChatFileRetry:
        return kCardButtonView;
    //打开所在文件夹、另存为、取消、拒绝：次要操作
    default:
        return kCardButtonSecondary;
    }
}

std::vector<ChatCardButton> FileCardButtons(const ChatMessage& m)
{
    //FileButtonActions 给出的顺序是最常用的在前，ChatCardArrangeButtons 把它重排成显示顺序，让主要按钮落在最右
    //（如「拒绝、接收」「另存为、打开所在文件夹、打开文件」「取消、发送离线文件」）
    int acts[kMaxChatFileActions];
    const int count = FileButtonActions(m.fileState, FileCardSkipsReject(m), acts);
    std::vector<ChatCardButton> buttons;
    for (int i = 0; i < count; ++i)
        buttons.push_back(ChatCardButton(FileCardLinkLabel(m, acts[i]), FileCardActionLevel(acts[i]), true, acts[i]));
    ChatCardArrangeButtons(buttons);
    return buttons;
}

void FileCardStatus(const ChatMessage& m, CString& text, int& tone)
{
    const int st = m.fileState;
    tone = kCardChipEnded;
    switch (st)
    {
    //上传中
    case kFileUploading:
        text = TR(L"传送中");
        tone = kCardChipPending;
        break;
    //下载中
    case kFileDownloading:
        text = TR(L"下载中");
        tone = kCardChipPending;
        break;
    //完成
    case kFileNormal:
        text = m.fromMe ? TR(L"传送完成") : TR(L"已完成");
        tone = kCardChipLive;
        break;
    //传输失败
    case kFileFailed:
        text = TR(L"传输失败");
        tone = kCardChipBad;
        break;
    //已取消
    case kFileCancelled:
        text = TR(L"已取消");
        break;
    //待接收：群聊或聊天记录里的文件没有接收流程，只说明本地还没有这份文件；单聊要用户自己决定收不收
    case kFilePendingAccept:
        if (m.fileDirectDownload)
        {
            text = TR(L"未下载");
        }
        else
        {
            text = TR(L"等待接收");
            tone = kCardChipPending;
        }
        break;
    //已拒绝接收
    case kFileRejected:
        text = TR(L"已拒绝接收");
        break;
    //已删除；在线直传的等待与作废状态由下面的直传写法覆盖
    default:
        text = TR(L"文件已删除，无法下载");
        break;
    }

    //在线直传特有的状态文字（等待对方接收、作废原因、历史里本机没有文件、接收中），覆盖上面按普通文件算出的文字；
    //接收中不带百分比，百分比画在进度条下面那一行
    const wchar_t* directKey = DirectFileStatusKey(st, m.fileDirectEnd, m.fromMe, m.fileDirect);
    if (directKey != NULL)
    {
        if (st == kFileDownloading)
        {
            text = TR(L"接收中");
            tone = kCardChipPending;
        }
        else
        {
            text = TR(directKey);
            tone = (st == kFileDirectWaiting) ? kCardChipPending : kCardChipEnded;
        }
    }

    //本端发出的文件：只要对方有过动作，状态就改说对方那边的进展 —— 自己传完了不等于对方收了
    if (m.fromMe && m.filePeerState != 0)
    {
        if (m.filePeerState == kFilePeerReceiving)
        {
            text = TR(L"对方正在接收");
            tone = kCardChipPending;
        }
        else if (m.filePeerState == kFilePeerReceived)
        {
            text = TR(L"对方已接收");
            tone = kCardChipLive;
        }
        else if (m.filePeerState == kFilePeerRejected)
        {
            text = TR(L"对方已拒绝接收");
            tone = kCardChipEnded;
        }
    }
}

void PaintFileCardShapes(Gdiplus::Graphics& g, const RECT& card, const ChatMessage& m)
{
    const int  st      = m.fileState;
    //本机没有这份文件：普通文件是服务器上已删除（画垃圾桶），在线直传是服务器上本来就没有（只把图标置灰）
    const bool gone    = (st == kFileDeleted);
    const bool deleted = gone && !m.fileDirect;
    cardpaint::PaintCardFrame(g, card);

    // ---- 文档图标：右上角折起的纸张，正常用类型色，本机没有文件用灰 ----
    const POINT doc = FileDocOrigin(card);
    const Gdiplus::REAL x = (Gdiplus::REAL)doc.x;
    const Gdiplus::REAL y = (Gdiplus::REAL)doc.y;
    const Gdiplus::REAL w = (Gdiplus::REAL)kFileDocW;
    const Gdiplus::REAL h = (Gdiplus::REAL)kFileDocH;
    const Gdiplus::REAL fold = (Gdiplus::REAL)kFileDocFold;
    const Gdiplus::REAL d = (Gdiplus::REAL)(kFileDocRadius * 2);
    {
        Gdiplus::GraphicsPath sheet;
        sheet.AddLine(x + d / 2, y, x + w - fold, y);
        sheet.AddLine(x + w - fold, y, x + w, y + fold);
        sheet.AddLine(x + w, y + fold, x + w, y + h - d / 2);
        sheet.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
        sheet.AddLine(x + w - d / 2, y + h, x + d / 2, y + h);
        sheet.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
        sheet.AddLine(x, y + h - d / 2, x, y + d / 2);
        sheet.AddArc(x, y, d, d, 180.0f, 90.0f);
        sheet.CloseFigure();
        Gdiplus::SolidBrush sheetBrush(FileGdip(gone ? kFileGoneColor : FileTypeColor(m.fileType), 255));
        g.FillPath(&sheetBrush, &sheet);

        const Gdiplus::REAL inner = (Gdiplus::REAL)kFileDocFoldInner;
        Gdiplus::PointF flap[4] =
        {
            Gdiplus::PointF(x + w - fold, y),
            Gdiplus::PointF(x + w, y + fold),
            Gdiplus::PointF(x + w - fold + inner / 2, y + fold),
            Gdiplus::PointF(x + w - fold, y + inner),
        };
        Gdiplus::SolidBrush flapBrush(FileGdip(kFileDocText, kFileDocFoldAlpha));
        g.FillPolygon(&flapBrush, flap, 4);
    }

    // 已删除：文档图标上画一个白色垃圾桶轮廓（盖 + 提手 + 桶身 + 竖纹）
    if (deleted)
    {
        Gdiplus::Pen  trash(FileGdip(kFileDocText, 255), kTrashStroke);
        const Gdiplus::REAL cx = x + w / 2;
        const Gdiplus::REAL ty = y + (Gdiplus::REAL)kTrashLidTop;
        g.DrawLine(&trash, cx - kTrashLidHalf, ty, cx + kTrashLidHalf, ty);
        g.DrawLine(&trash, cx - kTrashHandleHalf, ty - kTrashHandleH, cx + kTrashHandleHalf, ty - kTrashHandleH);
        g.DrawLine(&trash, cx - kTrashHandleHalf, ty - kTrashHandleH, cx - kTrashHandleHalf, ty);
        g.DrawLine(&trash, cx + kTrashHandleHalf, ty - kTrashHandleH, cx + kTrashHandleHalf, ty);
        const Gdiplus::REAL bx = cx - kTrashBodyHalf;
        const Gdiplus::REAL bw = kTrashBodyHalf * 2;
        const Gdiplus::REAL byTop = ty + kTrashBodyGap;
        g.DrawLine(&trash, bx, byTop, bx + kTrashBodyTaper, byTop + kTrashBodyH);
        g.DrawLine(&trash, bx + bw, byTop, bx + bw - kTrashBodyTaper, byTop + kTrashBodyH);
        g.DrawLine(&trash, bx + kTrashBodyTaper, byTop + kTrashBodyH, bx + bw - kTrashBodyTaper, byTop + kTrashBodyH);
        g.DrawLine(&trash, cx - kTrashHandleHalf, byTop + kTrashRibInset, cx - kTrashHandleHalf, byTop + kTrashBodyH - kTrashRibInset);
        g.DrawLine(&trash, cx + kTrashHandleHalf, byTop + kTrashRibInset, cx + kTrashHandleHalf, byTop + kTrashBodyH - kTrashRibInset);
    }

    // ---- 进度条（上传 / 下载 / 失败 / 已取消）：一律品牌橙（方案 3.7）----
    if (FileCardShowsProgress(st, m.fileDirect))
    {
        cardpaint::PaintCardProgress(g, FileProgressRect(card), FilePercent(m));
    }
}

void PaintFileCardText(HDC hdc, Gdiplus::Graphics& g, const RECT& card, const ChatMessage& m,
                       int hoverIdx, FileCardLinks& links)
{
    const int  st      = m.fileState;
    //含义同 PaintFileCardShapes：在线直传在本机没有文件时不画垃圾桶、不划删除线，只说明服务器未保存
    const bool gone    = (st == kFileDeleted);
    const bool deleted = gone && !m.fileDirect;
    const UINT kOneLine = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    const POINT doc = FileDocOrigin(card);
    const int right = card.right - cardstyle::kCardPadH;

    // 文档图标下部的类型字（扩展名大写；已删除处是垃圾桶，不画）
    if (!deleted)
    {
        CString typeUpper = m.fileType;
        typeUpper.MakeUpper();
        const RECT ftRc = { doc.x, doc.y + kFileDocH - kFileDocTextBottom - kFileDocTextH,
                            doc.x + kFileDocW, doc.y + kFileDocH - kFileDocTextBottom };
        DrawGdiText(hdc, (LPCWSTR)typeUpper, ftRc, kFileDocTextPx, true, kFileDocText,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX);
    }

    // 标题：文件名，加粗；服务器上已删除的灰色加删除线
    const int textLeft = doc.x + kFileDocW + cardstyle::kTileTextGap;
    const int titleTop = card.top + cardstyle::kCardPadTop + (kFileHeaderH - cardstyle::kTitleLineH - kFileSubtitleRowH) / 2;
    const RECT nameRc = { textLeft, titleTop, right, titleTop + cardstyle::kTitleLineH };
    DrawGdiText(hdc, (LPCWSTR)m.fileName, nameRc, cardstyle::kTitlePx, true, deleted ? kFileDeletedName : cardstyle::kTitleColor,
                kOneLine | DT_END_ELLIPSIS, false, false, deleted /*strike*/);

    // 副标题行：大小、状态标签、「P2P」徽章（在线直传经直连链路传输时，2026-10-04 起）
    const int rowTop = nameRc.bottom;
    const int rowBottom = rowTop + kFileSubtitleRowH;
    const RECT sizeRc = { textLeft, rowTop, right, rowBottom };
    DrawGdiText(hdc, (LPCWSTR)m.fileSize, sizeRc, cardstyle::kSubtitlePx, false, cardstyle::kSubtitleColor, kOneLine);
    int x = textLeft;
    if (!m.fileSize.IsEmpty())
        x += MeasureTextWidth(hdc, (LPCWSTR)m.fileSize, cardstyle::kSubtitlePx) + cardstyle::kMetaGap;
    CString statusText;
    int statusTone = kCardChipNone;
    FileCardStatus(m, statusText, statusTone);
    const int chipRight = cardpaint::PaintCardChip(hdc, g, x, rowTop, rowBottom, statusText, statusTone);
    if (FileCardShowsP2PBadge(st, m.fileDirect, balloonmain::ShouldShowP2PBadge(m.fileLinkPath)))
    {
        chatpaint::DrawP2PBadgeInRow(hdc, chipRight + chatpaint::kP2PBadgeGapPx, rowTop, rowBottom, right);
    }

    // 进度条下方一行：左边「已传 / 总大小」，右边百分比
    if (FileCardShowsProgress(st, m.fileDirect))
    {
        const RECT bar = FileProgressRect(card);
        const int lineTop = bar.bottom + cardstyle::kProgressTextGap;
        const int pct = FilePercent(m);
        CString pctText;
        pctText.Format(L"%d", pct);
        pctText += kPercentSuffix;
        const RECT pctRc = { bar.left, lineTop, bar.right, lineTop + cardstyle::kProgressTextLineH };
        DrawGdiText(hdc, (LPCWSTR)pctText, pctRc, cardstyle::kSubtitlePx, false, cardstyle::kSubtitleColor,
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (m.fileSizeBytes > 0)
        {
            CString bytesText = FormatFileSize(m.fileSizeBytes * pct / kFullPercent).c_str();
            bytesText += kSizeSeparator;
            bytesText += FormatFileSize(m.fileSizeBytes).c_str();
            RECT bytesRc = pctRc;
            bytesRc.right -= MeasureTextWidth(hdc, (LPCWSTR)pctText, cardstyle::kSubtitlePx) + cardstyle::kMetaGap;
            DrawGdiText(hdc, (LPCWSTR)bytesText, bytesRc, cardstyle::kSubtitlePx, false, cardstyle::kSubtitleColor,
                        kOneLine | DT_END_ELLIPSIS);
        }
    }

    // 底部一行：左边类型标签「文件」，右边按钮；按钮的矩形与动作写进 links 供点击判断与悬停反馈
    links = FileCardLinks();
    const std::vector<ChatCardButton> buttons = ChatCardButtonsOf(m);
    const RECT footer = { card.left, card.bottom - cardstyle::kFooterH, card.right, card.bottom };
    const cardpaint::CardFooterLayout lay =
        cardpaint::PaintCardFooter(hdc, g, footer, ChatCardTypeStyleOf(m), ChatCardTypeLabel(m), buttons, hoverIdx);
    for (size_t i = 0; i < buttons.size() && i < lay.m_buttonRects.size() && links.m_count < kMaxChatFileActions; ++i)
    {
        links.m_rect[links.m_count] = lay.m_buttonRects[i];
        links.m_action[links.m_count] = buttons[i].m_action;
        ++links.m_count;
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
