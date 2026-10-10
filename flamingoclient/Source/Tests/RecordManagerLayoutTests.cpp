/**
 *  录制管理窗口布局的单元测试（标签 [recordmgr-layout]），RecordManagerLayoutTests.cpp
 *
 *  覆盖 FlamingoMedia/Record/RecordManagerLayout.h（docs/屏幕共享与录制方案.md 第 11.6 节 F9 的 RMW1）：
 *    RMW1 窗口缩到最小尺寸（RecordManagerMinWidth × kRecordMgrMinH）时，工具栏五个按钮与空间充裕时一样宽、
 *         没有被压扁，按钮与「共 N 个」不重叠、都在内边距以内；列表仍至少能显示三行。
 *    CAPRMW1 语音字幕可用、显示「编辑字幕」时（2026-10-06），最小宽度下六个按钮同样不被压扁、互不重叠，「共 N 个」放得下；
 *         「编辑字幕」默认隐藏。
 *    RMW3 第 27 期 27d 的「本机 / 云端」页签：默认显示本机页；切到云端页后，最小宽度下云端工具栏五个按钮不被压扁、
 *         用量条与用量文字放得下，云端列表至少三行；状态列的宽度与读写。
 *    RMSNAP（隐藏，[.rmsnap]）把云端页画进内存位图存成 PNG（目录取环境变量 FLAMINGO_SNAP_DIR），供人工核对界面。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <objidl.h>    //gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>
#include <string>
#include <vector>

#include "../../FlamingoMedia/Record/CloudRecordManagerModel.h"
#include "../../FlamingoMedia/Record/RecordManagerLayout.h"
#include "../net/Protocol.h"
#include "../../FlamingoMedia/Record/RecordManagerList.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiTab.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiProgressBar.h"

using namespace screenrecord;

namespace {

//空间充裕时的内容区宽度（像素），用来取按钮没有被压缩时的宽度
const int kRmwRoomyWidth = 2000;

int RmwWidthOf(balloonwjui::DuiControl* root, UINT id)
{
    balloonwjui::DuiControl* c = root->FindCtrlById(id);
    REQUIRE(c != NULL);
    const RECT rc = c->GetRect();
    return rc.right - rc.left;
}

} // namespace

TEST_CASE("RMW1 最小尺寸下工具栏按钮不被压扁，列表仍有高度", "[recordmgr-layout]")
{
    RecordManagerBody body = BuildRecordManagerBody();
    REQUIRE(body.m_root);
    REQUIRE(body.m_list != NULL);
    const UINT buttons[] = { kRecordMgrIdPlay, kRecordMgrIdOpenFolder, kRecordMgrIdSend, kRecordMgrIdRename,
                             kRecordMgrIdDelete };
    const size_t buttonCount = sizeof(buttons) / sizeof(buttons[0]);
    //DuiFrameWindow 的客户区就是整个窗口，内容区高度 = 整窗高 - 标题栏高
    const int contentH = kRecordMgrMinH - kRecordMgrTitleBarH;

    RECT roomy = { 0, 0, kRmwRoomyWidth, contentH };
    body.m_root->ForceLayout(roomy);
    int roomyWidths[sizeof(buttons) / sizeof(buttons[0])] = { 0 };
    for (size_t i = 0; i < buttonCount; ++i)
        roomyWidths[i] = RmwWidthOf(body.m_root.get(), buttons[i]);

    const int minW = RecordManagerMinWidth();
    CHECK(minW >= kRecordMgrDefaultW * 2 / 3);
    RECT tight = { 0, 0, minW, contentH };
    body.m_root->ForceLayout(tight);
    int prevRight = kRecordMgrPadding;
    for (size_t i = 0; i < buttonCount; ++i)
    {
        INFO("按钮 index = " << i);
        CHECK(RmwWidthOf(body.m_root.get(), buttons[i]) == roomyWidths[i]);
        const RECT rc = body.m_root->FindCtrlById(buttons[i])->GetRect();
        CHECK(rc.left >= prevRight);
        prevRight = rc.right;
    }
    //「共 N 个」在最后一个按钮右边，右边不越过内边距
    const RECT count = body.m_root->FindCtrlById(kRecordMgrIdCount)->GetRect();
    CHECK(count.left >= prevRight);
    CHECK(count.right <= minW - kRecordMgrPadding);
    CHECK(count.right > count.left);

    //列表至少能显示三行
    const RECT list = body.m_list->GetRect();
    CHECK(list.bottom - list.top >= 3 * kRecordMgrRowH);
    CHECK(list.bottom <= contentH - kRecordMgrPadding);
}

TEST_CASE("CAPRMW1 显示编辑字幕时最小宽度下六个按钮不被压扁", "[recordmgr-layout]")
{
    RecordManagerBody body = BuildRecordManagerBody();
    REQUIRE(body.m_root);
    REQUIRE(body.m_caption != NULL);
    //默认隐藏
    CHECK_FALSE(body.m_caption->IsVisible());
    body.m_caption->SetVisible(true);
    const UINT buttons[] = { kRecordMgrIdPlay, kRecordMgrIdOpenFolder, kRecordMgrIdSend, kRecordMgrIdRename,
                             kRecordMgrIdDelete, kRecordMgrIdCaption };
    const size_t buttonCount = sizeof(buttons) / sizeof(buttons[0]);
    const int contentH = kRecordMgrMinH - kRecordMgrTitleBarH;

    RECT roomy = { 0, 0, kRmwRoomyWidth, contentH };
    body.m_root->ForceLayout(roomy);
    int roomyWidths[sizeof(buttons) / sizeof(buttons[0])] = { 0 };
    for (size_t i = 0; i < buttonCount; ++i)
        roomyWidths[i] = RmwWidthOf(body.m_root.get(), buttons[i]);

    const int minW = RecordManagerMinWidth();
    RECT tight = { 0, 0, minW, contentH };
    body.m_root->ForceLayout(tight);
    int prevRight = kRecordMgrPadding;
    for (size_t i = 0; i < buttonCount; ++i)
    {
        INFO("按钮 index = " << i);
        CHECK(RmwWidthOf(body.m_root.get(), buttons[i]) == roomyWidths[i]);
        const RECT rc = body.m_root->FindCtrlById(buttons[i])->GetRect();
        CHECK(rc.left >= prevRight);
        prevRight = rc.right;
    }
    const RECT count = body.m_root->FindCtrlById(kRecordMgrIdCount)->GetRect();
    CHECK(count.left >= prevRight);
    CHECK(count.right <= minW - kRecordMgrPadding);
}

TEST_CASE("RMW3 本机与云端两个页签，云端页在最小尺寸下排得下", "[recordmgr-layout]")
{
    RecordManagerBody body = BuildRecordManagerBody();
    REQUIRE(body.m_root);
    REQUIRE(body.m_tabs != NULL);
    REQUIRE(body.m_localPage != NULL);
    REQUIRE(body.m_cloudPage != NULL);
    REQUIRE(body.m_cloudList != NULL);
    //默认是本机页：云端页隐藏，「加载更多」与云端提示也隐藏
    CHECK(body.m_localPage->IsVisible());
    CHECK_FALSE(body.m_cloudPage->IsVisible());
    CHECK_FALSE(body.m_cloudMore->IsVisible());
    CHECK_FALSE(body.m_cloudEmpty->IsVisible());

    //切到云端页（与窗口 showPage 的做法相同：改显隐后对根容器强制排一次）
    body.m_localPage->SetVisible(false);
    body.m_cloudPage->SetVisible(true);
    const UINT buttons[] = { kRecordMgrIdCloudPlay, kRecordMgrIdCloudDownload, kRecordMgrIdCloudSend,
                             kRecordMgrIdCloudRename, kRecordMgrIdCloudDelete };
    const size_t count = sizeof(buttons) / sizeof(buttons[0]);
    const int contentH = kRecordMgrMinH - kRecordMgrTitleBarH;
    const RECT roomy = { 0, 0, kRmwRoomyWidth, contentH };
    body.m_root->ForceLayout(roomy);
    int roomyWidths[sizeof(buttons) / sizeof(buttons[0])];
    for (size_t i = 0; i < count; ++i)
        roomyWidths[i] = RmwWidthOf(body.m_root.get(), buttons[i]);

    //最小宽度：五个按钮不被压扁、依次排开，用量条与用量文字在它们右边、内边距以内
    const int minW = RecordManagerMinWidth();
    const RECT tight = { 0, 0, minW, contentH };
    body.m_root->ForceLayout(tight);
    int prevRight = kRecordMgrPadding;
    for (size_t i = 0; i < count; ++i)
    {
        INFO("cloud button " << buttons[i]);
        CHECK(RmwWidthOf(body.m_root.get(), buttons[i]) == roomyWidths[i]);
        const RECT rc = body.m_root->FindCtrlById(buttons[i])->GetRect();
        CHECK(rc.left >= prevRight);
        prevRight = rc.right;
    }
    const RECT bar = body.m_root->FindCtrlById(kRecordMgrIdCloudUsageBar)->GetRect();
    const RECT usage = body.m_root->FindCtrlById(kRecordMgrIdCloudUsage)->GetRect();
    CHECK(bar.left >= prevRight);
    CHECK(bar.right - bar.left == kRecordMgrUsageBarW);
    CHECK(bar.bottom - bar.top == kRecordMgrUsageBarH);
    CHECK(usage.left >= bar.right);
    CHECK(usage.right <= minW - kRecordMgrPadding);
    //页签在最上面，云端列表在工具栏之下，仍至少能显示三行
    const RECT tabs = body.m_tabs->GetRect();
    const RECT list = body.m_cloudList->GetRect();
    CHECK(tabs.top == kRecordMgrPadding);
    CHECK(tabs.bottom - tabs.top == kRecordMgrTabH);
    CHECK(list.top >= usage.bottom);
    CHECK(list.bottom - list.top >= 3 * kRecordMgrRowH);
    CHECK(list.bottom <= contentH - kRecordMgrPadding);

    //状态列：宽度放得下最长的状态文字，设置后能取回
    CHECK(RecordManagerTrailingWidth() >= 96);
    body.m_cloudList->AddItem(L"产品周会");
    body.m_cloudList->SetTrailing(0, L"上传中 72%", RGB(45, 108, 223), 72);
    CHECK(body.m_cloudList->TrailingText(0) == L"上传中 72%");
    CHECK(body.m_cloudList->TrailingText(1).empty());
    body.m_cloudList->ClearTrailing();
    CHECK(body.m_cloudList->TrailingText(0).empty());
}

namespace {

//截图的高度：默认窗口去掉标题栏；宽度与窗口相同，取默认宽度与最小宽度中较大的一个（见 RMSNAP）
const int kRmSnapH = kRecordMgrDefaultH - kRecordMgrTitleBarH;

bool RmFindPngEncoder(CLSID& clsid)
{
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0)
        return false;
    std::vector<BYTE> buf(size);
    Gdiplus::ImageCodecInfo* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    Gdiplus::GetImageEncoders(num, size, info);
    for (UINT i = 0; i < num; ++i)
    {
        if (wcscmp(info[i].MimeType, L"image/png") == 0)
        {
            clsid = info[i].Clsid;
            return true;
        }
    }
    return false;
}

} // namespace

//隐藏用例：把云端页（状态列、进度条、用量条）画进内存位图，存成 PNG 供人工核对界面。
//须先设环境变量 FLAMINGO_SNAP_DIR（PNG 存放的目录），单独运行 "[.rmsnap]"
TEST_CASE("RMSNAP 云端页截图", "[.rmsnap]")
{
    wchar_t dir[MAX_PATH] = { 0 };
    REQUIRE(::GetEnvironmentVariableW(L"FLAMINGO_SNAP_DIR", dir, MAX_PATH) > 0);
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartupInput gdipInput;
    REQUIRE(Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, NULL) == Gdiplus::Ok);
    {
        RecordManagerBody body = BuildRecordManagerBody();
        REQUIRE(body.m_root);
        //与窗口相同：先排版，再选中第二个页签（DuiTab 在宽度为 0 时选中第二个页签会把页签整体滚出可见区）
        const int minW = RecordManagerMinWidth();
        const int kRmSnapW = (kRecordMgrDefaultW > minW) ? kRecordMgrDefaultW : minW;
        body.m_localPage->SetVisible(false);
        body.m_cloudPage->SetVisible(true);
        body.m_cloudMore->SetVisible(true);
        const RECT all = { 0, 0, kRmSnapW, kRmSnapH };
        body.m_root->ForceLayout(all);
        body.m_tabs->SetCurSel(kRecordMgrPageCloud, false);

        const int64_t kGb = 1024LL * 1024 * 1024;
        body.m_cloudUsage->SetText(CloudUsageText(true, 12992000000LL, 50 * kGb, 30).c_str());
        body.m_cloudUsageBar->SetPos(CloudUsagePermille(12992000000LL, 50 * kGb), false);
        //选中第一行（可观看）时五个按钮都可用
        body.m_cloudPlay->SetEnabled(true);
        body.m_cloudDownload->SetEnabled(true);
        body.m_cloudSend->SetEnabled(true);
        body.m_cloudRename->SetEnabled(true);
        body.m_cloudDelete->SetEnabled(true);

        SYSTEMTIME t;
        memset(&t, 0, sizeof(t));
        t.wYear = 2026;
        t.wMonth = 10;
        t.wDay = 6;
        t.wHour = 14;
        t.wMinute = 20;
        struct SnapRow
        {
            const wchar_t* m_title;
            int            m_source;
            bool           m_audio;
            int64_t        m_durationMs;
            int64_t        m_bytes;
            int            m_state;
            int            m_progress;
            int            m_daysLeft;
        };
        const SnapRow rows[] = {
            { L"产品周会", cloudrecord_source_share, false, 3492000, 642148761LL, cloud_row_ready, -1, -1 },
            { L"架构评审（第二轮）", cloudrecord_source_share, false, 2550000, 0, cloud_row_composing, 45, -1 },
            { L"新人培训：客户端打包", cloudrecord_source_upload, false, 728000, 155189248LL, cloud_row_uploading, 72, -1 },
            { L"需求澄清会", cloudrecord_source_share, false, 1907000, 0, cloud_row_pending, -1, -1 },
            { L"周五复盘", cloudrecord_source_share, false, 1503000, 0, cloud_row_compose_failed, -1, -1 },
            { L"客户访谈 - 华东区", cloudrecord_source_upload, true, 3725000, 60083404LL, cloud_row_ready, -1, 3 },
        };
        for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
        {
            const int index = body.m_cloudList->AddItem(rows[i].m_title);
            body.m_cloudList->SetItemSubText(index, CloudRowSubText(t, rows[i].m_source, rows[i].m_audio,
                                                                    rows[i].m_durationMs, rows[i].m_bytes).c_str());
            const int progress = CloudRowShowsProgress(rows[i].m_state, rows[i].m_progress) ? rows[i].m_progress : -1;
            body.m_cloudList->SetTrailing(index, CloudRowStateText(rows[i].m_state, rows[i].m_progress, rows[i].m_daysLeft),
                                          CloudRowStateColor(rows[i].m_state, rows[i].m_daysLeft), progress);
        }
        body.m_cloudList->SetCurSel(0, false);

        HDC screenDC = ::GetDC(NULL);
        REQUIRE(screenDC != NULL);
        HDC memDC = ::CreateCompatibleDC(screenDC);
        HBITMAP bmp = ::CreateCompatibleBitmap(screenDC, kRmSnapW, kRmSnapH);
        ::ReleaseDC(NULL, screenDC);
        REQUIRE(memDC != NULL);
        REQUIRE(bmp != NULL);
        HGDIOBJ oldBmp = ::SelectObject(memDC, bmp);
        HBRUSH bg = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
        ::FillRect(memDC, &all, bg);
        ::DeleteObject(bg);
        body.m_root->OnPaint(memDC, all);
        ::GdiFlush();
        ::SelectObject(memDC, oldBmp);
        CLSID png;
        REQUIRE(RmFindPngEncoder(png));
        {
            Gdiplus::Bitmap image(bmp, NULL);
            const std::wstring path = std::wstring(dir) + L"\\recordmgr_cloud.png";
            CHECK(image.Save(path.c_str(), &png, NULL) == Gdiplus::Ok);
        }
        ::DeleteObject(bmp);
        ::DeleteDC(memDC);
    }
    Gdiplus::GdiplusShutdown(gdipToken);
}
