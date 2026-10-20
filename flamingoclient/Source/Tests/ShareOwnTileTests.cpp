/**
 *  多人共享里「本人那一块」的窗口单元测试（标签 [sharemulti]），ShareOwnTileTests.cpp
 *  背景：2026-10-12 起每个人都看到全部画面（docs/远程桌面控制权限与观看工具条方案.md 第 2.2 节 C），本人正在共享的那一块
 *  也显示出来，取本机采集的画面，只能看。2026-10-12 与用户确认的用例：
 *    - MSC2b 非主持人共享者的观看窗：本人那一块从本机预览来源取帧（不取收流管线），来源有新帧时随之更新；
 *      没有放大时「当前那一块」是第一块别人的画面，申请控制、录制默认不会落到本人身上；
 *    - MSC3 主持人的窗口标题为「共享画面」，含主持人自己那一块，同样取本机预览，「当前那一块」跳过自己。
 *      「只有自己在共享时不打开」由控制器按 CountOtherScreens 判断，见 ShareMultiTests.cpp 的 SSMC6；
 *    - MSC5 本人正在共享自己的屏幕、没有打开「允许截图」时，观看窗与它的浮动工具条、「共享画面」窗口排除在屏幕采集之外
 *      （按窗口的显示亲和性检查）；打开「允许截图」或不再共享时恢复。只看不共享的人不排除。
 *    - MSC6（2026-10-13 可见桌面实机测试发现后补）画面清单变化时的布局：原来并列的，加进或去掉一块之后仍并列（方案第 28 条
 *      「默认按人数并列平分」）；原来放大了某一块、它还在清单里的，继续放大。原来清单一变化就把「当前那一块」放大，
 *      观看者在第二个人开始共享时看到的是一大一小，而不是左右平分。只用画面控件，不建窗口。
 *    - MSC8（同上）两块、四块画面时，每一块的名字标签都不被观看窗的浮动工具条盖住（原来右边那块、右上那块的标签在工具条下面）。
 *  这些用例会建出真实的观看窗（打开时默认全屏）与「共享画面」窗口，须在隐藏桌面上运行。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-12
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include <string>
#include <vector>
#include "../../FlamingoMedia/Share/ShareRoomPolicy.h"   // ShouldExcludeShareView（MSC5）
#include "../../FlamingoMedia/Share/ShareScreenFeed.h"
#include "../../FlamingoMedia/Share/ShareScreensWindow.h"
#include "../../FlamingoMedia/Share/ShareViewerWindow.h"
#include "../Common/I18n/I18n.h"
#include "../net/Protocol.h"   // screenshare_source_sharing

using namespace screenshare;

namespace {

//中转公钥的字节数（与 libsodium 的公钥长度一致；用例不连中转，内容无所谓）
const int kOwnPubKeyLen = 32;
//主持人的画面座位（成员座位 0）与另一位共享者的画面座位
const int kOwnHostSeat = 0;
const int kOwnSharerSeat = 4;
//本机预览的两种画面尺寸（像素）
const int kOwnFrameW1 = 8;
const int kOwnFrameH1 = 6;
const int kOwnFrameW2 = 16;
const int kOwnFrameH2 = 10;
//BGRA 每像素字节数
const int kOwnBytesPerPixel = 4;
//标题文字缓冲的字符数
const int kOwnTitleChars = 128;

//只给一个座位出画面的假来源，代替推流管线的本机预览
class FakeOwnFeed : public IShareScreenFeed
{
public:
    explicit FakeOwnFeed(int seat)
        : m_seat(seat)
        , m_width(kOwnFrameW1)
        , m_height(kOwnFrameH1)
        , m_serial(1)
    {
    }

    //换一帧新尺寸的画面，帧序号加一
    void NextFrame(int width, int height)
    {
        m_width = width;
        m_height = height;
        ++m_serial;
    }

    virtual bool GetScreenFrame(int seat, std::vector<uint8_t>& bgra, int& width, int& height,
                                uint64_t& serial) const override
    {
        if (seat != m_seat)
            return false;
        bgra.assign(static_cast<size_t>(m_width) * m_height * kOwnBytesPerPixel, 0);
        width = m_width;
        height = m_height;
        serial = m_serial;
        return true;
    }

    virtual bool GetScreenSerial(int seat, uint64_t& serial) const override
    {
        if (seat != m_seat)
            return false;
        serial = m_serial;
        return true;
    }

    virtual std::vector<int> ScreenSeats() const override
    {
        return std::vector<int>(1, m_seat);
    }

    virtual int ScreenSourceState(int seat) const override
    {
        (void)seat;
        return screenshare_source_sharing;
    }

private:
    int      m_seat;     //出画面的座位
    int      m_width;    //画面宽
    int      m_height;   //画面高
    uint64_t m_serial;   //帧序号
};

//收命令的通知窗口
HWND OwnNotifyWindow()
{
    return ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
}

//把投给窗口的消息处理完（取帧是先投给自己再取）
void OwnPump(HWND hwnd)
{
    MSG msg;
    while (::PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}

//画面清单：主持人在前、另一位共享者在后
void OwnScreens(std::vector<int>& seats, std::vector<std::wstring>& names, bool selfIsHost)
{
    seats.clear();
    names.clear();
    seats.push_back(kOwnHostSeat);
    seats.push_back(kOwnSharerSeat);
    names.push_back(selfIsHost ? TRF(L"{0}（我）", L"host") : std::wstring(L"host"));
    names.push_back(selfIsHost ? std::wstring(L"sharer") : TRF(L"{0}（我）", L"sharer"));
}

//窗口当前是否排除在屏幕采集之外：显示亲和性不是「不排除」（WDA_NONE）
bool OwnIsExcluded(HWND hwnd)
{
    DWORD affinity = WDA_NONE;
    if (!::GetWindowDisplayAffinity(hwnd, &affinity))
        return false;
    return affinity != WDA_NONE;
}

} // namespace

TEST_CASE("MSC2b a non-host sharer's own tile in the viewer comes from the local preview", "[sharemulti]")
{
    const HWND notify = OwnNotifyWindow();
    REQUIRE(notify != NULL);
    {
        CShareViewerWindow wnd;
        ShareRelayInfo relay;
        relay.m_roomId = "msc2b-room";
        unsigned char pubKey[kOwnPubKeyLen] = { 0 };
        //不连中转：收流管线启动失败，窗口照样建好
        wnd.Open(relay, pubKey, L"host", notify, NULL, std::shared_ptr<balloonp2p::P2PLinkSlot>());
        REQUIRE(wnd.IsWindow());
        OwnPump(wnd.m_hWnd);
        std::vector<int> seats;
        std::vector<std::wstring> names;
        OwnScreens(seats, names, false);
        wnd.SetScreens(seats, names);
        //还没设本人那一块：「当前那一块」是第一块（主持人）
        CHECK(wnd.ActiveScreenSeat() == kOwnHostSeat);
        FakeOwnFeed feed(kOwnSharerSeat);
        wnd.SetLocalScreen(kOwnSharerSeat, &feed);
        OwnPump(wnd.m_hWnd);
        int w = 0;
        int h = 0;
        //本人那一块取到了本机预览的画面；主持人那块收流管线没有画面
        REQUIRE(wnd.ScreenFrameSizeForTest(kOwnSharerSeat, w, h));
        CHECK(w == kOwnFrameW1);
        CHECK(h == kOwnFrameH1);
        CHECK_FALSE(wnd.ScreenFrameSizeForTest(kOwnHostSeat, w, h));
        //「保存为图片」等按座位取帧的入口也取本机预览
        std::vector<uint8_t> bgra;
        CHECK(wnd.GetScreenFrame(kOwnSharerSeat, bgra, w, h));
        //来源有新帧：通知之后随之更新
        feed.NextFrame(kOwnFrameW2, kOwnFrameH2);
        wnd.OnLocalScreenFrame();
        OwnPump(wnd.m_hWnd);
        REQUIRE(wnd.ScreenFrameSizeForTest(kOwnSharerSeat, w, h));
        CHECK(w == kOwnFrameW2);
        CHECK(h == kOwnFrameH2);
        //本人那块排第二，「当前那一块」仍是主持人；把本人那块挪到第一位时跳过它
        CHECK(wnd.ActiveScreenSeat() == kOwnHostSeat);
        std::vector<int> swapped;
        swapped.push_back(kOwnSharerSeat);
        swapped.push_back(kOwnHostSeat);
        std::vector<std::wstring> swappedNames;
        swappedNames.push_back(names[1]);
        swappedNames.push_back(names[0]);
        wnd.SetScreens(swapped, swappedNames);
        CHECK(wnd.ActiveScreenSeat() == kOwnHostSeat);
        //只剩本人那一块：就是它
        wnd.SetScreens(std::vector<int>(1, kOwnSharerSeat), std::vector<std::wstring>(1, names[1]));
        CHECK(wnd.ActiveScreenSeat() == kOwnSharerSeat);
        wnd.CloseViewer();
    }
    ::DestroyWindow(notify);
}

TEST_CASE("MSC3 the host's window is titled Shared screens and includes the host's own tile", "[sharemulti]")
{
    const HWND notify = OwnNotifyWindow();
    REQUIRE(notify != NULL);
    {
        CShareScreensWindow wnd;
        std::vector<int> seats;
        std::vector<std::wstring> names;
        OwnScreens(seats, names, true);
        wnd.SetScreens(seats, names);
        wnd.SetOwnScreen(kOwnHostSeat);
        //推流管线：主持人自己的座位取到本机预览，别人的画面还没解出来
        FakeOwnFeed feed(kOwnHostSeat);
        wnd.Open(NULL, &feed, notify);
        REQUIRE(wnd.IsWindow());
        OwnPump(wnd.m_hWnd);
        wchar_t title[kOwnTitleChars] = { 0 };
        ::GetWindowTextW(wnd.m_hWnd, title, kOwnTitleChars);
        CHECK(std::wstring(title) == TR(L"共享画面"));
        int w = 0;
        int h = 0;
        REQUIRE(wnd.ScreenFrameSizeForTest(kOwnHostSeat, w, h));
        CHECK(w == kOwnFrameW1);
        CHECK(h == kOwnFrameH1);
        CHECK_FALSE(wnd.ScreenFrameSizeForTest(kOwnSharerSeat, w, h));
        //新帧到达：控制器在界面线程上调 OnFeedFrame
        feed.NextFrame(kOwnFrameW2, kOwnFrameH2);
        wnd.OnFeedFrame();
        REQUIRE(wnd.ScreenFrameSizeForTest(kOwnHostSeat, w, h));
        CHECK(w == kOwnFrameW2);
        //主持人自己排第一，「当前那一块」跳过自己，申请控制默认针对别人
        CHECK(wnd.ActiveScreenSeat() == kOwnSharerSeat);
        wnd.Close();
    }
    ::DestroyWindow(notify);
}

TEST_CASE("MSC5 view windows are excluded from capture while sharing one's own screen without allowing screenshots", "[sharemulti]")
{
    //规则：只在本人正在共享自己的屏幕、且没打开「允许截图」时排除
    CHECK(ShouldExcludeShareView(true, false));
    CHECK_FALSE(ShouldExcludeShareView(true, true));
    CHECK_FALSE(ShouldExcludeShareView(false, false));
    CHECK_FALSE(ShouldExcludeShareView(false, true));

    const HWND notify = OwnNotifyWindow();
    REQUIRE(notify != NULL);
    {
        //观看窗：先排除再打开（控制器在开始推流时设置），窗口与浮动工具条建好后都已排除
        CShareViewerWindow wnd;
        wnd.SetCaptureExcluded(true);
        ShareRelayInfo relay;
        relay.m_roomId = "msc5-room";
        unsigned char pubKey[kOwnPubKeyLen] = { 0 };
        wnd.Open(relay, pubKey, L"host", notify, NULL, std::shared_ptr<balloonp2p::P2PLinkSlot>());
        REQUIRE(wnd.IsWindow());
        OwnPump(wnd.m_hWnd);
        const HWND bar = wnd.BarWindowForTest();
        REQUIRE(bar != NULL);
        CHECK(wnd.IsCaptureExcluded());
        CHECK(OwnIsExcluded(wnd.m_hWnd));
        CHECK(OwnIsExcluded(bar));
        //打开「允许截图」或不再共享：恢复
        wnd.SetCaptureExcluded(false);
        CHECK_FALSE(OwnIsExcluded(wnd.m_hWnd));
        CHECK_FALSE(OwnIsExcluded(bar));
        //窗口开着时再排除：立即生效
        wnd.SetCaptureExcluded(true);
        CHECK(OwnIsExcluded(wnd.m_hWnd));
        CHECK(OwnIsExcluded(bar));
        //关窗复位：下一次观看从不排除开始
        wnd.CloseViewer();
        CHECK_FALSE(wnd.IsCaptureExcluded());
    }
    {
        //「共享画面」窗口：先排除再打开
        CShareScreensWindow wnd;
        std::vector<int> seats;
        std::vector<std::wstring> names;
        OwnScreens(seats, names, true);
        wnd.SetScreens(seats, names);
        wnd.SetCaptureExcluded(true);
        FakeOwnFeed feed(kOwnHostSeat);
        wnd.Open(NULL, &feed, notify);
        REQUIRE(wnd.IsWindow());
        CHECK(OwnIsExcluded(wnd.m_hWnd));
        wnd.SetCaptureExcluded(false);
        CHECK_FALSE(OwnIsExcluded(wnd.m_hWnd));
        wnd.Close();
    }
    ::DestroyWindow(notify);
}

TEST_CASE("MSC6 the side-by-side layout survives screen list changes, an enlarged screen stays enlarged", "[sharemulti]")
{
    //三位共享人的画面座位
    const int seatA = 0;
    const int seatB = 4;
    const int seatC = 6;
    //画面控件的大小：2 块时左右平分，取右半块的中心；放大时主画面在上方，取它上部的一点。缺陷存在时第一块已被放大、
    //占满上方，右半块中心也落在它上面，双击变成还原，「当前那一块」不会变成 B
    const RECT area = { 0, 0, 1000, 600 };
    const POINT rightHalf = { 750, 300 };
    const POINT mainTile = { 500, 100 };
    CShareVideoCanvas canvas;
    canvas.SetRect(area);

    std::vector<int> seats(1, seatA);
    std::vector<std::wstring> names(1, L"A");
    canvas.SetScreens(seats, names);
    CHECK(canvas.ActiveScreenSeat() == seatA);

    //B 开始共享：仍为并列（左 A、右 B），双击右半边放大的是 B
    seats.push_back(seatB);
    names.push_back(L"B");
    canvas.SetScreens(seats, names);
    REQUIRE(canvas.OnLButtonDblClk(rightHalf, 0));
    CHECK(canvas.ActiveScreenSeat() == seatB);

    //C 也开始共享：放大着的 B 还在，继续放大；双击主画面还原为并列，「当前那一块」回到第一块
    seats.push_back(seatC);
    names.push_back(L"C");
    canvas.SetScreens(seats, names);
    CHECK(canvas.ActiveScreenSeat() == seatB);
    REQUIRE(canvas.OnLButtonDblClk(mainTile, 0));
    CHECK(canvas.ActiveScreenSeat() == seatA);

    //C 停止共享：并列保持并列，双击右半边放大的仍是 B
    seats.pop_back();
    names.pop_back();
    canvas.SetScreens(seats, names);
    REQUIRE(canvas.OnLButtonDblClk(rightHalf, 0));
    CHECK(canvas.ActiveScreenSeat() == seatB);

    //放大着的 B 停止共享：只剩 A 一块，铺满；B 再次共享时为并列，双击右半边放大的是 B
    seats.pop_back();
    names.pop_back();
    canvas.SetScreens(seats, names);
    CHECK(canvas.ActiveScreenSeat() == seatA);
    seats.push_back(seatB);
    names.push_back(L"B");
    canvas.SetScreens(seats, names);
    REQUIRE(canvas.OnLButtonDblClk(rightHalf, 0));
    CHECK(canvas.ActiveScreenSeat() == seatB);
}

TEST_CASE("MSC8 tile name labels are not covered by the floating bar", "[sharemulti]")
{
    //四块画面的座位：主持人、本人与另外两位共享者
    const int seats4[] = { kOwnHostSeat, kOwnSharerSeat, kOwnSharerSeat + 1, kOwnSharerSeat + 2 };
    const int seatCount4 = static_cast<int>(sizeof(seats4) / sizeof(seats4[0]));
    const int seatCount2 = 2;
    const HWND notify = OwnNotifyWindow();
    REQUIRE(notify != NULL);
    {
        CShareViewerWindow wnd;
        ShareRelayInfo relay;
        relay.m_roomId = "msc8-room";
        unsigned char pubKey[kOwnPubKeyLen] = { 0 };
        wnd.Open(relay, pubKey, L"host", notify, NULL, std::shared_ptr<balloonp2p::P2PLinkSlot>());
        REQUIRE(wnd.IsWindow());
        OwnPump(wnd.m_hWnd);
        const HWND bar = wnd.BarWindowForTest();
        REQUIRE(bar != NULL);
        REQUIRE(::IsWindowVisible(bar));
        const int counts[] = { seatCount2, seatCount4 };
        for (size_t c = 0; c < sizeof(counts) / sizeof(counts[0]); ++c)
        {
            std::vector<int> seats(seats4, seats4 + counts[c]);
            std::vector<std::wstring> names;
            for (size_t i = 0; i < seats.size(); ++i)
                names.push_back(TRF(L"{0}（我）", L"name"));
            wnd.SetScreens(seats, names);
            ::RedrawWindow(wnd.m_hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            OwnPump(wnd.m_hWnd);
            //浮动工具条在观看窗客户区里的位置
            RECT barRc;
            ::GetWindowRect(bar, &barRc);
            ::MapWindowPoints(NULL, wnd.m_hWnd, reinterpret_cast<POINT*>(&barRc), 2);
            for (size_t i = 0; i < seats.size(); ++i)
            {
                INFO("screens " << counts[c] << ", tile " << i);
                RECT label;
                REQUIRE(wnd.ScreenLabelRectForTest(seats[i], label));
                RECT overlap;
                CHECK_FALSE(::IntersectRect(&overlap, &label, &barRc));
            }
        }
        wnd.CloseViewer();
    }
    ::DestroyWindow(notify);
}
