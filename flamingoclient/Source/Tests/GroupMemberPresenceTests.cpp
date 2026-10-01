/**
 *  GroupMemberPresenceTests.cpp
 *
 *  群聊窗成员侧栏里成员头像右下角的在线状态徽标（标签 [groupchat][memberpresence]）：
 *
 *    GMP1 按 userid 更新某个成员行的在线状态，只影响那一行；侧栏里没有的 userid 不影响任何行。
 *    GMP2 成员不在通讯录里（状态未知）时不画徽标；协议之外的取值按未知处理。
 *    GMP3 徽标的位置：在头像右下角，连同外圈白描边都收在头像以内，不压到右侧的昵称。
 *    GMP4 隐藏用例：把「未知 / 在线 / 离开 / 忙碌 / 离线」五种成员行画成 PNG，供人工核对外观。
 *    GMP5 实际绘制：在线的成员行在徽标区域里有在线绿色，状态未知的成员行没有。
 *
 *  用 Catch2 v3（amalgamated 形态）。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <gdiplus.h>
#include <memory>
#include <vector>

#include "../net/Protocol.h"   // presence_type
#include "../UI/BalloonChatWnd/GroupMemberItem.h"
#include "../UI/BalloonChatWnd/GroupMemberList.h"

using namespace balloonchatwnd;

namespace {

// 成员行的绘制尺寸（像素）：宽度与群聊窗右侧边栏相当，高度为固定行高
const int kRowW = 240;
const int kRowH = kGroupMemberRowHeight;

// 成员行头像左缘距行左缘的距离（像素），与 GroupMemberItem.cpp 的行内边距一致
const int kRowEdgePad = 10;

// 徽标圆底外加白描边后的半径（像素）：绘制方框 10 × 圆底比例 20/24 ÷ 2 + 白描边 1.5
const double kBadgeRingRadius = 10.0 * 20.0 / 24.0 / 2.0 + 1.5;

// 在线徽标的颜色（#16A34A，见 MainPanelStatusMenu.cpp 的 GetPresenceVisual），以及判定
// "接近这个颜色"时每个通道允许的偏差（抗锯齿边缘的像素会与底色混合）
const int kOnlineR = 0x16;
const int kOnlineG = 0xA3;
const int kOnlineB = 0x4A;
const int kColorTolerance = 24;

// 在作用域内启动 GDI+，离开时关闭（成员行的头像与徽标用 GDI+ 绘制）。
class ScopedGdiplusForMemberRow
{
public:
    ScopedGdiplusForMemberRow()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    ~ScopedGdiplusForMemberRow()
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

// 一张自上而下存储的 32 位内存画布，用来离屏绘制成员行并读像素。
class RowCanvas
{
public:
    RowCanvas(int w, int h)
        : m_w(w)
        , m_h(h)
        , m_dc(NULL)
        , m_bmp(NULL)
        , m_old(NULL)
        , m_bits(NULL)
    {
        BITMAPINFO bi;
        ::ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth       = w;
        bi.bmiHeader.biHeight      = -h;   // 负高 = 自上而下，内存第 0 行即最上面一行
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = NULL;
        m_dc = ::CreateCompatibleDC(NULL);
        m_bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        m_bits = (unsigned int*)bits;
        if (m_dc != NULL && m_bmp != NULL)
        {
            m_old = ::SelectObject(m_dc, m_bmp);
            RECT all = { 0, 0, w, h };
            HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
            ::FillRect(m_dc, &all, white);
            ::DeleteObject(white);
        }
    }

    ~RowCanvas()
    {
        if (m_dc != NULL && m_old != NULL)
        {
            ::SelectObject(m_dc, m_old);
        }
        if (m_bmp != NULL)
        {
            ::DeleteObject(m_bmp);
        }
        if (m_dc != NULL)
        {
            ::DeleteDC(m_dc);
        }
    }

    bool Ok() const
    {
        return m_dc != NULL && m_bmp != NULL && m_bits != NULL;
    }

    // 把成员行摆到 rc 并画到画布上。用 SetRect 而不是 Layout：成员行没有子控件，基类 Layout
    // 不记录矩形，OnPaint 按 m_rcItem 取位置，矩形为空时什么都不画。
    void Paint(GroupMemberItem& item, const RECT& rc)
    {
        item.SetRect(rc);
        item.OnPaint(m_dc, rc);
        ::GdiFlush();
    }

    // 数 rc 范围内接近在线绿色的像素。
    int CountOnlineGreen(const RECT& rc) const
    {
        int count = 0;
        for (int y = rc.top; y < rc.bottom && y < m_h; ++y)
        {
            for (int x = rc.left; x < rc.right && x < m_w; ++x)
            {
                const unsigned int px = m_bits[y * m_w + x];
                const int r = (int)((px >> 16) & 0xFF);
                const int g = (int)((px >> 8) & 0xFF);
                const int b = (int)(px & 0xFF);
                if (abs(r - kOnlineR) <= kColorTolerance && abs(g - kOnlineG) <= kColorTolerance
                    && abs(b - kOnlineB) <= kColorTolerance)
                {
                    ++count;
                }
            }
        }
        return count;
    }

    // 把整张画布存成 PNG。成功返回 true。
    bool SavePng(LPCWSTR path) const
    {
        UINT num = 0;
        UINT size = 0;
        if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0)
        {
            return false;
        }
        std::vector<BYTE> buf(size);
        Gdiplus::ImageCodecInfo* codecs = (Gdiplus::ImageCodecInfo*)&buf[0];
        if (Gdiplus::GetImageEncoders(num, size, codecs) != Gdiplus::Ok)
        {
            return false;
        }
        for (UINT i = 0; i < num; ++i)
        {
            if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
            {
                Gdiplus::Bitmap bmp(m_bmp, NULL);
                return bmp.Save(path, &codecs[i].Clsid, NULL) == Gdiplus::Ok;
            }
        }
        return false;
    }

private:
    int           m_w;      //画布宽（像素）
    int           m_h;      //画布高（像素）
    HDC           m_dc;     //画布所在的内存 DC
    HBITMAP       m_bmp;    //画布位图（DIBSection）
    HGDIOBJ       m_old;    //选入画布位图前 DC 里原来的位图，析构时换回
    unsigned int* m_bits;   //画布像素，按 BGRA 存放
};

// 造一个成员行。
std::unique_ptr<GroupMemberItem> MakeRow(LPCTSTR name, int presence)
{
    std::unique_ptr<GroupMemberItem> item(new GroupMemberItem());
    item->SetMember(name, CString(name).Left(1), RGB(0x5B, 0x8F, 0xF9), 0);
    item->SetPresence(presence);
    return item;
}

// 成员行在 rc 里时，头像的左上角（与 GroupMemberItem::OnPaint 的取法一致）。
POINT AvatarOrigin(const RECT& rc)
{
    POINT pt;
    pt.x = rc.left + kRowEdgePad;
    pt.y = rc.top + (rc.bottom - rc.top - kGroupMemberAvatarPx) / 2;
    return pt;
}

} // anonymous namespace

TEST_CASE("GMP1 按 userid 更新成员行的在线状态，只影响那一行", "[groupchat][memberpresence]")
{
    GroupMemberList list;
    const int64_t ids[] = { 5010, 5058, 5308 };   //三名成员的 userid，取值无特殊含义
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        std::unique_ptr<GroupMemberItem> item(new GroupMemberItem());
        item->SetUserId(ids[i]);
        list.AddChild(std::move(item));
    }

    CHECK(list.SetMemberPresence(ids[1], ::presence_online));
    const GroupMemberItem* rows[3];
    for (size_t i = 0; i < 3; ++i)
    {
        rows[i] = dynamic_cast<const GroupMemberItem*>(list.Children()[i].get());
        REQUIRE(rows[i] != nullptr);
    }
    CHECK(rows[0]->Presence() == kMemberPresenceUnknown);
    CHECK(rows[1]->Presence() == ::presence_online);
    CHECK(rows[2]->Presence() == kMemberPresenceUnknown);

    //侧栏里没有的 userid：返回 false，任何一行都不变
    CHECK_FALSE(list.SetMemberPresence(9999, ::presence_away));
    CHECK(rows[0]->Presence() == kMemberPresenceUnknown);
    CHECK(rows[1]->Presence() == ::presence_online);
}

TEST_CASE("GMP2 状态未知时不画徽标，协议之外的取值按未知处理", "[groupchat][memberpresence]")
{
    GroupMemberItem item;
    CHECK(item.Presence() == kMemberPresenceUnknown);   //新建的行默认未知

    item.SetPresence(::presence_offline);
    CHECK(item.Presence() == ::presence_offline);       //离线是合法状态，画灰色空心圆环
    item.SetPresence(::presence_busy);
    CHECK(item.Presence() == ::presence_busy);

    const int kOutOfRange = 7;   //协议里没有的取值
    item.SetPresence(kOutOfRange);
    CHECK(item.Presence() == kMemberPresenceUnknown);
    item.SetPresence(-5);
    CHECK(item.Presence() == kMemberPresenceUnknown);
}

TEST_CASE("GMP3 徽标在头像右下角，连同白描边收在头像以内，不压到昵称", "[groupchat][memberpresence]")
{
    const int avX = 10;   //头像左上角，取值无特殊含义
    const int avY = 6;
    const int avS = kGroupMemberAvatarPx;
    const RECT box = ComputeMemberPresenceBadgeBox(avX, avY, avS);

    //绘制方框边长 10，收在头像以内
    CHECK(box.right - box.left == 10);
    CHECK(box.bottom - box.top == 10);
    CHECK(box.left >= avX);
    CHECK(box.top >= avY);
    CHECK(box.right <= avX + avS);
    CHECK(box.bottom <= avY + avS);

    //圆心在头像的右下四分之一里
    const double cx = (box.left + box.right) / 2.0;
    const double cy = (box.top + box.bottom) / 2.0;
    CHECK(cx > avX + avS / 2.0);
    CHECK(cy > avY + avS / 2.0);

    //白描边外缘不超出头像外缘，离右侧昵称（头像右缘再隔 8 像素）还有距离
    CHECK(cx + kBadgeRingRadius <= avX + avS);
    CHECK(cy + kBadgeRingRadius <= avY + avS);
    CHECK(cx + kBadgeRingRadius < avX + avS + kGroupMemberAvatarGapPx);
}

TEST_CASE("GMP4 五种在线状态的成员行效果图", "[.][snapshot][memberpresence]")
{
    TCHAR path[MAX_PATH] = { 0 };
    if (::GetEnvironmentVariable(_T("FLAMINGO_SNAPSHOT_PATH"), path, MAX_PATH) == 0)
    {
        SUCCEED("FLAMINGO_SNAPSHOT_PATH not set, snapshot skipped");
        return;
    }
    ScopedGdiplusForMemberRow gdiplus;
    REQUIRE(gdiplus.Ok());

    const int presences[] = { kMemberPresenceUnknown, ::presence_online, ::presence_away,
                              ::presence_busy, ::presence_offline };
    const LPCTSTR names[] = { _T("状态未知的成员"), _T("在线的成员"), _T("离开的成员"),
                              _T("忙碌的成员"), _T("离线的成员") };
    const int count = (int)(sizeof(presences) / sizeof(presences[0]));
    RowCanvas canvas(kRowW, kRowH * count);
    REQUIRE(canvas.Ok());
    for (int i = 0; i < count; ++i)
    {
        std::unique_ptr<GroupMemberItem> row = MakeRow(names[i], presences[i]);
        RECT rc = { 0, kRowH * i, kRowW, kRowH * (i + 1) };
        canvas.Paint(*row, rc);
    }
    CHECK(canvas.SavePng(path));
}

TEST_CASE("GMP5 在线的成员行画出在线绿色徽标，状态未知的不画", "[groupchat][memberpresence]")
{
    ScopedGdiplusForMemberRow gdiplus;
    REQUIRE(gdiplus.Ok());

    const RECT rc = { 0, 0, kRowW, kRowH };
    const POINT av = AvatarOrigin(rc);
    const RECT box = ComputeMemberPresenceBadgeBox(av.x, av.y, kGroupMemberAvatarPx);

    {
        RowCanvas canvas(kRowW, kRowH);
        REQUIRE(canvas.Ok());
        std::unique_ptr<GroupMemberItem> row = MakeRow(_T("在线的成员"), ::presence_online);
        canvas.Paint(*row, rc);
        const int kMinGreenPixels = 20;   //徽标圆底面积约 50 像素，至少一半应接近在线绿色
        CHECK(canvas.CountOnlineGreen(box) >= kMinGreenPixels);
    }
    {
        RowCanvas canvas(kRowW, kRowH);
        REQUIRE(canvas.Ok());
        std::unique_ptr<GroupMemberItem> row = MakeRow(_T("状态未知的成员"), kMemberPresenceUnknown);
        canvas.Paint(*row, rc);
        CHECK(canvas.CountOnlineGreen(box) == 0);
    }
}
