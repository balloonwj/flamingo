/**
 *  OfflineAvatarGrayTests.cpp
 *
 *  离线用户头像置灰（2026-10-03）在各个界面控件上的测试，标签 [offlinegray]。判定函数本身
 *  （AG1 ~ AG4）在 OnlineCapabilityTests.cpp，主面板树的节点标记（AG5）在
 *  PresenceTreeMarksTests.cpp，本文件覆盖其余控件：
 *    AG6a  群成员行的灰彩判定：别人离线置灰、在线 / 离开 / 忙碌 / 未知不置灰；本人只看本端连接态；
 *    AG6b  群成员行的字形头像：离线时底色画成等亮度的灰色，恢复在线后画回原色；
 *    AG6c  群成员行的真图头像：离线时画灰度副本，恢复在线后画回彩色；
 *    AG6d  本端连接态只影响本人那一行，不影响别人；
 *    AG7a  资料窗的在线状态换算：在线 / 离开 / 忙碌按在线，离线与越界值按离线；
 *    AG7b  资料窗头像：模型标记为灰度时底色画成灰色、状态点为离线，取消后恢复原色；
 *    AG7c  资料窗头像的彩色底色常量与布局 XML 里写的底色一致；
 *    AG8   单聊窗标题栏的真图图标：要求灰度时圆内画成灰色、圆外仍是标题栏底色，不要求时画彩色；
 *    AG9a  气泡头像缓存的灰度副本：只解码一次，与彩色原图是两张位图，内容是等亮度的灰色；
 *    AG9b  作废路径时彩色与灰度一起作废；文件不存在时灰度副本也返回空；
 *    AG10a 消息列表按发送者设置灰彩：只影响该发送者的行，本人的行另由本端连接态决定，
 *          之后追加的行沿用记下的结果；
 *    AG10b 文字气泡（有渲染缓存）的字形头像：灰彩变化后重画出来的颜色跟着变；
 *    AG10c 气泡的真图头像：发送者离线时画灰度副本，上线后画回彩色。
 *
 *  在离屏位图上调用控件的 OnPaint，不创建窗口。框架 Catch2 v3；全局 _Module 由
 *  BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <cstdlib>   // abs：颜色分量比较
#include <string>
#include <vector>
#include <gdiplus.h>

#include "../UI/BalloonChatWnd/GroupMemberItem.h"
#include "../UI/BalloonChatWnd/ChatTitleIcon.h"
#include "../UI/BalloonChatWnd/ChatAvatarBitmapCache.h"
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonContact/ContactLayout.h"
#include "../UI/BalloonContact/ContactModel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../UI/BalloonMain/AvatarGray.h"
#include "../UI/BalloonMain/OnlineCapability.h"
#include "../net/Protocol.h"   // presence_type

using balloonchatwnd::GroupMemberItem;
using balloonchatwnd::ChatAvatarBitmapCache;
using balloonchatwnd::ChatBubble;
using balloonchatwnd::ChatMessage;
using balloonchatwnd::ChatMessageList;

namespace {

// 群成员行的测试尺寸（像素）：宽度足够放下头像与名字，高度与成员侧栏的行高相同。
const int kItemW = 200;
const int kItemH = 40;

// 群成员行头像左上角相对行的偏移，与 GroupMemberItem.cpp 的 kEdgePad、纵向居中算法一致。
const int kItemAvatarLeft = 10;
const int kItemAvatarTop  = (kItemH - balloonchatwnd::kGroupMemberAvatarPx) / 2;

// 取样点：头像上边缘往下几个像素的水平中点。这里在圆角之内，离右下角的状态徽标与
// 居中的头像字都足够远，取到的就是头像底色或真图本身的颜色。
const int kSampleDown = 4;

// 测试头像的颜色：纯色，换算成灰度后与原色明显不同。
const COLORREF kAvatarColor = RGB(0xFF, 0x7A, 0x45);
const COLORREF kImageColor  = RGB(0xE0, 0x20, 0x20);

// 测试图片的边长（像素），取上传头像的实际尺寸。
const int kImagePx = 256;

// 颜色比较的容差：真图经高质量插值缩放后，纯色区域的分量仍可能有 ±1~2 的偏差。
const int kColorTolerance = 3;

// 在作用域内启动 GDI+，离开时关闭。群成员行的头像用 GDI+ 绘制。
class AgScopedGdiplus
{
public:
    // 启动 GDI+；失败时 Ok() 返回 false。
    AgScopedGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    // 关闭 GDI+。
    ~AgScopedGdiplus()
    {
        if (m_ok)
        {
            Gdiplus::GdiplusShutdown(m_token);
        }
    }

    // GDI+ 是否启动成功。
    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌，关闭时要用
    bool      m_ok;      // 是否启动成功
};

// 把控件画到白底离屏位图上，取 (sx, sy) 处的像素颜色。画布建不出来返回 CLR_INVALID。
COLORREF PaintAndSample(balloonwjui::DuiControl& ctrl, int w, int h, int sx, int sy)
{
    HDC screenDC = ::GetDC(NULL);
    HDC memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(NULL, screenDC);
    if (memDC == NULL)
    {
        return CLR_INVALID;
    }
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;   // 负高 = 自上而下
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL || bits == NULL)
    {
        ::DeleteDC(memDC);
        return CLR_INVALID;
    }
    HGDIOBJ oldBmp = ::SelectObject(memDC, bmp);

    RECT all = { 0, 0, w, h };
    HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    ::FillRect(memDC, &all, white);
    ::DeleteObject(white);

    ctrl.SetRect(all);
    ctrl.OnPaint(memDC, all);
    ::GdiFlush();

    const COLORREF c = ::GetPixel(memDC, sx, sy);
    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(bmp);
    ::DeleteDC(memDC);
    return c;
}

// 标题栏图标的边长（像素），与 CBalloonChatWindow::RebuildPeerIcons 相同。
const int kTitleIconPx = 48;

// 取一个设备相关位图（DDB）在 (x, y) 处的像素颜色。失败返回 CLR_INVALID。
COLORREF SampleBitmap(HBITMAP hbm, int x, int y)
{
    HDC screenDC = ::GetDC(NULL);
    HDC memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(NULL, screenDC);
    if (memDC == NULL)
    {
        return CLR_INVALID;
    }
    HGDIOBJ old = ::SelectObject(memDC, hbm);
    const COLORREF c = ::GetPixel(memDC, x, y);
    ::SelectObject(memDC, old);
    ::DeleteDC(memDC);
    return c;
}

// 气泡头像在缓存里的边长与圆角（像素），与气泡头像一致即可。
const int kBubbleAvatarPx     = 34;
const int kBubbleAvatarRadius = 7;

// 消息列表的测试尺寸（像素）。
const int kListW = 400;
const int kListH = 260;

// 认定"画了这个头像"所需的最少同色像素数：34 见方的圆角头像约 1100 个像素，取一个远小于它、
// 又远大于文字抗锯齿偶然同色的值。
const int kMinAvatarPixels = 200;

// 字形头像的底色：聊天窗里没有别的元素用这个颜色，按颜色数像素不会误计。
const COLORREF kMonoColor = RGB(0x12, 0x9A, 0x34);

// 测试用的发送者 userid。
const int64_t kSenderA = 7001;
const int64_t kSenderB = 7002;

// 读 DIB 位图正中那个像素的 RGB（位图是预乘透明通道，正中不透明，预乘不影响取值）。
COLORREF DibCenterPixel(HBITMAP bmp)
{
    DIBSECTION ds;
    if (::GetObject(bmp, sizeof(ds), &ds) != sizeof(ds) || ds.dsBm.bmBits == NULL)
    {
        return CLR_INVALID;
    }
    const int w = ds.dsBm.bmWidth;
    const int h = (ds.dsBm.bmHeight < 0) ? -ds.dsBm.bmHeight : ds.dsBm.bmHeight;
    const BYTE* px = static_cast<const BYTE*>(ds.dsBm.bmBits) + ((h / 2) * w + w / 2) * 4;
    return RGB(px[2], px[1], px[0]);   // DIB 按 BGRA 存放
}

// 造一条对方发来的文字消息，发送者为 sender，字形头像底色为 kMonoColor；imagePath 非空时带真图头像。
ChatMessage MakePeerMessage(int64_t sender, const std::wstring& imagePath)
{
    ChatMessage m = balloonchatwnd::MakeTextMessage(false, _T("text"));
    m.senderUserId          = sender;
    m.senderAvatarText      = _T("A");
    m.senderAvatarColor     = kMonoColor;
    m.senderAvatarImagePath = imagePath.c_str();
    return m;
}

// 造一条本人发出的文字消息。
ChatMessage MakeSelfMessage()
{
    ChatMessage m = balloonchatwnd::MakeTextMessage(true, _T("mine"));
    m.senderUserId = kSenderB + 1;
    return m;
}

// 把消息列表排版后画到白底离屏位图上，数出指定颜色的像素个数。画布建不出来返回 -1。
int PaintListAndCount(ChatMessageList& list, COLORREF want)
{
    RECT all = { 0, 0, kListW, kListH };
    list.ForceLayout(all);

    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    ::ReleaseDC(NULL, screen);
    if (mem == NULL)
    {
        return -1;
    }
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = kListW;
    bi.bmiHeader.biHeight      = -kListH;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = ::CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (dib == NULL || bits == NULL)
    {
        ::DeleteDC(mem);
        return -1;
    }
    HGDIOBJ old = ::SelectObject(mem, dib);
    HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    ::FillRect(mem, &all, white);
    ::DeleteObject(white);
    list.OnPaint(mem, all);
    ::GdiFlush();

    const unsigned int target = ((unsigned int)GetRValue(want) << 16)
                              | ((unsigned int)GetGValue(want) << 8)
                              |  (unsigned int)GetBValue(want);
    const unsigned int* px = static_cast<const unsigned int*>(bits);
    int count = 0;
    for (int i = 0; i < kListW * kListH; ++i)
    {
        if ((px[i] & 0x00FFFFFFu) == target)
        {
            ++count;
        }
    }
    ::SelectObject(mem, old);
    ::DeleteObject(dib);
    ::DeleteDC(mem);
    return count;
}

// 取消息列表里全部消息气泡（跳过时间分隔行），按显示顺序。
std::vector<ChatBubble*> MessageBubbles(ChatMessageList& list)
{
    std::vector<ChatBubble*> out;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->GetMessage().kind != balloonchatwnd::kMsgTime)
        {
            out.push_back(b);
        }
    }
    return out;
}

// 两个颜色的每个分量相差都不超过容差。
bool ColorNear(COLORREF a, COLORREF b)
{
    return abs((int)GetRValue(a) - (int)GetRValue(b)) <= kColorTolerance
        && abs((int)GetGValue(a) - (int)GetGValue(b)) <= kColorTolerance
        && abs((int)GetBValue(a) - (int)GetBValue(b)) <= kColorTolerance;
}

// 颜色是否为灰色（三个分量在容差内相等）。
bool IsGrayish(COLORREF c)
{
    return abs((int)GetRValue(c) - (int)GetGValue(c)) <= kColorTolerance
        && abs((int)GetGValue(c) - (int)GetBValue(c)) <= kColorTolerance;
}

// 取群成员行头像上的取样点颜色。
COLORREF SampleMemberAvatar(GroupMemberItem& item)
{
    return PaintAndSample(item, kItemW, kItemH,
                          kItemAvatarLeft + balloonchatwnd::kGroupMemberAvatarPx / 2,
                          kItemAvatarTop + kSampleDown);
}

// 临时目录下的文件路径，文件名带进程 id（并发跑的测试进程互不干扰），并删掉旧文件。
std::wstring AgTempFile(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t prefix[32] = { 0 };
    ::swprintf_s(prefix, L"%u_", static_cast<unsigned int>(::GetCurrentProcessId()));
    std::wstring path = dir;
    path += prefix;
    path += name;
    ::DeleteFileW(path.c_str());
    return path;
}

// 写一张 kImagePx 见方的纯色 PNG。成功返回 true。需要 GDI+ 已启动。
bool WriteSolidPng(const std::wstring& path, COLORREF color)
{
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0)
    {
        return false;
    }
    std::vector<BYTE> buf(size);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    Gdiplus::GetImageEncoders(num, size, codecs);
    const CLSID* png = NULL;
    for (UINT i = 0; i < num; ++i)
    {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
        {
            png = &codecs[i].Clsid;
            break;
        }
    }
    if (png == NULL)
    {
        return false;
    }
    Gdiplus::Bitmap bmp(kImagePx, kImagePx, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.Clear(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));
    }
    return bmp.Save(path.c_str(), png, NULL) == Gdiplus::Ok;
}

} // anonymous namespace

TEST_CASE("AG6a 群成员行的灰彩判定：别人看在线状态，本人看本端连接态", "[offlinegray][groupmember]")
{
    GroupMemberItem other;
    other.SetIsSelf(false);

    // 状态未知（新建的行默认如此）不置灰。
    CHECK_FALSE(other.IsAvatarGray());

    other.SetPresence(presence_offline);
    CHECK(other.IsAvatarGray());
    other.SetPresence(presence_online);
    CHECK_FALSE(other.IsAvatarGray());
    other.SetPresence(presence_away);
    CHECK_FALSE(other.IsAvatarGray());
    other.SetPresence(presence_busy);
    CHECK_FALSE(other.IsAvatarGray());

    GroupMemberItem self;
    self.SetIsSelf(true);
    self.SetPresence(presence_online);
    CHECK_FALSE(self.IsAvatarGray());
    self.SetSelfConnectionState(balloonmain::kConnUiConnecting);
    CHECK(self.IsAvatarGray());
    self.SetSelfConnectionState(balloonmain::kConnUiOffline);
    CHECK(self.IsAvatarGray());
    self.SetSelfConnectionState(balloonmain::kConnUiOnline);
    CHECK_FALSE(self.IsAvatarGray());
}

TEST_CASE("AG6b 群成员行的字形头像：离线时底色画成灰色，上线后画回原色", "[offlinegray][groupmember]")
{
    AgScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());

    GroupMemberItem item;
    // 头像字留空，取样点只会落在底色上。
    item.SetMember(_T("Member"), CString(), kAvatarColor, 0);

    item.SetPresence(presence_offline);
    const COLORREF offline = SampleMemberAvatar(item);
    REQUIRE(offline != CLR_INVALID);
    CHECK(ColorNear(offline, balloonmain::MakeGrayColor(kAvatarColor)));

    item.SetPresence(presence_online);
    const COLORREF online = SampleMemberAvatar(item);
    CHECK(ColorNear(online, kAvatarColor));
}

TEST_CASE("AG6c 群成员行的真图头像：离线时画灰度副本，上线后画回彩色", "[offlinegray][groupmember]")
{
    AgScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());

    const std::wstring path = AgTempFile(L"ag6c_avatar.png");
    REQUIRE(WriteSolidPng(path, kImageColor));
    {
        GroupMemberItem item;
        item.SetMember(_T("Member"), _T("M"), kAvatarColor, 0, CString(path.c_str()));

        item.SetPresence(presence_offline);
        const COLORREF offline = SampleMemberAvatar(item);
        REQUIRE(offline != CLR_INVALID);
        CHECK(IsGrayish(offline));
        CHECK(ColorNear(offline, balloonmain::MakeGrayColor(kImageColor)));

        item.SetPresence(presence_online);
        const COLORREF online = SampleMemberAvatar(item);
        CHECK(ColorNear(online, kImageColor));
    }
    ::DeleteFileW(path.c_str());
}

TEST_CASE("AG6d 本端连接态只影响本人那一行，不影响别人", "[offlinegray][groupmember]")
{
    AgScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());

    GroupMemberItem other;
    other.SetIsSelf(false);
    other.SetMember(_T("Other"), CString(), kAvatarColor, 0);
    other.SetPresence(presence_online);
    other.SetSelfConnectionState(balloonmain::kConnUiOffline);
    CHECK_FALSE(other.IsAvatarGray());
    CHECK(ColorNear(SampleMemberAvatar(other), kAvatarColor));

    GroupMemberItem self;
    self.SetIsSelf(true);
    self.SetMember(_T("Me"), CString(), kAvatarColor, 0);
    self.SetPresence(presence_online);
    self.SetSelfConnectionState(balloonmain::kConnUiOffline);
    CHECK(self.IsAvatarGray());
    CHECK(ColorNear(SampleMemberAvatar(self), balloonmain::MakeGrayColor(kAvatarColor)));
}

TEST_CASE("AG7a 资料窗的在线状态换算：在线、离开、忙碌按在线，其余按离线", "[offlinegray][contact]")
{
    CHECK(ballooncontact::ContactStatusFromPresence(presence_online) == ballooncontact::kContactStatusOnline);
    CHECK(ballooncontact::ContactStatusFromPresence(presence_away) == ballooncontact::kContactStatusOnline);
    CHECK(ballooncontact::ContactStatusFromPresence(presence_busy) == ballooncontact::kContactStatusOnline);
    CHECK(ballooncontact::ContactStatusFromPresence(presence_offline) == ballooncontact::kContactStatusOffline);
    // 已作废的 4（曾经的「隐身」）与其它越界值按离线，与通讯录的头像灰彩一致。
    CHECK(ballooncontact::ContactStatusFromPresence(4) == ballooncontact::kContactStatusOffline);
    CHECK(ballooncontact::ContactStatusFromPresence(999) == ballooncontact::kContactStatusOffline);
}

TEST_CASE("AG7b 资料窗头像：标记为灰度时底色画成灰色，取消后恢复原色", "[offlinegray][contact]")
{
    ballooncontact::ContactModel model;
    balloonwjui::DuiAvatar avatar;

    model.SetStatus(ballooncontact::kContactStatusOffline);
    model.SetAvatarGray(true);
    ballooncontact::ApplyContactAvatar(&avatar, model);
    CHECK(avatar.GetFallbackBgColor() == balloonmain::MakeGrayColor(ballooncontact::kContactAvatarBgColor));
    CHECK(avatar.GetStatus() == balloonwjui::DuiAvatar::StatusOffline);

    model.SetStatus(ballooncontact::kContactStatusOnline);
    model.SetAvatarGray(false);
    ballooncontact::ApplyContactAvatar(&avatar, model);
    CHECK(avatar.GetFallbackBgColor() == ballooncontact::kContactAvatarBgColor);
    CHECK(avatar.GetStatus() == balloonwjui::DuiAvatar::StatusOnline);

    // 控件为 NULL 时什么都不做，不崩溃。
    ballooncontact::ApplyContactAvatar(NULL, model);
}

TEST_CASE("AG7c 资料窗头像的彩色底色常量与布局 XML 里写的底色一致", "[offlinegray][contact]")
{
    std::unique_ptr<balloonwjui::DuiControl> root = ballooncontact::BuildContactBody();
    REQUIRE(root.get() != NULL);
    balloonwjui::DuiAvatar* avatar = dynamic_cast<balloonwjui::DuiAvatar*>(
        root->FindCtrlById((UINT)ballooncontact::kIdAvatar));
    REQUIRE(avatar != NULL);
    CHECK(avatar->GetFallbackBgColor() == ballooncontact::kContactAvatarBgColor);
}

TEST_CASE("AG8 标题栏真图图标：灰度时圆内变灰、圆外仍是标题栏底色", "[offlinegray][titleicon]")
{
    AgScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());

    const std::wstring path = AgTempFile(L"ag8_avatar.png");
    REQUIRE(WriteSolidPng(path, kImageColor));

    const int center = kTitleIconPx / 2;

    HBITMAP grayIcon = balloonchatwnd::MakeRoundTitleIconFromImage(
        path.c_str(), balloonchatwnd::kChatTitleBarBg, kTitleIconPx, true);
    REQUIRE(grayIcon != NULL);
    const COLORREF grayCenter = SampleBitmap(grayIcon, center, center);
    const COLORREF grayCorner = SampleBitmap(grayIcon, 0, 0);
    ::DeleteObject(grayIcon);
    CHECK(IsGrayish(grayCenter));
    CHECK(ColorNear(grayCenter, balloonmain::MakeGrayColor(kImageColor)));
    // 圆外的角落没有经过灰度换算，仍是标题栏底色本身，标题栏上看不出方形的边。
    CHECK(grayCorner == balloonchatwnd::kChatTitleBarBg);

    HBITMAP colorIcon = balloonchatwnd::MakeRoundTitleIconFromImage(
        path.c_str(), balloonchatwnd::kChatTitleBarBg, kTitleIconPx);
    REQUIRE(colorIcon != NULL);
    const COLORREF colorCenter = SampleBitmap(colorIcon, center, center);
    ::DeleteObject(colorIcon);
    CHECK(ColorNear(colorCenter, kImageColor));

    ::DeleteFileW(path.c_str());
}

TEST_CASE("AG9a 气泡头像缓存的灰度副本：只解码一次、与原图是两张位图、内容为灰色", "[offlinegray][avatarcache]")
{
    AgScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    ChatAvatarBitmapCache& cache = ChatAvatarBitmapCache::Inst();
    cache.Clear();

    const std::wstring path = AgTempFile(L"ag9a_avatar.png");
    REQUIRE(WriteSolidPng(path, kImageColor));

    HBITMAP color = cache.Get(path, kBubbleAvatarPx, kBubbleAvatarRadius);
    REQUIRE(color != NULL);
    HBITMAP gray = cache.GetGray(path, kBubbleAvatarPx, kBubbleAvatarRadius);
    REQUIRE(gray != NULL);
    CHECK(gray != color);
    CHECK(cache.GetGray(path, kBubbleAvatarPx, kBubbleAvatarRadius) == gray);
    // 灰度副本由彩色原图换算而来，不再解码图片文件。
    CHECK(cache.DecodeCount() == 1);

    CHECK(ColorNear(DibCenterPixel(color), kImageColor));
    const COLORREF grayCenter = DibCenterPixel(gray);
    CHECK(IsGrayish(grayCenter));
    CHECK(ColorNear(grayCenter, balloonmain::MakeGrayColor(kImageColor)));

    cache.Clear();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("AG9b 作废路径时彩色与灰度一起作废；文件不存在时灰度副本为空", "[offlinegray][avatarcache]")
{
    AgScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    ChatAvatarBitmapCache& cache = ChatAvatarBitmapCache::Inst();
    cache.Clear();

    const std::wstring missing = AgTempFile(L"ag9b_missing.png");
    CHECK(cache.GetGray(missing, kBubbleAvatarPx, kBubbleAvatarRadius) == NULL);

    const std::wstring path = AgTempFile(L"ag9b_avatar.png");
    REQUIRE(WriteSolidPng(path, kImageColor));
    REQUIRE(cache.GetGray(path, kBubbleAvatarPx, kBubbleAvatarRadius) != NULL);
    const int decodedBefore = cache.DecodeCount();

    // 同一路径换成另一种颜色（头像被重新下载覆盖），作废后灰度副本按新图重新生成。
    REQUIRE(WriteSolidPng(path, kMonoColor));
    cache.Invalidate(path);
    HBITMAP gray = cache.GetGray(path, kBubbleAvatarPx, kBubbleAvatarRadius);
    REQUIRE(gray != NULL);
    CHECK(cache.DecodeCount() == decodedBefore + 1);
    CHECK(ColorNear(DibCenterPixel(gray), balloonmain::MakeGrayColor(kMonoColor)));

    cache.Clear();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("AG10a 消息列表按发送者设置灰彩，本人的行看本端连接态，新行沿用记下的结果", "[offlinegray][bubblegray]")
{
    ChatMessageList list;
    list.AppendMessage(MakePeerMessage(kSenderA, std::wstring()));
    list.AppendMessage(MakePeerMessage(kSenderB, std::wstring()));
    list.AppendMessage(MakeSelfMessage());

    std::vector<ChatBubble*> rows = MessageBubbles(list);
    REQUIRE(rows.size() == 3);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        CHECK_FALSE(rows[i]->IsAvatarGray());
    }

    list.SetSenderAvatarGray(kSenderA, true);
    CHECK(rows[0]->IsAvatarGray());
    CHECK_FALSE(rows[1]->IsAvatarGray());
    CHECK_FALSE(rows[2]->IsAvatarGray());

    // 本人的行只由 SetSelfAvatarGray 决定；即便本人的 userid 被当成发送者置灰也不受影响。
    list.SetSenderAvatarGray(kSenderB + 1, true);
    CHECK_FALSE(rows[2]->IsAvatarGray());
    list.SetSelfAvatarGray(true);
    CHECK(rows[2]->IsAvatarGray());

    // 之后追加的行沿用记下的结果。
    list.AppendMessage(MakePeerMessage(kSenderA, std::wstring()));
    list.AppendMessage(MakePeerMessage(kSenderB, std::wstring()));
    list.AppendMessage(MakeSelfMessage());
    rows = MessageBubbles(list);
    REQUIRE(rows.size() == 6);
    CHECK(rows[3]->IsAvatarGray());
    CHECK_FALSE(rows[4]->IsAvatarGray());
    CHECK(rows[5]->IsAvatarGray());

    // 发送者上线、本端重新连上：相应的行恢复彩色。
    list.SetSenderAvatarGray(kSenderA, false);
    list.SetSelfAvatarGray(false);
    CHECK_FALSE(rows[0]->IsAvatarGray());
    CHECK_FALSE(rows[3]->IsAvatarGray());
    CHECK_FALSE(rows[2]->IsAvatarGray());
    CHECK_FALSE(rows[5]->IsAvatarGray());

    // userid 不合法时忽略。
    list.SetSenderAvatarGray(0, true);
    CHECK_FALSE(rows[1]->IsAvatarGray());
}

TEST_CASE("AG10b 文字气泡的字形头像：灰彩变化后重画出来的颜色跟着变", "[offlinegray][bubblegray]")
{
    AgScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());

    ChatMessageList list;
    list.AppendMessage(MakePeerMessage(kSenderA, std::wstring()));
    const COLORREF grayMono = balloonmain::MakeGrayColor(kMonoColor);

    CHECK(PaintListAndCount(list, kMonoColor) > kMinAvatarPixels);

    // 文字气泡的头像画在渲染缓存里：置灰后必须重画缓存，原色像素应当一个都不剩。
    list.SetSenderAvatarGray(kSenderA, true);
    CHECK(PaintListAndCount(list, kMonoColor) == 0);
    CHECK(PaintListAndCount(list, grayMono) > kMinAvatarPixels);

    list.SetSenderAvatarGray(kSenderA, false);
    CHECK(PaintListAndCount(list, kMonoColor) > kMinAvatarPixels);
}

TEST_CASE("AG10c 气泡的真图头像：发送者离线时画灰度副本，上线后画回彩色", "[offlinegray][bubblegray]")
{
    AgScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    ChatAvatarBitmapCache::Inst().Clear();

    const std::wstring path = AgTempFile(L"ag10c_avatar.png");
    REQUIRE(WriteSolidPng(path, kImageColor));
    {
        ChatMessageList list;
        list.AppendMessage(MakePeerMessage(kSenderA, path));
        CHECK(PaintListAndCount(list, kImageColor) > kMinAvatarPixels);

        list.SetSenderAvatarGray(kSenderA, true);
        CHECK(PaintListAndCount(list, kImageColor) == 0);
        CHECK(PaintListAndCount(list, balloonmain::MakeGrayColor(kImageColor)) > kMinAvatarPixels);

        list.SetSenderAvatarGray(kSenderA, false);
        CHECK(PaintListAndCount(list, kImageColor) > kMinAvatarPixels);
    }
    ChatAvatarBitmapCache::Inst().Clear();
    ::DeleteFileW(path.c_str());
}
