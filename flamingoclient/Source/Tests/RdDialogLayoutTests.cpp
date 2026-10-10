/**
 *  远程桌面会话提示对话框的提示文字与排版单测，RdDialogLayoutTests.cpp
 *
 *  [rddlg]
 *    RDD1 对端名字过长时截断并加「…」；名字为空时用「对方」
 *    RDD2 两种形态、名字长短不同时，按设计尺寸排版后提示文字不越过左右边距、高度容得下实际画出的全部文字、
 *         在提示区里垂直居中，按钮栏离窗口下缘留白不变
 *
 *  2026-10-10 之前提示文字单行居中、没有左右留白，对端带备注名时两端都被截掉。
 *  框架 Catch2 v3。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *  balloonwj@qq.com   2026-10-10
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>

#include "../../FlamingoMedia/RemoteDesktop/RdDialogLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"

using namespace rd;

namespace {

// 演示环境里出问题的名字（真名加备注）
const wchar_t* const kNameWithRemark = L"赵卓轩（卓轩（桌面端））";
// 一个远超 kRdMaxPeerNameChars 的名字，检查截断后仍排得下
const wchar_t* const kVeryLongName = L"研发部前端组临时借调到测试组的同事某某某某某某某某某某";

// 把标签按 width × kTallPx 的矩形画到白底位图上，返回最下面一行有墨迹的行号（从 0 起），以及最左、最右有墨迹的列。
// 什么都没画出时 lastRow 为 -1。会改动标签的矩形，调用方须在此之前取好需要的排版结果。
void InkExtent(balloonwjui::DuiLabel* label, int width, int& lastRow, int& minCol, int& maxCol)
{
    const int kTallPx = 300;     // 位图高，足够画下任何一段提示文字
    const int kInkLevel = 160;   // 三个分量都低于该值的像素算作墨迹（深灰字，抗锯齿边缘较浅）
    lastRow = -1;
    minCol = width;
    maxCol = -1;
    const RECT rc = { 0, 0, width, kTallPx };
    label->SetRect(rc);
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -kTallPx;   // 负值表示自上而下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC dc = ::CreateCompatibleDC(NULL);
    HBITMAP bmp = ::CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (dc == NULL || bmp == NULL || bits == NULL)
    {
        return;
    }
    HGDIOBJ old = ::SelectObject(dc, bmp);
    ::FillRect(dc, &rc, (HBRUSH)::GetStockObject(WHITE_BRUSH));
    label->OnPaint(dc, rc);
    ::GdiFlush();
    const DWORD* px = static_cast<const DWORD*>(bits);
    for (int y = 0; y < kTallPx; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const DWORD v = px[y * width + x];
            if (((v >> 16) & 0xFF) < kInkLevel && ((v >> 8) & 0xFF) < kInkLevel && (v & 0xFF) < kInkLevel)
            {
                lastRow = y;
                if (x < minCol)
                {
                    minCol = x;
                }
                if (x > maxCol)
                {
                    maxCol = x;
                }
            }
        }
    }
    ::SelectObject(dc, old);
    ::DeleteObject(bmp);
    ::DeleteDC(dc);
}

} // namespace

TEST_CASE("RDD1 对端名字过长时截断并加省略号，名字为空时用「对方」", "[rddlg]")
{
    //不超过上限的名字原样保留
    CHECK(ShortenRdPeerName(kNameWithRemark) == CString(kNameWithRemark));
    CString exact;
    for (int i = 0; i < kRdMaxPeerNameChars; ++i)
    {
        exact += L'名';
    }
    CHECK(ShortenRdPeerName(exact) == exact);

    //超过上限：总长等于上限，最后一个字符是省略号，前面是原名字的开头
    const CString shortened = ShortenRdPeerName(kVeryLongName);
    CHECK(shortened.GetLength() == kRdMaxPeerNameChars);
    CHECK(shortened[kRdMaxPeerNameChars - 1] == L'\x2026');
    CHECK(shortened.Left(kRdMaxPeerNameChars - 1) == CString(kVeryLongName).Left(kRdMaxPeerNameChars - 1));

    //提示文字带上截断后的名字；空名字用「对方」
    CHECK(MakeRdDialogText(kRdModeCalling, kVeryLongName).Find(shortened) >= 0);
    CHECK(MakeRdDialogText(kRdModeAuth, kVeryLongName).Find(shortened) >= 0);
    CHECK(MakeRdDialogText(kRdModeCalling, CString()).Find(L"对方") >= 0);
}

TEST_CASE("RDD2 提示文字在设计尺寸下完整显示、不越过边距并垂直居中", "[rddlg]")
{
    const int modes[] = { kRdModeCalling, kRdModeAuth };
    const wchar_t* names[] = { L"程薇", kNameWithRemark, kVeryLongName };
    for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m)
    {
        for (size_t n = 0; n < sizeof(names) / sizeof(names[0]); ++n)
        {
            INFO("mode = " << modes[m] << ", name index = " << n);
            std::unique_ptr<balloonwjui::DuiControl> root = BuildRdDialogBody(modes[m]);
            const RECT content = { 0, 0, kRdDlgWidth, kRdDlgContentHeight };
            root->ForceLayout(content);
            ApplyRdDialogText(root.get(), MakeRdDialogText(modes[m], names[n]));

            balloonwjui::DuiControl* box = root->FindCtrlById(kRdIdMsgBox);
            balloonwjui::DuiLabel* msg = dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kRdIdMsg));
            balloonwjui::DuiControl* btn = root->FindCtrlById(kRdIdBtnRight);
            REQUIRE(box != NULL);
            REQUIRE(msg != NULL);
            REQUIRE(btn != NULL);
            const RECT rcBox = box->GetRect();
            const RECT rcMsg = msg->GetRect();
            const RECT rcBtn = btn->GetRect();

            //左右边距：文字区域不贴窗口边缘
            CHECK(rcMsg.left >= kRdMsgPadH);
            CHECK(rcMsg.right <= kRdDlgWidth - kRdMsgPadH);
            //垂直居中：上下留白相差不超过 1 像素（奇数余量时差 1）
            const int topGap = rcMsg.top - rcBox.top;
            const int bottomGap = rcBox.bottom - rcMsg.bottom;
            CHECK(topGap >= 0);
            CHECK(bottomGap >= 0);
            CHECK(topGap - bottomGap <= 1);
            CHECK(bottomGap - topGap <= 1);
            //按钮栏离窗口下缘的留白不变
            CHECK(rcBtn.bottom == kRdDlgContentHeight - kRdBottomPad);

            //实际画出的文字：全部落在分到的高度之内，也不越过标签左右两边
            const int allotted = rcMsg.bottom - rcMsg.top;
            const int width = rcMsg.right - rcMsg.left;
            int lastRow = -1;
            int minCol = 0;
            int maxCol = 0;
            InkExtent(msg, width, lastRow, minCol, maxCol);
            INFO("allotted = " << allotted << ", last ink row = " << lastRow
                 << ", ink columns = " << minCol << " .. " << maxCol << " of " << width);
            REQUIRE(lastRow >= 0);
            CHECK(allotted > lastRow);
            CHECK(minCol > 0);
            CHECK(maxCol < width - 1);
        }
    }
}
