/**
 *  音视频通话优化的单元测试（标签 [callux]，docs/音视频通话优化方案.md），CallUxTests.cpp
 *  覆盖：
 *    - FWDC1 选会话窗口的「发起通话」模式：标题、确认按钮、说明区小标题、说明文字、只能选一个；转发与发送文件不受影响；
 *    - FWDC2 只能选一个时，已选集合由选择窗清空后再加入（模型层的 Toggle 与 Clear 配合）；
 *    - MAV1 媒体 DLL 的头像来源：来源给了字形与底色时生成字形位图并缓存，同一头像再取不新增位图；
 *    - MAV2 来源不给（没登记、返回失败）时画问号与中性灰；群头像走群的查询；
 *    - MAV3 照片路径解码不了时改画字形，不返回空位图；DuiAvatar 套用头像后挂上位图、名字与底色作后备；
 *    - WPN1 通话窗固定期间拦下哪些系统命令、怎样保住窗口位置与大小；
 *    - WPN2 图钉图标两种状态的位图与提示文字；
 *    - WPN3 置顶 / 取消置顶后扩展样式里的 WS_EX_TOPMOST 随之变化，无效窗口直接返回 false；
 *    - WPN4（2026-10-06）图钉图标改画方案 C：未固定是与三按钮同色的空心线框，已固定是实心蓝并多一条底线；
 *    - CDL1 对方画面停止的判定与来电窗第二行的文字；
 *    - CDL2 视频态「头像 + 提示」的版式：小画面里头像比语音态版式大，提示在头像下方且不越出区域；
 *    - VEN1 视频引擎只开接收、不开摄像头也能解码对方画面，停止接收后不再交付（测试不打开真实摄像头）；
 *    - CCD1 一对一通话卡片的解析：呼叫时的字段、改写后的结果与时长，不是一对一卡片时返回 false；
 *    - CCD2 已结束卡片的状态文字按结束方式与本人是主叫还是被叫区分，接通过的写时长；
 *    - CCD3 还没结束的卡片：在通话中写「通话中」，否则主叫写「正在呼叫」、被叫写「邀请你通话」；
 *    - CCD4 套用到气泡：标题、摘要、副标题、图标用的媒体类型、没有按钮；内容不可用时仍是卡片；
 *    - CCD5 1717 推送的解析与会话列表摘要（「[语音通话]」「[视频通话]」）；
 *    - CLY1 呼叫中态与接通后同一副骨架：默认 776×569 排得下、画面区位置与接通后的语音态一致、底部栏只有「取消」；
 *    - ICL1 来电窗按 420×200 的设计尺寸排得下：头像、两行文字、按钮都不被压扁、不越界；
 *    - ICL2 改标题栏高度会重建窗口骨架、丢掉已放进去的客户区内容，来电窗要先设标题栏高度再放内容；
 *    - CVA1 语音态画面设了头像时画头像、没设时画听筒；
 *    - CVA2 视频态没有画面时画头像，来了画面只画画面，画面清空后头像重新出现。
 *  菜单两项见 MainPanelSystemMenuTests.cpp 的 SYSC1，列表里的「通话中」徽章见 MainPanelRowMarkTests.cpp 的 ICB1~ICB4。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string.h>
#include <string>
#include <vector>

#include "../UI/BalloonMain/ForwardRequest.h"
#include "../UI/BalloonMain/ForwardTargetModel.h"
#include "../../FlamingoMedia/Common/MediaAvatar.h"
#include "../../FlamingoMedia/Common/WindowPin.h"
#include "../../FlamingoMedia/Common/IncomingCallLayout.h"
#include "../../FlamingoMedia/Call/CallDlgLayout.h"
#include "../../FlamingoMedia/Call/CallVideoCanvas.h"
#include "../../FlamingoMedia/Call/CallingLayout.h"
#include "../../FlamingoMedia/Call/CallButtonArt.h"
#include "../../FlamingoMedia/Call/CallVideoEngine.h"
#include "../UI/BalloonChatWnd/ChatCallCard.h"
#include "../net/MessageProtocolClient.h"
#include "../../FlamingoMedia/Call/MfH264Codec.h"

#include <mfapi.h>   //MFStartup / MFShutdown：VEN1 在测试线程上编解码
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "LayoutFitCheck.h"

#include <objidl.h>    //gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>   //CVA1：语音态没设头像时用 GDI+ 画听筒

namespace {

//测试用的用户、群与头像
const int64_t kMavUser = 5302;
const int64_t kMavOther = 5303;
const int64_t kMavGroup = 600001;
const int kMavSizePx = 56;
const COLORREF kMavUserColor = RGB(0x2D, 0x6C, 0xDF);
const COLORREF kMavGroupColor = RGB(0x12, 0xB7, 0x6A);

//假的头像来源：按 userid / 群 id 给固定的字形与底色，可指定照片路径；记下被问了几次
class FakeAvatarQuery : public flamingomedia::IMediaAvatarQuery
{
public:
    FakeAvatarQuery()
        : m_userCalls(0)
        , m_groupCalls(0)
    {
    }

    virtual bool QueryUserAvatar(int64_t userId, flamingomedia::MediaAvatarInfo* out) override
    {
        ++m_userCalls;
        if (userId != kMavUser && userId != kMavOther)
            return false;
        wcsncpy_s(out->m_glyph, flamingomedia::kMediaAvatarGlyphChars, (userId == kMavUser) ? L"张" : L"韩", _TRUNCATE);
        out->m_color = static_cast<uint32_t>(kMavUserColor);
        wcsncpy_s(out->m_imagePath, flamingomedia::kMediaAvatarPathChars, m_photo.c_str(), _TRUNCATE);
        return true;
    }

    virtual bool QueryGroupAvatar(int64_t groupId, flamingomedia::MediaAvatarInfo* out) override
    {
        ++m_groupCalls;
        if (groupId != kMavGroup)
            return false;
        wcsncpy_s(out->m_glyph, flamingomedia::kMediaAvatarGlyphChars, L"研", _TRUNCATE);
        out->m_color = static_cast<uint32_t>(kMavGroupColor);
        return true;
    }

    std::wstring m_photo;   //给用户头像带的照片路径；空串表示没有照片
    int m_userCalls;        //QueryUserAvatar 被调用的次数
    int m_groupCalls;       //QueryGroupAvatar 被调用的次数
};

} // namespace

TEST_CASE("FWDC1 选会话窗口的发起通话模式：标题、按钮、说明与只能选一个", "[callux]")
{
    balloonmain::ForwardRequest voice;
    voice.m_callMedia = balloonmain::kForwardCallVoice;
    CHECK(balloonmain::ForwardPickerTitle(voice) == L"发起语音通话");
    CHECK(balloonmain::ForwardPickerConfirmText(voice) == L"呼叫");
    CHECK(balloonmain::ForwardPickerContentTitle(voice) == L"说明");
    CHECK(balloonmain::ForwardPickerTargetTitle(voice) == L"通话对象");
    CHECK(balloonmain::ForwardPickerSingleSelect(voice));
    CHECK(balloonmain::BuildForwardPreviewText(voice).find(L"语音通话") != std::wstring::npos);

    balloonmain::ForwardRequest video;
    video.m_callMedia = balloonmain::kForwardCallVideo;
    CHECK(balloonmain::ForwardPickerTitle(video) == L"发起视频通话");
    CHECK(balloonmain::ForwardPickerConfirmText(video) == L"呼叫");
    CHECK(balloonmain::BuildForwardPreviewText(video).find(L"视频通话") != std::wstring::npos);

    //转发与发送文件不受影响
    balloonmain::ForwardRequest forward;
    forward.m_msgids.push_back(1);
    CHECK(balloonmain::ForwardPickerTitle(forward) == L"转发");
    CHECK(balloonmain::ForwardPickerConfirmText(forward) == L"发送");
    CHECK(balloonmain::ForwardPickerContentTitle(forward) == L"转发内容");
    CHECK(balloonmain::ForwardPickerTargetTitle(forward) == L"发送给");
    CHECK_FALSE(balloonmain::ForwardPickerSingleSelect(forward));
    balloonmain::ForwardRequest file;
    file.m_filePath = L"C:\\temp\\a.mp4";
    CHECK(balloonmain::ForwardPickerTitle(file) == L"发送到会话");
    CHECK(balloonmain::ForwardPickerConfirmText(file) == L"发送");
}

TEST_CASE("FWDC2 只能选一个：先清空再加入，已选集合始终只有最后选的那个", "[callux]")
{
    const int64_t selfId = 5127;
    balloonmain::ForwardTargetSelection sel(selfId);
    balloonmain::ForwardTarget a;
    a.m_ctype = 1;
    a.m_cid = kMavUser;
    balloonmain::ForwardTarget g;
    g.m_ctype = 2;
    g.m_cid = kMavGroup;

    //与 CForwardTargetDlg::ToggleTarget 在单选模式下的做法相同：勾选一个没选过的目标前先清空
    sel.Toggle(a);
    REQUIRE(sel.Selected().size() == 1);
    if (!sel.IsSelected(g.m_ctype, g.m_cid))
        sel.Clear();
    sel.Toggle(g);
    REQUIRE(sel.Selected().size() == 1);
    CHECK(sel.Selected()[0].m_ctype == 2);
    CHECK(sel.Selected()[0].m_cid == kMavGroup);
    //再点一次已选的那个：取消选择
    sel.Toggle(g);
    CHECK(sel.Selected().empty());
}

TEST_CASE("MAV1 头像来源给了字形与底色时生成字形位图并缓存", "[callux]")
{
    FakeAvatarQuery query;
    flamingomedia::CMediaAvatarSource source;
    source.SetQuery(&query);

    const flamingomedia::MediaAvatarInfo info = source.QueryUser(kMavUser);
    CHECK(info.m_cbSize == sizeof(flamingomedia::MediaAvatarInfo));
    CHECK(std::wstring(info.m_glyph) == L"张");
    CHECK(info.m_color == static_cast<uint32_t>(kMavUserColor));
    CHECK(info.m_imagePath[0] == L'\0');

    HBITMAP first = source.GetUserBitmap(kMavUser, kMavSizePx);
    REQUIRE(first != NULL);
    CHECK(source.BitmapCount() == 1);
    //同一头像同一尺寸再取：复用缓存，不新增位图
    HBITMAP again = source.GetUserBitmap(kMavUser, kMavSizePx);
    CHECK(again == first);
    CHECK(source.BitmapCount() == 1);
    //另一个人：新的一张
    CHECK(source.GetUserBitmap(kMavOther, kMavSizePx) != NULL);
    CHECK(source.BitmapCount() == 2);
    //尺寸不合法：不生成
    CHECK(source.GetUserBitmap(kMavUser, 0) == NULL);

    source.Clear();
    CHECK(source.BitmapCount() == 0);
}

TEST_CASE("MAV2 来源不给时画问号与中性灰；群头像走群的查询", "[callux]")
{
    //没有登记来源
    flamingomedia::CMediaAvatarSource none;
    const flamingomedia::MediaAvatarInfo unknown = none.QueryUser(kMavUser);
    CHECK(std::wstring(unknown.m_glyph) == flamingomedia::kUnknownAvatarGlyph);
    CHECK(unknown.m_color == static_cast<uint32_t>(flamingomedia::kUnknownAvatarColor));
    CHECK(none.GetUserBitmap(kMavUser, kMavSizePx) != NULL);

    //来源不认识这个人、这个群
    FakeAvatarQuery query;
    flamingomedia::CMediaAvatarSource source;
    source.SetQuery(&query);
    CHECK(std::wstring(source.QueryUser(999).m_glyph) == flamingomedia::kUnknownAvatarGlyph);
    CHECK(std::wstring(source.QueryGroup(999).m_glyph) == flamingomedia::kUnknownAvatarGlyph);

    //群头像：经 QueryGroupAvatar，不经用户的查询
    const int userCalls = query.m_userCalls;
    const flamingomedia::MediaAvatarInfo group = source.QueryGroup(kMavGroup);
    CHECK(std::wstring(group.m_glyph) == L"研");
    CHECK(group.m_color == static_cast<uint32_t>(kMavGroupColor));
    CHECK(query.m_userCalls == userCalls);
    CHECK(source.GetGroupBitmap(kMavGroup, kMavSizePx) != NULL);

    //登记宿主为 NULL：等同没有来源
    source.SetHost(NULL);
    CHECK(std::wstring(source.QueryUser(kMavUser).m_glyph) == flamingomedia::kUnknownAvatarGlyph);
}

TEST_CASE("MAV3 照片解码不了时改画字形；DuiAvatar 套用头像后挂上位图与后备名字、底色", "[callux]")
{
    FakeAvatarQuery query;
    query.m_photo = L"C:\\no\\such\\photo-for-mav3.png";
    flamingomedia::CMediaAvatarSource source;
    source.SetQuery(&query);
    //照片不存在：仍返回位图（字形）
    CHECK(source.GetUserBitmap(kMavUser, kMavSizePx) != NULL);

    std::unique_ptr<balloonwjui::DuiAvatar> avatar = source.MakeUserAvatar(kMavUser, kMavSizePx);
    REQUIRE(avatar);
    CHECK(avatar->GetBitmap() != NULL);
    CHECK(avatar->GetName() == CString(L"张"));
    CHECK(avatar->GetFallbackBgColor() == kMavUserColor);
    CHECK(avatar->GetShape() == balloonwjui::DuiAvatar::ShapeCircle);

    //换成群头像：同一个控件就地更新
    source.ApplyGroup(avatar.get(), kMavGroup, kMavSizePx);
    CHECK(avatar->GetName() == CString(L"研"));
    CHECK(avatar->GetFallbackBgColor() == kMavGroupColor);
    //先销毁借用位图的控件，再释放位图
    avatar.reset();
    source.Clear();
}


namespace {

//WPN2：图钉图标上取样的两个像素（图标坐标）：钉帽上的一点（2026-10-06 改画方案 C 后，钉帽两种状态都完全不透明），
//以及左上角的透明底
const int kPinIconPx = 16;
const int kPinProbeX = 5;
const int kPinProbeY = 2;
//WPN4（方案 C 的造型）：钉身正中的一点（未固定的线框在这里是空心）、底线所在行上远离钉针的一点（只有已固定才有底线）
const int kPinHollowX = 7;
const int kPinHollowY = 5;
const int kPinBaselineX = 4;
const int kPinBaselineY = 14;
//空心处的不透明度上限：线框只覆盖这个像素的一小部分
const unsigned char kPinHollowAlphaMax = 128;
//标题栏三按钮字形的灰度（balloonui DuiFrameWindow.cpp 的 kGlyphInk = RGB(60, 60, 60)）
const unsigned char kPinGlyphGray = 60;
//像素各通道在 32 位 BGRA 里的偏移与 alpha 的不透明值
const int kChB = 0;
const int kChG = 1;
const int kChR = 2;
const int kChA = 3;
const int kBytesPerPx = 4;
const unsigned char kAlphaOpaque = 255;

//读 32 位 DIB 的 (x, y) 像素的某个通道。只用于本文件里自上而下创建（biHeight 取负）的位图：第 0 行在最前面。
//不能按 GetObject 取回的 dsBmih.biHeight 判断方向 —— 自上而下创建的 DIB 取回来也是正数（2026-10-05 实测，
//按它倒过来读会读到圆形头像下方的黑底）
unsigned char DibChannel(HBITMAP bmp, int x, int y, int channel)
{
    DIBSECTION ds;
    memset(&ds, 0, sizeof(ds));
    if (::GetObject(bmp, sizeof(ds), &ds) != sizeof(ds) || ds.dsBm.bmBits == NULL)
        return 0;
    if (x < 0 || y < 0 || x >= ds.dsBm.bmWidth || y >= ds.dsBm.bmHeight)
        return 0;
    const unsigned char* bits = static_cast<const unsigned char*>(ds.dsBm.bmBits);
    return bits[static_cast<size_t>(y) * ds.dsBm.bmWidthBytes + static_cast<size_t>(x) * kBytesPerPx + channel];
}

//建一张 32 位自上而下的 DIB，填成不透明的纯色（预乘 alpha 下不透明色就是原色）
HBITMAP MakeSolidDib(int w, int h, COLORREF color)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL || bits == NULL)
        return NULL;
    unsigned char* px = static_cast<unsigned char*>(bits);
    for (int i = 0; i < w * h; ++i)
    {
        px[i * kBytesPerPx + kChB] = GetBValue(color);
        px[i * kBytesPerPx + kChG] = GetGValue(color);
        px[i * kBytesPerPx + kChR] = GetRValue(color);
        px[i * kBytesPerPx + kChA] = kAlphaOpaque;
    }
    return bmp;
}

//CVA：画面控件的尺寸、头像与画面的颜色、判定「是这个颜色」的容差
const int kCvaW = 240;
const int kCvaH = 240;
const int kCvaAvatarPx = 96;
const COLORREF kCvaAvatarColor = RGB(0xE0, 0x10, 0x20);
const int kCvaFrameW = 16;
const int kCvaFrameH = 9;
const unsigned char kCvaFrameG = 0xC0;
const int kCvaTolerance = 12;

//进程内 GDI+ 的启停（语音态没设头像时画听筒要用）；多次 GdiplusStartup 是安全的
class GdiplusScope
{
public:
    GdiplusScope()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&m_token, &input, NULL);
    }

    ~GdiplusScope()
    {
        if (m_token != 0)
            Gdiplus::GdiplusShutdown(m_token);
    }

private:
    ULONG_PTR m_token;   //GdiplusStartup 返回的令牌
};

//把画面控件画进一张与控件同尺寸的 32 位位图，返回 (x, y) 处的颜色
COLORREF PaintCanvasAndProbe(call::CallVideoCanvas& canvas, int x, int y)
{
    HBITMAP target = MakeSolidDib(kCvaW, kCvaH, RGB(0xFF, 0xFF, 0xFF));
    if (target == NULL)
        return CLR_INVALID;
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    ::ReleaseDC(NULL, screen);
    HGDIOBJ old = ::SelectObject(mem, target);
    const RECT rc = canvas.GetRect();
    //OnPaint 在 CallVideoCanvas 里是私有的覆写，经基类的公有虚函数调用
    balloonwjui::DuiControl* base = &canvas;
    base->OnPaint(mem, rc);
    ::GdiFlush();
    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    const COLORREF color = RGB(DibChannel(target, x, y, kChR), DibChannel(target, x, y, kChG),
                               DibChannel(target, x, y, kChB));
    ::DeleteObject(target);
    return color;
}

//两个颜色在容差之内
bool NearColor(COLORREF a, COLORREF b)
{
    return abs(static_cast<int>(GetRValue(a)) - static_cast<int>(GetRValue(b))) <= kCvaTolerance
           && abs(static_cast<int>(GetGValue(a)) - static_cast<int>(GetGValue(b))) <= kCvaTolerance
           && abs(static_cast<int>(GetBValue(a)) - static_cast<int>(GetBValue(b))) <= kCvaTolerance;
}

//ICL1：控件 id
const UINT kIclDecline = 10;
const UINT kIclAccept = 11;

} // namespace

TEST_CASE("WPN1 固定期间拦下移动、改大小、最小化、最大化、还原，关闭与其余命令照常", "[callux]")
{
    //拖标题栏时系统发的是 SC_MOVE 再带上 HTCAPTION（低 4 位）；拖边框是 SC_SIZE 带上边框方向
    CHECK(flamingomedia::IsSysCommandBlockedWhenPinned(SC_MOVE));
    CHECK(flamingomedia::IsSysCommandBlockedWhenPinned(SC_MOVE | HTCAPTION));
    CHECK(flamingomedia::IsSysCommandBlockedWhenPinned(SC_SIZE));
    CHECK(flamingomedia::IsSysCommandBlockedWhenPinned(SC_SIZE | WMSZ_BOTTOMRIGHT));
    CHECK(flamingomedia::IsSysCommandBlockedWhenPinned(SC_MINIMIZE));
    CHECK(flamingomedia::IsSysCommandBlockedWhenPinned(SC_MAXIMIZE));
    CHECK(flamingomedia::IsSysCommandBlockedWhenPinned(SC_RESTORE));
    //关闭、Alt+空格菜单本身、屏幕保护等照常
    CHECK_FALSE(flamingomedia::IsSysCommandBlockedWhenPinned(SC_CLOSE));
    CHECK_FALSE(flamingomedia::IsSysCommandBlockedWhenPinned(SC_KEYMENU));
    CHECK_FALSE(flamingomedia::IsSysCommandBlockedWhenPinned(SC_SCREENSAVE));

    //保住位置与大小：加上 SWP_NOMOVE 与 SWP_NOSIZE，原有标志（如改层叠次序）不动
    WINDOWPOS wp;
    memset(&wp, 0, sizeof(wp));
    wp.flags = SWP_NOACTIVATE;
    flamingomedia::KeepWindowPosWhenPinned(&wp);
    CHECK((wp.flags & SWP_NOMOVE) != 0);
    CHECK((wp.flags & SWP_NOSIZE) != 0);
    CHECK((wp.flags & SWP_NOACTIVATE) != 0);
    CHECK((wp.flags & SWP_NOZORDER) == 0);
    //传 NULL 不崩溃
    flamingomedia::KeepWindowPosWhenPinned(NULL);
}

TEST_CASE("WPN2 图钉图标：两种状态各一张 16x16 的 32 位位图，钉杆颜色不同、底透明；提示文字随状态变", "[callux]")
{
    HBITMAP normal = flamingomedia::MakePinCaptionIcon(false);
    HBITMAP active = flamingomedia::MakePinCaptionIcon(true);
    REQUIRE(normal != NULL);
    REQUIRE(active != NULL);
    BITMAP bm;
    memset(&bm, 0, sizeof(bm));
    REQUIRE(::GetObject(normal, sizeof(bm), &bm) != 0);
    CHECK(bm.bmWidth == kPinIconPx);
    CHECK(bm.bmHeight == kPinIconPx);
    CHECK(bm.bmBitsPixel == 32);

    //钉杆上的点不透明；左上角是透明底
    CHECK(DibChannel(normal, kPinProbeX, kPinProbeY, kChA) == kAlphaOpaque);
    CHECK(DibChannel(active, kPinProbeX, kPinProbeY, kChA) == kAlphaOpaque);
    CHECK(DibChannel(normal, 0, 0, kChA) == 0);
    //已固定是主题蓝（蓝通道明显高于未固定的深灰）
    CHECK(DibChannel(active, kPinProbeX, kPinProbeY, kChB) > DibChannel(normal, kPinProbeX, kPinProbeY, kChB));
    ::DeleteObject(normal);
    ::DeleteObject(active);

    const std::wstring pinnedTip = flamingomedia::PinCaptionTooltip(true);
    const std::wstring unpinnedTip = flamingomedia::PinCaptionTooltip(false);
    CHECK(!pinnedTip.empty());
    CHECK(!unpinnedTip.empty());
    CHECK(pinnedTip != unpinnedTip);
    CHECK(pinnedTip.find(L"取消") != std::wstring::npos);
}

TEST_CASE("WPN4 图钉图标方案 C 的造型：未固定是空心线框，已固定是实心并多一条底线（2026-10-06）", "[callux]")
{
    HBITMAP normal = flamingomedia::MakePinCaptionIcon(false);
    HBITMAP active = flamingomedia::MakePinCaptionIcon(true);
    REQUIRE(normal != NULL);
    REQUIRE(active != NULL);
    //钉身正中：线框是空心的，已固定的实心图钉在这里完全不透明
    CHECK(DibChannel(normal, kPinHollowX, kPinHollowY, kChA) < kPinHollowAlphaMax);
    CHECK(DibChannel(active, kPinHollowX, kPinHollowY, kChA) == kAlphaOpaque);
    //底线：只有已固定才有，且是清晰的一整行
    CHECK(DibChannel(active, kPinBaselineX, kPinBaselineY, kChA) == kAlphaOpaque);
    CHECK(DibChannel(normal, kPinBaselineX, kPinBaselineY, kChA) == 0);
    //未固定的线框与标题栏三按钮的字形同色（预乘 alpha 下完全不透明的像素就是原色）
    CHECK(DibChannel(normal, kPinProbeX, kPinProbeY, kChR) == kPinGlyphGray);
    CHECK(DibChannel(normal, kPinProbeX, kPinProbeY, kChG) == kPinGlyphGray);
    CHECK(DibChannel(normal, kPinProbeX, kPinProbeY, kChB) == kPinGlyphGray);
    ::DeleteObject(normal);
    ::DeleteObject(active);
}

TEST_CASE("CDL1 对方画面超过 2 秒没有新帧才算停止，没收到过画面不算；来电窗第二行按通话类型写", "[callux]")
{
    const int64_t base = 100000;
    const int64_t stale = call::kRemoteVideoStaleMs;
    //还没收到过画面：不算停止（本来就显示等待提示与头像）
    CHECK_FALSE(call::IsRemoteVideoStale(base + stale * 10, 0, stale));
    //刚收到、正好到判定时长：都还不算
    CHECK_FALSE(call::IsRemoteVideoStale(base, base, stale));
    CHECK_FALSE(call::IsRemoteVideoStale(base + stale, base, stale));
    //超过判定时长：算停止
    CHECK(call::IsRemoteVideoStale(base + stale + 1, base, stale));

    const CString video = call::MakeIncomingSubText(true);
    const CString voice = call::MakeIncomingSubText(false);
    CHECK(video.Find(L"视频") >= 0);
    CHECK(voice.Find(L"语音") >= 0);
    CHECK(video != voice);
}

TEST_CASE("ICL1 来电窗按 420x200 的设计尺寸排得下：头像不被压扁，文字在头像右侧，按钮贴底且不越界", "[callux]")
{
    //按钮位图缓存须比控件树活得久：先声明它，控件树后析构
    call::CallButtonArtCache art;
    flamingomedia::IncomingCallBody body = flamingomedia::BuildIncomingCallBody(
        std::unique_ptr<balloonwjui::DuiAvatar>(new balloonwjui::DuiAvatar()), L"张三丰",
        std::wstring((LPCWSTR)call::MakeIncomingSubText(true)), kIclDecline, kIclAccept, art);
    REQUIRE(body.m_root);
    REQUIRE(body.m_avatar != NULL);
    REQUIRE(body.m_line1 != NULL);
    REQUIRE(body.m_line2 != NULL);
    CHECK(body.m_line1->GetText() == CString(L"张三丰"));

    //内容区 = 整窗扣掉标题栏
    const int contentW = flamingomedia::kIncomingDlgW;
    const int contentH = flamingomedia::kIncomingDlgH - flamingomedia::kIncomingTitleBarH;
    const UINT buttonIds[] = { kIclDecline, kIclAccept };
    for (size_t i = 0; i < sizeof(buttonIds) / sizeof(buttonIds[0]); ++i)
    {
        const layoutfit::FitResult r = layoutfit::MeasureAt(body.m_root.get(), buttonIds[i], contentW, contentH);
        REQUIRE(r.m_found);
        CHECK(r.m_bottom <= contentH);
        CHECK(r.m_height == r.m_roomyHeight);
        CHECK(r.m_bottomGap == r.m_roomyBottomGap);
    }

    //MeasureAt 最后一次按设计尺寸排版，下面读到的矩形就是实际尺寸下的
    const RECT avatar = body.m_avatar->GetRect();
    CHECK(avatar.right - avatar.left == flamingomedia::kIncomingAvatarPx);
    CHECK(avatar.bottom - avatar.top == flamingomedia::kIncomingAvatarPx);
    const RECT line1 = body.m_line1->GetRect();
    const RECT line2 = body.m_line2->GetRect();
    CHECK(line1.left >= avatar.right);
    CHECK(line1.right <= contentW);
    CHECK(line1.right > line1.left);
    CHECK(line2.top >= line1.bottom);
    CHECK(line2.bottom <= avatar.bottom);
    //按钮在头像下方，「接听」在「拒绝」右边；2026-10-05 起两个都是圆形图标按钮：正方形、贴着位图、没有文字
    balloonwjui::DuiButton* declineBtn = static_cast<balloonwjui::DuiButton*>(body.m_root->FindCtrlById(kIclDecline));
    balloonwjui::DuiButton* acceptBtn = static_cast<balloonwjui::DuiButton*>(body.m_root->FindCtrlById(kIclAccept));
    const RECT decline = declineBtn->GetRect();
    const RECT accept = acceptBtn->GetRect();
    CHECK(decline.top >= avatar.bottom);
    CHECK(accept.left >= decline.right);
    CHECK(accept.right <= contentW);
    CHECK(decline.right - decline.left == call::kCallRoundBtnPx);
    CHECK(decline.bottom - decline.top == call::kCallRoundBtnPx);
    CHECK(accept.right - accept.left == call::kCallRoundBtnPx);
    CHECK(accept.bottom - accept.top == call::kCallRoundBtnPx);
    CHECK(declineBtn->GetBgBitmapForCurrentState() != NULL);
    CHECK(acceptBtn->GetBgBitmapForCurrentState() != NULL);
    CHECK(declineBtn->GetText().IsEmpty());
    CHECK(acceptBtn->GetText().IsEmpty());

    //不给头像：同样大小的空白，结构不变
    flamingomedia::IncomingCallBody noAvatar = flamingomedia::BuildIncomingCallBody(
        std::unique_ptr<balloonwjui::DuiAvatar>(), L"群名", L"3 人正在通话", kIclDecline, kIclAccept, art);
    REQUIRE(noAvatar.m_root);
    CHECK(noAvatar.m_avatar == NULL);
    CHECK(noAvatar.m_root->FindCtrlById(kIclAccept) != NULL);
}

TEST_CASE("CVB1 调试提示条：设了提示时画面顶部是一条琥珀色提示条，下方不受影响，清空后恢复；语音态与视频态都画", "[callux]")
{
    GdiplusScope gdiplus;
    //取色点：提示条内靠左（避开居中的文字），以及画面底部（提示条之外）
    const int probeX = 8;
    const int probeTopY = 4;
    const int probeBottomY = kCvaH - 4;
    const COLORREF black = RGB(0, 0, 0);
    call::CallVideoCanvas canvas;
    const RECT rc = { 0, 0, kCvaW, kCvaH };
    canvas.ForceLayout(rc);

    //视频态、还没有画面：黑底
    CHECK(NearColor(PaintCanvasAndProbe(canvas, probeX, probeTopY), black));
    canvas.SetBannerText(L"调试模式：未使用麦克风和扬声器");
    CHECK(NearColor(PaintCanvasAndProbe(canvas, probeX, probeTopY), call::kCanvasBannerBgColor));
    CHECK(NearColor(PaintCanvasAndProbe(canvas, probeX, probeBottomY), black));
    canvas.SetBannerText(NULL);
    CHECK(NearColor(PaintCanvasAndProbe(canvas, probeX, probeTopY), black));

    //语音态同样画在顶部
    canvas.SetVoiceArt(true);
    const COLORREF voiceTop = PaintCanvasAndProbe(canvas, probeX, probeTopY);
    CHECK_FALSE(NearColor(voiceTop, call::kCanvasBannerBgColor));
    canvas.SetBannerText(L"调试模式：未使用麦克风和扬声器");
    CHECK(NearColor(PaintCanvasAndProbe(canvas, probeX, probeTopY), call::kCanvasBannerBgColor));
    canvas.SetBannerText(L"");
    CHECK(NearColor(PaintCanvasAndProbe(canvas, probeX, probeTopY), voiceTop));
}

TEST_CASE("CVA1 语音态画面：设了头像时圆形区域里画头像，没设时不是头像的颜色", "[callux]")
{
    GdiplusScope gdiplus;
    call::CallVideoCanvas canvas;
    const RECT rc = { 0, 0, kCvaW, kCvaH };
    canvas.ForceLayout(rc);
    canvas.SetVoiceArt(true);
    canvas.SetPlaceholderText(L"语音通话中");
    const call::VoiceArtLayout l = call::ComputeVoiceArtLayout(rc);
    const int cx = (l.m_circle.left + l.m_circle.right) / 2;
    const int cy = (l.m_circle.top + l.m_circle.bottom) / 2;

    //没设头像：画的是听筒，圆心处不是头像的颜色
    CHECK_FALSE(NearColor(PaintCanvasAndProbe(canvas, cx, cy), kCvaAvatarColor));

    HBITMAP avatar = MakeSolidDib(kCvaAvatarPx, kCvaAvatarPx, kCvaAvatarColor);
    REQUIRE(avatar != NULL);
    canvas.SetAvatar(avatar);
    CHECK(canvas.GetAvatar() == avatar);
    CHECK(NearColor(PaintCanvasAndProbe(canvas, cx, cy), kCvaAvatarColor));

    //取消头像：回到听筒
    canvas.SetAvatar(NULL);
    CHECK_FALSE(NearColor(PaintCanvasAndProbe(canvas, cx, cy), kCvaAvatarColor));
    ::DeleteObject(avatar);
}

TEST_CASE("CVA2 视频态画面：没有画面时画头像，来了画面只画画面，画面清空后头像重新出现", "[callux]")
{
    call::CallVideoCanvas canvas;
    const RECT rc = { 0, 0, kCvaW, kCvaH };
    canvas.ForceLayout(rc);
    canvas.SetPlaceholderText(L"摄像头已关闭");
    HBITMAP avatar = MakeSolidDib(kCvaAvatarPx, kCvaAvatarPx, kCvaAvatarColor);
    REQUIRE(avatar != NULL);
    canvas.SetAvatar(avatar);
    //头像在上、提示文字在下（视频态的紧凑版式）
    const call::VoiceArtLayout l = call::ComputeAvatarTipLayout(rc);
    const int cx = (l.m_circle.left + l.m_circle.right) / 2;
    const int cy = (l.m_circle.top + l.m_circle.bottom) / 2;
    CHECK(NearColor(PaintCanvasAndProbe(canvas, cx, cy), kCvaAvatarColor));

    //来了一帧纯绿的画面：整块画面铺满，圆心处是画面的颜色
    std::vector<uint8_t> frame(static_cast<size_t>(kCvaFrameW) * kCvaFrameH * kBytesPerPx, 0);
    for (int i = 0; i < kCvaFrameW * kCvaFrameH; ++i)
    {
        frame[i * kBytesPerPx + kChG] = kCvaFrameG;
        frame[i * kBytesPerPx + kChA] = kAlphaOpaque;
    }
    canvas.SetFrame(&frame[0], kCvaFrameW, kCvaFrameH);
    const COLORREF withFrame = PaintCanvasAndProbe(canvas, cx, cy);
    CHECK_FALSE(NearColor(withFrame, kCvaAvatarColor));
    CHECK(NearColor(withFrame, RGB(0, kCvaFrameG, 0)));

    //画面清空（对方关了摄像头、画面停止）：头像重新出现
    canvas.SetFrame(NULL, 0, 0);
    CHECK(NearColor(PaintCanvasAndProbe(canvas, cx, cy), kCvaAvatarColor));

    //没设头像：黑底，圆心处是黑色
    canvas.SetAvatar(NULL);
    CHECK(NearColor(PaintCanvasAndProbe(canvas, cx, cy), RGB(0, 0, 0)));
    ::DeleteObject(avatar);
}

TEST_CASE("ICL2 改标题栏高度会丢掉已放进去的客户区内容，来电窗要先设标题栏高度再放内容", "[callux]")
{
    //与改之前的通话窗相同：先按呼叫态的标题栏高度建窗口（不可见，不在屏幕上闪过）
    const int oldTitleBarH = flamingomedia::kIncomingTitleBarH * 2;
    //按钮位图缓存须比窗口里的控件树活得久：先于窗口声明
    call::CallButtonArtCache art;
    balloonwjui::DuiFrameWindow frame;
    RECT rc = { 0, 0, flamingomedia::kIncomingDlgW, flamingomedia::kIncomingDlgH };
    REQUIRE(frame.Create(NULL, rc, _T("CallUxTests"), WS_OVERLAPPEDWINDOW) != NULL);
    frame.SetTitleBarHeight(oldTitleBarH);

    //先放内容、再改标题栏高度：DuiFrameWindow 重建骨架，内容丢失（2026-10-05 联调时来电窗只剩标题栏即由此而来）
    flamingomedia::IncomingCallBody first = flamingomedia::BuildIncomingCallBody(
        std::unique_ptr<balloonwjui::DuiAvatar>(new balloonwjui::DuiAvatar()), L"张三丰", L"邀请你语音通话",
        kIclDecline, kIclAccept, art);
    frame.SetClientContent(std::move(first.m_root));
    REQUIRE(frame.GetClientContent() != NULL);
    REQUIRE(frame.GetClientContent()->FindCtrlById(kIclAccept) != NULL);
    frame.SetTitleBarHeight(flamingomedia::kIncomingTitleBarH);
    balloonwjui::DuiControl* lost = frame.GetClientContent();
    CHECK((lost == NULL || lost->FindCtrlById(kIclAccept) == NULL));

    //先改标题栏高度、再放内容（CBalloonCallDlg::OpenIncoming 的顺序）：内容保留；高度不变时再设一次也不重建
    flamingomedia::IncomingCallBody second = flamingomedia::BuildIncomingCallBody(
        std::unique_ptr<balloonwjui::DuiAvatar>(new balloonwjui::DuiAvatar()), L"张三丰", L"邀请你语音通话",
        kIclDecline, kIclAccept, art);
    frame.SetClientContent(std::move(second.m_root));
    frame.SetTitleBarHeight(flamingomedia::kIncomingTitleBarH);
    REQUIRE(frame.GetClientContent() != NULL);
    CHECK(frame.GetClientContent()->FindCtrlById(kIclAccept) != NULL);
    CHECK(frame.GetClientContent()->FindCtrlById(kIclDecline) != NULL);
    frame.DestroyWindow();
}


TEST_CASE("WPN3 ApplyWindowTopmost：置顶后有 WS_EX_TOPMOST，取消后没有；无效窗口返回 false", "[callux]")
{
    //不可见的普通窗口即可（不在屏幕上闪过）
    balloonwjui::DuiFrameWindow frame;
    RECT rc = { 0, 0, flamingomedia::kIncomingDlgW, flamingomedia::kIncomingDlgH };
    REQUIRE(frame.Create(NULL, rc, _T("CallUxTestsTopmost"), WS_OVERLAPPEDWINDOW) != NULL);
    CHECK(flamingomedia::ApplyWindowTopmost(frame.m_hWnd, true));
    CHECK((::GetWindowLong(frame.m_hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
    //已经置顶时再置顶一次（内部会先取消再置顶）：仍是置顶
    CHECK(flamingomedia::ApplyWindowTopmost(frame.m_hWnd, true));
    CHECK_FALSE(flamingomedia::ApplyWindowTopmost(frame.m_hWnd, false));
    CHECK((::GetWindowLong(frame.m_hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0);
    frame.DestroyWindow();
    CHECK_FALSE(flamingomedia::ApplyWindowTopmost(NULL, true));
}


TEST_CASE("CDL2 视频态「头像 + 提示」版式：小画面里头像比语音态版式大，提示在头像下方且不越出区域", "[callux]")
{
    //左下角小画面、大画面与过小区域的尺寸（像素）：与通话窗视频态的小画面、大画面一致
    const int pipW = 160;
    const int pipH = 90;
    const int bigW = 720;
    const int bigH = 405;
    const int tinySide = 20;
    const RECT pip = { 0, 0, pipW, pipH };
    const call::VoiceArtLayout tip = call::ComputeAvatarTipLayout(pip);
    const call::VoiceArtLayout voice = call::ComputeVoiceArtLayout(pip);
    const int tipD = tip.m_circle.right - tip.m_circle.left;
    CHECK(tipD > voice.m_circle.right - voice.m_circle.left);
    CHECK(tipD == tip.m_circle.bottom - tip.m_circle.top);
    CHECK(tip.m_circle.top >= pip.top);
    CHECK(tip.m_text.top >= tip.m_circle.bottom);
    CHECK(tip.m_text.bottom <= pip.bottom);
    //水平居中
    CHECK(tip.m_circle.left - pip.left == pip.right - tip.m_circle.right);

    //大画面：头像取满 kVoiceArtCircle，整组竖直居中
    const RECT big = { 0, 0, bigW, bigH };
    const call::VoiceArtLayout bigTip = call::ComputeAvatarTipLayout(big);
    CHECK(bigTip.m_circle.right - bigTip.m_circle.left == call::kVoiceArtCircle);
    const int above = bigTip.m_circle.top - big.top;
    const int below = big.bottom - bigTip.m_text.bottom;
    CHECK(abs(above - below) <= 1);

    //区域太小：头像直径为 0，不出负数
    const RECT tiny = { 0, 0, tinySide, tinySide };
    const call::VoiceArtLayout tinyTip = call::ComputeAvatarTipLayout(tiny);
    CHECK(tinyTip.m_circle.right - tinyTip.m_circle.left == 0);
}


namespace {

//VEN1：测试画面的尺寸、帧率、码率与帧数（编出的码流尺寸与引擎解码器的预设尺寸 1280×720 不同，顺带验证按码流重新协商）
const int kVenW = 320;
const int kVenH = 240;
const int kVenFps = 15;
const int kVenBitrate = 300000;
const int kVenFrames = 30;
//NV12 的 Y 平面亮度与 UV 平面取值（中灰、无色）
const uint8_t kVenLuma = 0x80;
const uint8_t kVenChroma = 0x80;
//一秒的 100ns 计数（帧时间戳 = 帧序号 × 此值 / 帧率）
const int64_t kVenHnsPerSecond = 10000000LL;

//收集编码器输出的 H.264 包
class VenPacketCollector : public call::MfH264EncoderSink
{
public:
    virtual void OnEncodedH264(const uint8_t* data, size_t len, bool keyframe, int64_t ptsHns) override
    {
        (void)keyframe;
        m_packets.push_back(std::vector<uint8_t>(data, data + len));
        m_pts.push_back(ptsHns);
    }

    std::vector<std::vector<uint8_t> > m_packets;   //按输出顺序排列的包
    std::vector<int64_t>               m_pts;       //每个包的时间戳（100ns）
};

//记下视频引擎交付了几帧远端画面、几帧本地预览、几个编码帧
class VenEngineSink : public call::CallVideoEngineSink
{
public:
    VenEngineSink()
        : m_remote(0)
        , m_local(0)
        , m_encoded(0)
        , m_lastW(0)
        , m_lastH(0)
    {
    }

    virtual void OnEncodedVideo(const uint8_t* data, size_t len, bool keyframe, int64_t ptsHns) override
    {
        (void)data;
        (void)len;
        (void)keyframe;
        (void)ptsHns;
        ++m_encoded;
    }

    virtual void OnLocalVideoFrame(const uint8_t* nv12, int width, int height, int stride) override
    {
        (void)nv12;
        (void)width;
        (void)height;
        (void)stride;
        ++m_local;
    }

    virtual void OnRemoteVideoFrame(const uint8_t* nv12, int width, int height, int stride) override
    {
        (void)nv12;
        (void)stride;
        ++m_remote;
        m_lastW = width;
        m_lastH = height;
    }

    int m_remote;    //交付的远端画面帧数
    int m_local;     //交付的本地预览帧数
    int m_encoded;   //送出的编码帧数
    int m_lastW;     //最近一帧远端画面的宽
    int m_lastH;     //最近一帧远端画面的高
};

} // namespace

TEST_CASE("VEN1 视频引擎只开接收、不开摄像头也能解码对方画面，停止接收后不再交付", "[callux]")
{
    //编解码 MFT 要在初始化了 COM 与 MF 的线程上使用（与通话的视频解码线程相同）
    const HRESULT hrCo = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    REQUIRE(SUCCEEDED(::MFStartup(MF_VERSION, MFSTARTUP_LITE)));

    //先编出一段码流，当作「对方发来的画面」
    VenPacketCollector collector;
    {
        call::MfH264Encoder encoder;
        REQUIRE(encoder.Init(kVenW, kVenH, kVenFps, kVenBitrate));
        std::vector<uint8_t> nv12(static_cast<size_t>(kVenW) * kVenH * 3 / 2, kVenChroma);
        memset(&nv12[0], kVenLuma, static_cast<size_t>(kVenW) * kVenH);
        for (int i = 0; i < kVenFrames; ++i)
            encoder.EncodeNv12(&nv12[0], kVenW, i * kVenHnsPerSecond / kVenFps, &collector);
    }
    REQUIRE_FALSE(collector.m_packets.empty());

    //只开接收：不打开摄像头，照样把对方的包解码交付
    VenEngineSink sink;
    call::CallVideoEngine engine;
    REQUIRE(engine.StartReceive(&sink));
    CHECK(engine.IsReceiving());
    CHECK_FALSE(engine.IsCameraOn());
    for (size_t i = 0; i < collector.m_packets.size(); ++i)
        engine.OnRemoteEncodedVideo(&collector.m_packets[i][0], collector.m_packets[i].size(), collector.m_pts[i]);
    CHECK(sink.m_remote > 0);
    CHECK(sink.m_lastW == kVenW);
    CHECK(sink.m_lastH == kVenH);
    //没开摄像头：没有本地预览，也没有送出编码帧
    CHECK(sink.m_local == 0);
    CHECK(sink.m_encoded == 0);

    //停止接收：再喂包也不交付
    engine.StopReceive();
    CHECK_FALSE(engine.IsReceiving());
    CHECK_FALSE(engine.IsRunning());
    const int delivered = sink.m_remote;
    for (size_t i = 0; i < collector.m_packets.size(); ++i)
        engine.OnRemoteEncodedVideo(&collector.m_packets[i][0], collector.m_packets[i].size(), collector.m_pts[i]);
    CHECK(sink.m_remote == delivered);

    //关摄像头可重复调用；没登记事件去向时不会去开摄像头
    engine.StopCamera();
    call::CallVideoEngine bare;
    CHECK_FALSE(bare.StartCamera(kVenFps, kVenBitrate));
    CHECK_FALSE(bare.IsCameraOn());

    ::MFShutdown();
    if (SUCCEEDED(hrCo))
        ::CoUninitialize();
}


namespace {

//CCD：一张语音通话卡片在呼叫时的 content，以及改写后的几种（主叫 5127「张勇」，被叫 5308）
const char* const kCcdStartVoice =
    "{\"callee\":\"5308\",\"caller\":\"5127\",\"callername\":\"\xE5\xBC\xA0\xE5\x8B\x87\",\"kind\":\"call\",\"media\":1,"
    "\"sessionid\":\"5127-5308-1-1\",\"starttime\":\"1791167956000\"}";
const char* const kCcdEndedVideo171 =
    "{\"callee\":\"5308\",\"caller\":\"5127\",\"callername\":\"\xE5\xBC\xA0\xE5\x8B\x87\",\"duration\":171,"
    "\"endtime\":\"1791168130000\",\"kind\":\"call\",\"media\":2,\"result\":7,\"sessionid\":\"5127-5308-1-1\","
    "\"starttime\":\"1791167956000\"}";
//改写后的结束方式（与协议 call_state 一致）：拒绝、取消、超时、结束
const int kCcdRejected = 2;
const int kCcdCanceled = 3;
const int kCcdTimeout = 5;
const int kCcdEnded = 7;
//CCD2 里用的时长：2 分 51 秒，与「通话时长 02:51」对应
const int64_t kCcdDuration = 171;
const int64_t kCcdMsgId = 9001;

//只带结束方式（没有时长）的卡片
balloonchatwnd::CallCardInfo EndedWithout(int result)
{
    balloonchatwnd::CallCardInfo info;
    info.m_media = balloonchatwnd::kCallCardVoice;
    info.m_ended = true;
    info.m_result = result;
    return info;
}

balloonchatwnd::ChatMessage CcdBubble(bool fromMe)
{
    balloonchatwnd::ChatMessage msg;
    msg.kind = balloonchatwnd::kMsgText;
    msg.fromMe = fromMe;
    msg.msgid = kCcdMsgId;
    return msg;
}

} // namespace

TEST_CASE("CCD1 一对一通话卡片的解析：呼叫时的字段、改写后的结果与时长；不是一对一卡片返回 false", "[callux]")
{
    balloonchatwnd::CallCardInfo info;
    REQUIRE(balloonchatwnd::ParseCallCard(kCcdStartVoice, info));
    CHECK(info.m_sessionId == "5127-5308-1-1");
    CHECK(info.m_media == balloonchatwnd::kCallCardVoice);
    CHECK(info.m_callerId == 5127);
    CHECK(info.m_calleeId == 5308);
    CHECK(info.m_callerName == CString(L"张勇"));
    CHECK_FALSE(info.m_ended);
    CHECK_FALSE(info.m_connected);
    CHECK_FALSE(balloonchatwnd::IsCallCardEnded(kCcdStartVoice));

    balloonchatwnd::CallCardInfo ended;
    REQUIRE(balloonchatwnd::ParseCallCard(kCcdEndedVideo171, ended));
    CHECK(ended.m_media == balloonchatwnd::kCallCardVideo);
    CHECK(ended.m_ended);
    CHECK(ended.m_result == kCcdEnded);
    CHECK(ended.m_connected);
    CHECK(ended.m_durationSeconds == kCcdDuration);
    CHECK(balloonchatwnd::IsCallCardEnded(kCcdEndedVideo171));

    //群通话卡片、坏内容、空串都不是一对一通话卡片
    CHECK_FALSE(balloonchatwnd::ParseCallCard("{\"kind\":\"groupcall\",\"media\":1}", info));
    CHECK_FALSE(balloonchatwnd::ParseCallCard("not json", info));
    CHECK_FALSE(balloonchatwnd::ParseCallCard("", info));
}

TEST_CASE("CCD2 已结束卡片的状态文字：按结束方式与主叫被叫区分，接通过的写时长", "[callux]")
{
    using balloonchatwnd::CallCardStatusText;
    CHECK(CallCardStatusText(EndedWithout(kCcdRejected), true, 0) == CString(L"对方已拒绝"));
    CHECK(CallCardStatusText(EndedWithout(kCcdRejected), false, 0) == CString(L"已拒绝"));
    CHECK(CallCardStatusText(EndedWithout(kCcdCanceled), true, 0) == CString(L"已取消"));
    CHECK(CallCardStatusText(EndedWithout(kCcdCanceled), false, 0) == CString(L"对方已取消"));
    CHECK(CallCardStatusText(EndedWithout(kCcdTimeout), true, 0) == CString(L"对方未接听"));
    CHECK(CallCardStatusText(EndedWithout(kCcdTimeout), false, 0) == CString(L"未接听"));
    //结束但没接通过（如响铃中掉线超过宽限期）
    CHECK(CallCardStatusText(EndedWithout(kCcdEnded), true, 0) == CString(L"未接通"));
    //接通过：主叫被叫都写时长；结束后此刻是否在通话中不影响
    balloonchatwnd::CallCardInfo connected = EndedWithout(kCcdEnded);
    connected.m_connected = true;
    connected.m_durationSeconds = kCcdDuration;
    CHECK(CallCardStatusText(connected, true, 0) == CString(L"通话时长 02:51"));
    CHECK(CallCardStatusText(connected, false, balloonchatwnd::kCallCardVoice) == CString(L"通话时长 02:51"));
    //未知的结束方式
    CHECK(CallCardStatusText(EndedWithout(99), true, 0) == CString(L"通话已结束"));
}

TEST_CASE("CCD3 还没结束的卡片：在通话中写「通话中」，否则主叫写「正在呼叫」、被叫写「邀请你通话」", "[callux]")
{
    balloonchatwnd::CallCardInfo ringing;
    REQUIRE(balloonchatwnd::ParseCallCard(kCcdStartVoice, ringing));
    CHECK(balloonchatwnd::CallCardStatusText(ringing, true, 0) == CString(L"正在呼叫"));
    CHECK(balloonchatwnd::CallCardStatusText(ringing, false, 0) == CString(L"邀请你通话"));
    CHECK(balloonchatwnd::CallCardStatusText(ringing, true, balloonchatwnd::kCallCardVoice) == CString(L"通话中"));
    CHECK(balloonchatwnd::CallCardStatusText(ringing, false, balloonchatwnd::kCallCardVideo) == CString(L"通话中"));
}

TEST_CASE("CCD4 套用到气泡：标题、摘要、副标题、媒体类型、没有按钮；内容不可用时仍是卡片", "[callux]")
{
    //本人是主叫：副标题「我发起的通话」
    balloonchatwnd::ChatMessage mine = CcdBubble(true);
    balloonchatwnd::ApplyCallCard(mine, kCcdEndedVideo171, CString(L"韩晓燕"), 0);
    CHECK(mine.kind == balloonchatwnd::kMsgScreenShare);
    CHECK(mine.screenShareIsCallCard);
    CHECK_FALSE(mine.screenShareIsGroupCall);
    CHECK(mine.groupCallMedia == balloonchatwnd::kCallCardVideo);
    CHECK(mine.text == CString(L"[视频通话]"));
    CHECK(mine.screenShareTitle == CString(L"视频通话"));
    CHECK(mine.screenShareSubtitle == CString(L"我发起的通话"));
    CHECK(mine.screenShareStatus == CString(L"通话时长 02:51"));
    CHECK_FALSE(mine.screenShareHasButton);
    CHECK(mine.screenShareContent == std::string(kCcdEndedVideo171));
    CHECK(mine.msgid == kCcdMsgId);

    //对方是主叫：副标题用对方的显示名；没给显示名时用卡片里的主叫昵称
    balloonchatwnd::ChatMessage other = CcdBubble(false);
    balloonchatwnd::ApplyCallCard(other, kCcdStartVoice, CString(L"张总"), balloonchatwnd::kCallCardVoice);
    CHECK(other.text == CString(L"[语音通话]"));
    CHECK(other.screenShareTitle == CString(L"语音通话"));
    CHECK(other.screenShareSubtitle == CString(L"张总发起的通话"));
    CHECK(other.screenShareStatus == CString(L"通话中"));
    balloonchatwnd::ChatMessage noName = CcdBubble(false);
    balloonchatwnd::ApplyCallCard(noName, kCcdStartVoice, CString(), 0);
    CHECK(noName.screenShareSubtitle == CString(L"张勇发起的通话"));

    //内容不可用：仍是一张语音通话卡片，状态写「通话已结束」
    balloonchatwnd::ChatMessage broken = CcdBubble(false);
    balloonchatwnd::ApplyCallCard(broken, "garbage", CString(), 0);
    CHECK(broken.screenShareIsCallCard);
    CHECK(broken.screenShareTitle == CString(L"语音通话"));
    CHECK(broken.screenShareStatus == CString(L"通话已结束"));
    CHECK(broken.screenShareSubtitle == CString(L"对方发起的通话"));
}

TEST_CASE("CCD5 1717 推送的解析；会话列表摘要写「[语音通话]」「[视频通话]」，群通话卡片不变", "[callux]")
{
    net::CallCardNotify notify;
    REQUIRE(net::MessageProtocolClient::parseCallCardNotify(
        "{\"sessionid\":\"s1\",\"msgid\":\"9001\",\"cid\":\"5308\",\"content\":{\"kind\":\"call\",\"media\":2,\"result\":7}}",
        notify));
    CHECK(notify.m_sessionId == "s1");
    CHECK(notify.m_msgid == kCcdMsgId);
    CHECK(notify.m_cid == 5308);
    balloonchatwnd::CallCardInfo info;
    REQUIRE(balloonchatwnd::ParseCallCard(notify.m_content, info));
    CHECK(info.m_media == balloonchatwnd::kCallCardVideo);
    //msgid 或 cid 缺失、content 不是对象：不可用
    CHECK_FALSE(net::MessageProtocolClient::parseCallCardNotify("{\"cid\":\"5308\",\"content\":{}}", notify));
    CHECK_FALSE(net::MessageProtocolClient::parseCallCardNotify("{\"msgid\":\"1\",\"content\":{}}", notify));
    CHECK_FALSE(net::MessageProtocolClient::parseCallCardNotify("{\"msgid\":\"1\",\"cid\":\"2\",\"content\":\"x\"}", notify));

    CHECK(net::MessageProtocolClient::groupCallDigest(kCcdStartVoice) == std::string(u8"[语音通话]"));
    CHECK(net::MessageProtocolClient::groupCallDigest(kCcdEndedVideo171) == std::string(u8"[视频通话]"));
    CHECK(net::MessageProtocolClient::groupCallDigest("{\"kind\":\"groupcall\",\"media\":2}") == std::string(u8"[群视频通话]"));
    CHECK(net::MessageProtocolClient::groupCallDigest("{\"kind\":\"other\"}") == std::string(u8"[通话]"));
}


TEST_CASE("CLY1 呼叫中态与接通后同一副骨架：默认尺寸排得下、画面区与接通后的语音态位置一致、只有「取消」且贴右下；最大化后仍如此", "[callux]")
{
    const UINT canvasId = 12;
    const UINT cancelId = 11;
    //按钮位图缓存须比控件树活得久：先声明它，控件树后析构
    call::CallButtonArtCache art;
    call::CallingBody body = call::BuildCallingBody(canvasId, cancelId, art);
    REQUIRE(body.m_root);
    REQUIRE(body.m_canvas != NULL);
    CHECK(body.m_root->FindCtrlById(canvasId) == body.m_canvas);
    balloonwjui::DuiControl* cancelCtrl = body.m_root->FindCtrlById(cancelId);
    REQUIRE(cancelCtrl != NULL);
    //2026-10-05 起「取消」是红色圆形图标按钮：贴着位图、没有文字，「取消」改作悬停提示
    balloonwjui::DuiButton* cancelBtn = static_cast<balloonwjui::DuiButton*>(cancelCtrl);
    CHECK(cancelBtn->GetBgBitmapForCurrentState() != NULL);
    CHECK(cancelBtn->GetText().IsEmpty());
    CHECK(call::TooltipForAction(call::kActionCancel, false, false) == L"取消");
    //接通态底部栏的其它按钮（拒绝、小画面、切换媒体、摄像头、共享屏幕、麦克风、音量）呼叫中都没有
    const UINT otherIds[] = { 10, 13, 14, 15, 16, 17, 18 };
    for (size_t i = 0; i < sizeof(otherIds) / sizeof(otherIds[0]); ++i)
    {
        INFO("ctrlId = " << otherIds[i]);
        CHECK(body.m_root->FindCtrlById(otherIds[i]) == NULL);
    }

    //默认尺寸：内容区 = 776 ×（画面区 405 + 底部栏 120），即整窗 776×569 扣掉 44 的标题栏（2026-10-06 由 720 加宽）
    const int contentW = call::kCallDlgWidth;
    const int contentH = call::kCallCanvasH + call::kCallBottomBarH;
    CHECK(contentH + call::kCallDlgTitleBarH == 569);
    const layoutfit::FitResult r = layoutfit::MeasureAt(body.m_root.get(), cancelId, contentW, contentH);
    REQUIRE(r.m_found);
    CHECK(r.m_bottom <= contentH);
    CHECK(r.m_height == r.m_roomyHeight);

    //画面区：四周留 12（底边紧接底部栏），与接通后语音态的画面区同一位置，对方接听后不跳动
    const RECT canvas = body.m_canvas->GetRect();
    CHECK(canvas.left == call::kCallArtMargin);
    CHECK(canvas.top == call::kCallArtMargin);
    CHECK(canvas.right == contentW - call::kCallArtMargin);
    CHECK(canvas.bottom == call::kCallCanvasH);

    //「取消」：56×56 的圆形按钮，右边距 24，在底部栏里竖直居中（与接通态「挂断」同一位置、同一尺寸）
    const int btn = call::kCallRoundBtnLargePx;
    const RECT cancel = cancelCtrl->GetRect();
    CHECK(cancel.right - cancel.left == btn);
    CHECK(cancel.bottom - cancel.top == btn);
    CHECK(cancel.right == contentW - call::kCallSideMargin);
    CHECK(cancel.top == call::kCallCanvasH + (call::kCallBottomBarH - btn) / 2);

    //最大化（如 1920x1000 的内容区）：画面区跟着变大，「取消」仍贴右下、尺寸不变
    const int bigW = 1920;
    const int bigH = 1000;
    RECT big = { 0, 0, bigW, bigH };
    body.m_root->ForceLayout(big);
    const RECT bigCanvas = body.m_canvas->GetRect();
    const RECT bigCancel = cancelCtrl->GetRect();
    CHECK(bigCanvas.right == bigW - call::kCallArtMargin);
    CHECK(bigCanvas.bottom == bigH - call::kCallBottomBarH);
    CHECK(bigCancel.right == bigW - call::kCallSideMargin);
    CHECK(bigCancel.right - bigCancel.left == btn);
    CHECK(bigCancel.top == bigH - call::kCallBottomBarH + (call::kCallBottomBarH - btn) / 2);
}
