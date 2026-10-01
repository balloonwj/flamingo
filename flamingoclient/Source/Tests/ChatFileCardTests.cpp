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
