/**
 *  ChatFileCardTests.cpp
 *
 *  文件卡片（聊天窗文件气泡与聊天记录查看窗共用的那张卡片）的测试。
 *
 *    FCSNAP 隐藏用例（标签 [.][filecardsnap]）：用聊天气泡的绘制代码把各状态的文件气泡画到
 *           一张位图上，原始像素写到环境变量 FLAMINGO_FILECARD_DUMP 指定的文件，并在同名
 *           加 ".png" 的路径另存一张 PNG 供人工查看。把文件卡片的绘制从 ChatBubble 抽成共用
 *           组件前后各跑一次、逐字节比较两份原始像素，就能确认外观没有变化。用法：
 *               set FLAMINGO_FILECARD_DUMP=E:\flamingo\tmp\filecard_before.bin
 *               FlamingoUnit.exe "[filecardsnap]"
 *
 *    FWD-C18c 共用文件卡片（ChatFileCard）：各状态的卡片高度，完成态与未下载态的操作链接个数、
 *             顺序与位置，以及点中判断（链接中心返回对应下标，卡片头部返回 -1）。
 *
 *    DFC9  在线直传等待接收的卡片：两个链接依次为「发送离线文件」「取消」，文字取自 FileCardLinkLabel。
 *    DFC10 「P2P」徽章与加粗的状态文字之间保持设计间距（kP2PBadgeGapPx，允许 1 像素抗锯齿误差）。
 *    DFC13 发送方取消的在线直传文件卡片：不画进度条（高度只有头部区与按钮行），只有「重新发送」一个链接。
 *          2026-10-05 联调发现状态文字按常规字重量宽、按加粗字重绘制，字越多徽章越贴近文字。
 *
 *  用 Catch2 v3（amalgamated 形态）。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <vector>
#include <gdiplus.h>

#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatFileStateModel.h"
#include "../UI/BalloonChatWnd/ChatFileCard.h"
#include "../UI/BalloonChatWnd/ChatFileAction.h"
#include "../UI/BalloonChatWnd/ChatPaintUtil.h"
#include "../UI/BalloonMain/P2PBadge.h"
#include "../net/Protocol.h"   // folder_state_canceled：发送方取消直传时记下的原因

using namespace balloonchatwnd;

namespace {

// 截图用的列表尺寸（像素）：宽度放得下文件气泡与两侧头像，高度放得下全部状态的气泡
const int kSnapListWidth  = 520;
const int kSnapListHeight = 1900;
// 各状态下示例文件的字节数与进度（百分比）
const long long kSnapFileBytes   = 4404019;
const int       kSnapDownloadPct = 37;
const int       kSnapUploadPct   = 45;

// 在作用域内启动 GDI+，离开时关闭。气泡底色、图标与进度条都用 GDI+ 绘制。
class ScopedGdiplusForCard
{
public:
    ScopedGdiplusForCard()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    ~ScopedGdiplusForCard()
    {
        if (m_ok)
        {
            Gdiplus::GdiplusShutdown(m_token);
        }
    }

    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   //GdiplusStartup 返回的令牌
    bool      m_ok;      //是否启动成功
};

// 在 GDI+ 已注册的图像编码器里找 PNG 编码器的 CLSID。
bool FindPngEncoderForCard(CLSID& out)
{
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0)
    {
        return false;
    }
    std::vector<BYTE> buf(bytes);
    Gdiplus::ImageCodecInfo* codecs = (Gdiplus::ImageCodecInfo*)&buf[0];
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok)
    {
        return false;
    }
    for (UINT i = 0; i < count; ++i)
    {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
        {
            out = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

// 造一条指定状态的文件气泡数据。
//   fromMe：是否本端发出；state：文件态；progress：进度百分比；direct：是否群聊的直接下载
ChatMessage MakeSnapFile(bool fromMe, int state, int progress, bool direct)
{
    ChatMessage m = MakeIncomingFileMessage(fromMe, _T("季度报告-最终版.xlsx"), kSnapFileBytes,
                                            0, "1", "md5", state);
    m.fileProgress = progress;
    m.fileDirectDownload = direct;
    if (state == kFileNormal)
    {
        m.fileLocalPath = _T("C:\\down\\季度报告-最终版.xlsx");
    }
    return m;
}

} // anonymous namespace

TEST_CASE("FCSNAP 各状态文件气泡的像素截图（重构前后比对用）", "[.][filecardsnap]")
{
    char path[MAX_PATH] = { 0 };
    if (::GetEnvironmentVariableA("FLAMINGO_FILECARD_DUMP", path, MAX_PATH) == 0)
    {
        SUCCEED("FLAMINGO_FILECARD_DUMP not set, snapshot skipped");
        return;
    }

    ScopedGdiplusForCard gdiplus;
    REQUIRE(gdiplus.Ok());

    ChatMessageList list;
    // 单聊：对方发来待接收（接收 / 拒绝）
    list.AppendMessage(MakeSnapFile(false, kFilePendingAccept, 0, false));
    // 群聊：对方发来未下载（只有下载）
    list.AppendMessage(MakeSnapFile(false, kFilePendingAccept, 0, true));
    // 下载中
    list.AppendMessage(MakeSnapFile(false, kFileDownloading, kSnapDownloadPct, false));
    // 对方发来、已完成
    list.AppendMessage(MakeSnapFile(false, kFileNormal, 0, false));
    // 本端发出、已完成且对方已接收
    ChatMessage sentDone = MakeSnapFile(true, kFileNormal, 0, false);
    sentDone.filePeerState = kFilePeerReceived;
    list.AppendMessage(sentDone);
    // 本端发出、上传中
    list.AppendMessage(MakeSnapFile(true, kFileUploading, kSnapUploadPct, false));
    // 传输失败
    list.AppendMessage(MakeSnapFile(false, kFileFailed, kSnapDownloadPct, false));
    // 已取消
    list.AppendMessage(MakeSnapFile(false, kFileCancelled, kSnapDownloadPct, false));
    // 已拒绝接收
    list.AppendMessage(MakeSnapFile(false, kFileRejected, 0, false));
    // 已删除 / 失效
    list.AppendMessage(MakeSnapFile(false, kFileDeleted, 0, false));

    RECT all = { 0, 0, kSnapListWidth, kSnapListHeight };
    list.ForceLayout(all);

    HDC screenDC = ::GetDC(NULL);
    REQUIRE(screenDC != NULL);
    HDC memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(NULL, screenDC);
    REQUIRE(memDC != NULL);

    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = kSnapListWidth;
    bi.bmiHeader.biHeight      = -kSnapListHeight;   // 负高 = 自上而下
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    REQUIRE(bmp != NULL);
    REQUIRE(bits != NULL);
    HGDIOBJ oldBmp = ::SelectObject(memDC, bmp);

    HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    ::FillRect(memDC, &all, white);
    ::DeleteObject(white);
    list.OnPaint(memDC, all);
    ::GdiFlush();

    // 原始像素：每像素 4 字节（BGRA），逐行自上而下
    const size_t pixelBytes = (size_t)kSnapListWidth * (size_t)kSnapListHeight * 4;
    FILE* fp = NULL;
    REQUIRE(fopen_s(&fp, path, "wb") == 0);
    REQUIRE(fp != NULL);
    CHECK(fwrite(bits, 1, pixelBytes, fp) == pixelBytes);
    fclose(fp);

    // 同时另存一张 PNG 供人工查看
    CLSID png;
    if (FindPngEncoderForCard(png))
    {
        std::string pngPath = std::string(path) + ".png";
        Gdiplus::Bitmap image(bmp, NULL);
        const std::wstring wide(CA2W(pngPath.c_str()));
        CHECK(image.Save(wide.c_str(), &png, NULL) == Gdiplus::Ok);
    }

    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(bmp);
    ::DeleteDC(memDC);
}

namespace {

// 卡片各部分的高度（逻辑像素），与 ChatFileCard.cpp 的几何常量对应：头部区 60，进度条 5，
// 按钮行 = 上间距 10 + 按钮高 34 + 下边距 12
const int kCardHeaderH   = 60;
const int kCardProgressH = 5;
const int kCardButtonRow = 10 + 34 + 12;

// 卡片在画布上的位置（像素）与画布尺寸
const int kCardLeft   = 10;
const int kCardTop    = 20;
const int kCanvasW    = 400;
const int kCanvasH    = 300;

} // anonymous namespace

TEST_CASE("FWD-C18c 共用文件卡片：各状态的高度、操作链接与点中判断", "[forward][filecard]")
{
    // 高度：头部区固定，有进度的状态加进度条，有操作的状态加按钮行
    CHECK(FileCardHeight(MakeSnapFile(false, kFilePendingAccept, 0, true)) == kCardHeaderH + kCardButtonRow);
    CHECK(FileCardHeight(MakeSnapFile(false, kFileDownloading, kSnapDownloadPct, true))
          == kCardHeaderH + kCardProgressH + kCardButtonRow);
    CHECK(FileCardHeight(MakeSnapFile(false, kFileNormal, 0, true)) == kCardHeaderH + kCardButtonRow);
    CHECK(FileCardHeight(MakeSnapFile(false, kFileFailed, kSnapDownloadPct, true))
          == kCardHeaderH + kCardProgressH + kCardButtonRow);
    CHECK(FileCardHeight(MakeSnapFile(false, kFileDeleted, 0, true)) == kCardHeaderH);

    // 待接收：聊天记录 / 群聊里的文件只有「下载」，单聊里对方发来的有「接收 / 拒绝」
    CHECK(FileCardSkipsReject(MakeSnapFile(false, kFilePendingAccept, 0, true)));
    CHECK_FALSE(FileCardSkipsReject(MakeSnapFile(false, kFilePendingAccept, 0, false)));

    ScopedGdiplusForCard gdiplus;
    REQUIRE(gdiplus.Ok());
    HDC screenDC = ::GetDC(NULL);
    REQUIRE(screenDC != NULL);
    HDC memDC = ::CreateCompatibleDC(screenDC);
    HBITMAP bmp = ::CreateCompatibleBitmap(screenDC, kCanvasW, kCanvasH);
    ::ReleaseDC(NULL, screenDC);
    REQUIRE(memDC != NULL);
    REQUIRE(bmp != NULL);
    HGDIOBJ oldBmp = ::SelectObject(memDC, bmp);

    // 完成态：三个链接，依次为打开文件 / 打开所在文件夹 / 另存为，自左向右排在卡片内
    const ChatMessage done = MakeSnapFile(false, kFileNormal, 0, true);
    const RECT card = { kCardLeft, kCardTop, kCardLeft + kFileCardWidth, kCardTop + FileCardHeight(done) };
    FileCardLinks links;
    {
        Gdiplus::Graphics g(memDC);
        PaintFileCardText(memDC, g, card, done, -1, links);
    }
    REQUIRE(links.m_count == 3);
    CHECK(links.m_action[0] == kChatFileOpen);
    CHECK(links.m_action[1] == kChatFileOpenFolder);
    CHECK(links.m_action[2] == kChatFileSaveAs);
    for (int i = 0; i < links.m_count; ++i)
    {
        INFO("link " << i);
        CHECK(links.m_rect[i].left >= card.left);
        CHECK(links.m_rect[i].right <= card.right);
        CHECK(links.m_rect[i].top >= card.top);
        CHECK(links.m_rect[i].bottom <= card.bottom);
        if (i > 0)
        {
            CHECK(links.m_rect[i].left > links.m_rect[i - 1].right);
        }
    }

    // 点中判断：链接中心点返回对应下标，卡片头部（文件名处）不是链接
    POINT mid = { (links.m_rect[1].left + links.m_rect[1].right) / 2,
                  (links.m_rect[1].top + links.m_rect[1].bottom) / 2 };
    CHECK(FileCardLinkAt(links, mid) == 1);
    POINT header = { card.left + kFileCardWidth / 2, card.top + kCardHeaderH / 2 };
    CHECK(FileCardLinkAt(links, header) == -1);

    // 未下载（聊天记录里）：只有一个「下载」
    const ChatMessage pending = MakeSnapFile(false, kFilePendingAccept, 0, true);
    FileCardLinks pendingLinks;
    {
        Gdiplus::Graphics g(memDC);
        PaintFileCardText(memDC, g, card, pending, -1, pendingLinks);
    }
    REQUIRE(pendingLinks.m_count == 1);
    CHECK(pendingLinks.m_action[0] == kChatFileAccept);

    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(bmp);
    ::DeleteDC(memDC);
}

namespace {

// 在白底画布上画一张卡片的文字部分，读回 32 位自上而下的像素（每像素 BGRA 四字节）
std::vector<unsigned char> PaintCardPixels(const ChatMessage& m, const RECT& card, FileCardLinks& links)
{
    std::vector<unsigned char> pixels;
    HDC screenDC = ::GetDC(NULL);
    if (screenDC == NULL)
    {
        return pixels;
    }
    HDC memDC = ::CreateCompatibleDC(screenDC);
    HBITMAP bmp = ::CreateCompatibleBitmap(screenDC, kCanvasW, kCanvasH);
    ::ReleaseDC(NULL, screenDC);
    if (memDC == NULL || bmp == NULL)
    {
        if (bmp != NULL)
        {
            ::DeleteObject(bmp);
        }
        if (memDC != NULL)
        {
            ::DeleteDC(memDC);
        }
        return pixels;
    }
    HGDIOBJ oldBmp = ::SelectObject(memDC, bmp);
    RECT all = { 0, 0, kCanvasW, kCanvasH };
    ::FillRect(memDC, &all, (HBRUSH)::GetStockObject(WHITE_BRUSH));
    {
        Gdiplus::Graphics g(memDC);
        PaintFileCardText(memDC, g, card, m, -1, links);
    }
    ::SelectObject(memDC, oldBmp);

    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = kCanvasW;
    bi.bmiHeader.biHeight      = -kCanvasH;   // 负值表示自上而下
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    pixels.resize((size_t)kCanvasW * kCanvasH * 4);
    if (::GetDIBits(memDC, bmp, 0, kCanvasH, &pixels[0], &bi, DIB_RGB_COLORS) != kCanvasH)
    {
        pixels.clear();
    }
    ::DeleteObject(bmp);
    ::DeleteDC(memDC);
    return pixels;
}

// 白底上「有笔画」的判定阈值：三个分量都不低于它就当作背景（容下 ClearType 在空白处留下的极淡杂色）
const int kInkThreshold = 245;

// 某一列在 [top, bottom] 行之间有没有笔画
bool ColumnHasInk(const std::vector<unsigned char>& px, int x, int top, int bottom)
{
    for (int y = top; y <= bottom; ++y)
    {
        const unsigned char* p = &px[((size_t)y * kCanvasW + x) * 4];
        if (p[0] < kInkThreshold || p[1] < kInkThreshold || p[2] < kInkThreshold)
        {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

TEST_CASE("DFC9 在线直传等待接收的卡片：链接依次为「发送离线文件」「取消」", "[directfilechat][filecard]")
{
    ScopedGdiplusForCard gdiplus;
    REQUIRE(gdiplus.Ok());

    ChatMessage waiting = MakeSnapFile(true, kFileDirectWaiting, 0, false);
    waiting.fileDirect = true;
    const RECT card = { kCardLeft, kCardTop, kCardLeft + kFileCardWidth, kCardTop + FileCardHeight(waiting) };
    FileCardLinks links;
    const std::vector<unsigned char> pixels = PaintCardPixels(waiting, card, links);
    REQUIRE_FALSE(pixels.empty());
    REQUIRE(links.m_count == 2);
    CHECK(links.m_action[0] == kChatFileSendOffline);
    CHECK(links.m_action[1] == kChatFileCancel);
    CHECK(FileCardLinkLabel(waiting, kChatFileSendOffline) == CString(L"发送离线文件"));
    CHECK(FileCardLinkLabel(waiting, kChatFileCancel) == CString(L"取消"));

    // 其余动作的文字保持原样：收到的失败文件重试是「重新下载」
    ChatMessage failedIn = MakeSnapFile(false, kFileFailed, kSnapDownloadPct, false);
    CHECK(FileCardLinkLabel(failedIn, kChatFileRetry) == CString(L"\x21BB 重新下载"));
    CHECK(FileCardLinkLabel(failedIn, kChatFileAccept) == CString(L"接收"));
    CHECK(FileCardLinkLabel(failedIn, kChatFileReject) == CString(L"拒绝"));
    CHECK(FileCardLinkLabel(failedIn, kChatFileOpen) == CString(L"打开文件"));
}

TEST_CASE("DFC10 「P2P」徽章与加粗的状态文字之间保持设计间距", "[directfilechat][filecard]")
{
    ScopedGdiplusForCard gdiplus;
    REQUIRE(gdiplus.Ok());

    // 本端发出、对方已接收：状态文字「对方已接收」加粗，五个字，常规字重量宽时误差最明显
    ChatMessage sent = MakeSnapFile(true, kFileNormal, 0, false);
    sent.fileDirect    = true;
    sent.fileLinkPath  = 1;   // 局域网直连
    sent.filePeerState = kFilePeerReceived;
    const RECT card = { kCardLeft, kCardTop, kCardLeft + kFileCardWidth, kCardTop + FileCardHeight(sent) };
    FileCardLinks links;
    const std::vector<unsigned char> px = PaintCardPixels(sent, card, links);
    REQUIRE_FALSE(px.empty());

    // 徽章底色的像素圈出徽章所在的行与最左一列
    const BYTE kBadgeR = GetRValue(balloonmain::kP2PBadgeBgColor);
    const BYTE kBadgeG = GetGValue(balloonmain::kP2PBadgeBgColor);
    const BYTE kBadgeB = GetBValue(balloonmain::kP2PBadgeBgColor);
    int minX = kCanvasW;
    int minY = kCanvasH;
    int maxY = -1;
    for (int y = 0; y < kCanvasH; ++y)
    {
        for (int x = 0; x < kCanvasW; ++x)
        {
            const unsigned char* p = &px[((size_t)y * kCanvasW + x) * 4];
            if (p[2] == kBadgeR && p[1] == kBadgeG && p[0] == kBadgeB)
            {
                minX = (x < minX) ? x : minX;
                minY = (y < minY) ? y : minY;
                maxY = (y > maxY) ? y : maxY;
            }
        }
    }
    REQUIRE(maxY >= 0);

    // 从底色最左列向左越过徽章圆角处的过渡色，得到徽章左边；再往左数连续的空白列，就是徽章与文字的间距
    int badgeLeft = minX;
    while (badgeLeft - 1 > card.left && ColumnHasInk(px, badgeLeft - 1, minY, maxY))
    {
        --badgeLeft;
    }
    int gap = 0;
    int x = badgeLeft - 1;
    while (x > card.left && !ColumnHasInk(px, x, minY, maxY))
    {
        ++gap;
        --x;
    }
    REQUIRE(x > card.left);   // 左边确实有状态文字
    INFO("gap between status text and badge: " << gap << " px");
    CHECK(gap >= chatpaint::kP2PBadgeGapPx - 1);
    CHECK(gap <= chatpaint::kP2PBadgeGapPx + 1);
}

TEST_CASE("DFC13 发送方取消的直传文件卡片：没有进度条，只有「重新发送」", "[directfilechat][filecard]")
{
    ScopedGdiplusForCard gdiplus;
    REQUIRE(gdiplus.Ok());

    //本端发出、等对方接收时自己取消了：已取消、仍是直传、记下取消原因
    ChatMessage canceled = MakeFileMessage(true, _T("季度报告-最终版.xlsx"), kSnapFileBytes,
                                           _T("C:\\data\\季度报告-最终版.xlsx"), "cmid-dfc13", kFileCancelled);
    canceled.fileDirect = true;
    canceled.fileDirectEnd = folder_state_canceled;
    //高度：头部区 + 按钮行，没有进度条那一截；普通文件的已取消照旧带进度条
    CHECK(FileCardHeight(canceled) == kCardHeaderH + kCardButtonRow);
    ChatMessage normal = canceled;
    normal.fileDirect = false;
    normal.fileDirectEnd = 0;
    CHECK(FileCardHeight(normal) == kCardHeaderH + kCardProgressH + kCardButtonRow);

    //只有一个链接：重新发送（本端刚发出的那笔，带 clientMsgId 与本地源文件）
    const RECT card = { kCardLeft, kCardTop, kCardLeft + kFileCardWidth, kCardTop + FileCardHeight(canceled) };
    FileCardLinks links;
    const std::vector<unsigned char> pixels = PaintCardPixels(canceled, card, links);
    REQUIRE_FALSE(pixels.empty());
    REQUIRE(links.m_count == 1);
    CHECK(links.m_action[0] == kChatFileRetry);
    CHECK(FileCardLinkLabel(canceled, kChatFileRetry) == CString(L"\x21BB 重新发送"));
    //链接行不留进度条的位置：比普通文件已取消的那张卡片正好高出一个进度条的高度
    const RECT normalCard = { kCardLeft, kCardTop, kCardLeft + kFileCardWidth, kCardTop + FileCardHeight(normal) };
    FileCardLinks normalLinks;
    const std::vector<unsigned char> normalPixels = PaintCardPixels(normal, normalCard, normalLinks);
    REQUIRE_FALSE(normalPixels.empty());
    REQUIRE(normalLinks.m_count == 1);
    CHECK(normalLinks.m_rect[0].top - links.m_rect[0].top == kCardProgressH);
}
