/**
 *  LoginAccountAvatarTests.cpp
 *
 *  登录窗账号头像（UI/BalloonAuth/LoginAccountAvatar）的测试，标签 [loginavatar]（2026-10-04 起）：
 *    LAV1 本机没登录过的账号：账号首字、默认底色（调色板第 0 个），不画照片；
 *    LAV2 这次升级之前存下的账号（还没有 userid）：姓名首字（没有姓名时账号首字），底色按账号取；
 *    LAV3 字形头像：选定的字形与调色板底色；
 *    LAV4 自动头像：姓名首字，底色按 userid 取，与主面板同一口径；
 *    LAV5 照片头像：照片在本机时画照片，不在本机时画姓名首字；
 *    LAV6 位图：同一头像同一边长只生成一次，照片解码失败时改画字形，Clear 后全部释放；
 *    LAV7 填下拉框：每项文字为账号，浮层里带头像位图与姓名副文字（没有姓名的不带），行高与头像边长已设好；
 *    LAB1 大头像默认画法：没有照片，主题橙底色；
 *    LAB2 大头像画当前账号的照片：头像方块中心是照片的颜色；
 *    LAB3 照片不存在时画底色与字形；换到没有照片的账号后不再画照片；
 *    LAP1 状态胶囊加宽放下下拉小三角，带细边框，水平居中、在大头像下方；
 *    LAP2 鼠标移入状态胶囊：悬停态、手形光标、登记提示「点击切换登录状态」、底色加深；移出或离开控件后恢复。
 *  只在内存里建控件与位图，不创建窗口。框架 Catch2 v3；全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonAuth/LoginAccountAvatar.h"
#include "../UI/BalloonMain/AvatarSpec.h"
#include "../UI/BalloonMain/AvatarPalette.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../UI/BalloonAuth/AuthBanner.h"   // LAB1~LAB3、LAP1~LAP2：大头像与状态胶囊
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   // LAP2：状态胶囊的悬停提示

#include <gdiplus.h>
#include <stdlib.h>   // abs
#include <string.h>   // memset / strcpy_s
#include <vector>

namespace {

// 测试账号的 userid
const long long kLavUserId = 5127;
// 头像类型：自动、字形、照片（见 AvatarSpec.h 的 AvatarKind）
const int kLavFaceAuto = 0;
const int kLavFaceGlyph = 1;
const int kLavFacePhoto = 2;
// 字形头像的头像内容「狐|6」（UTF-8 字节）与它的色槽
const char* const kLavGlyphFace = "\xE7\x8B\x90|6";
const int kLavGlyphColorIndex = 6;
// 一个照片文件 id
const char* const kLavPhotoFace = "9f86d081884c7d65";
// 位图边长，单位：像素
const int kLavAvatarPx = 28;
// 照片路径桩返回的路径：文件并不存在，生成位图时会解码失败
const wchar_t* const kLavMissingPhoto = L"C:\\flamingo_lav_no_such_dir\\photo.png";

// 造一条本机账号记录
LOGIN_ACCOUNT_INFO MakeAccount(const wchar_t* user, const wchar_t* nick, long long userId, int faceType,
                               const char* customFace)
{
    LOGIN_ACCOUNT_INFO info;
    memset(&info, 0, sizeof(info));
    wcsncpy_s(info.szUser, _countof(info.szUser), user, _TRUNCATE);
    wcsncpy_s(info.szNickName, _countof(info.szNickName), nick, _TRUNCATE);
    info.nUserId = userId;
    info.nFaceType = faceType;
    strncpy_s(info.szCustomFace, _countof(info.szCustomFace), customFace, _TRUNCATE);
    return info;
}

// 照片路径桩：照片头像一律当作已下载到本机，返回 kLavMissingPhoto；其它头像返回空串
std::wstring StubPhotoPath(const CString& account, const LOGIN_ACCOUNT_INFO* info)
{
    (void)account;
    if (info != NULL && info->nFaceType == kLavFacePhoto)
        return kLavMissingPhoto;
    return std::wstring();
}

} // namespace

TEST_CASE("LAV1 本机没登录过的账号：账号首字与默认底色", "[loginavatar]")
{
    const balloonauth::LoginAvatarSpec spec = balloonauth::ResolveLoginAvatar(_T("alice"), NULL, std::wstring());
    CHECK(spec.m_glyph == _T("a"));
    CHECK(spec.m_color == balloonmain::AvatarColorByIndex(0));
    CHECK(spec.m_photoPath.empty());
}

TEST_CASE("LAV2 还没有 userid 的老账号：姓名首字，底色按账号取", "[loginavatar]")
{
    const LOGIN_ACCOUNT_INFO withNick = MakeAccount(L"alice", L"张三", 0, kLavFaceAuto, "");
    const balloonauth::LoginAvatarSpec a = balloonauth::ResolveLoginAvatar(_T("alice"), &withNick, std::wstring());
    CHECK(a.m_glyph == _T("张"));
    CHECK(a.m_color == balloonmain::AvatarColorForText(_T("alice")));

    // 没有姓名：账号首字
    const LOGIN_ACCOUNT_INFO noNick = MakeAccount(L"bob", L"", 0, kLavFaceAuto, "");
    const balloonauth::LoginAvatarSpec b = balloonauth::ResolveLoginAvatar(_T("bob"), &noNick, std::wstring());
    CHECK(b.m_glyph == _T("b"));
    CHECK(b.m_color == balloonmain::AvatarColorForText(_T("bob")));
}

TEST_CASE("LAV3 字形头像：选定的字形与调色板底色", "[loginavatar]")
{
    const LOGIN_ACCOUNT_INFO info = MakeAccount(L"alice", L"张三", kLavUserId, kLavFaceGlyph, kLavGlyphFace);
    const balloonauth::LoginAvatarSpec spec = balloonauth::ResolveLoginAvatar(_T("alice"), &info, std::wstring());
    CHECK(spec.m_glyph == L"\u72D0");
    CHECK(spec.m_color == balloonmain::AvatarColorByIndex(kLavGlyphColorIndex));
    CHECK(spec.m_photoPath.empty());
}

TEST_CASE("LAV4 自动头像：姓名首字，底色按 userid 取", "[loginavatar]")
{
    const LOGIN_ACCOUNT_INFO info = MakeAccount(L"alice", L"张三", kLavUserId, kLavFaceAuto, "");
    const balloonauth::LoginAvatarSpec spec = balloonauth::ResolveLoginAvatar(_T("alice"), &info, std::wstring());
    CHECK(spec.m_glyph == _T("张"));
    CHECK(spec.m_color == balloonmain::AvatarColorForUser(kLavUserId));
    // 与主面板同一口径
    const balloonmain::AvatarSpec panel = balloonmain::DecodeAvatar(kLavFaceAuto, std::string(), _T("张三"), kLavUserId);
    CHECK(spec.m_glyph == panel.m_glyph);
    CHECK(spec.m_color == panel.m_color);
}

TEST_CASE("LAV5 照片头像：照片在本机时画照片，不在时画姓名首字", "[loginavatar]")
{
    const LOGIN_ACCOUNT_INFO info = MakeAccount(L"alice", L"张三", kLavUserId, kLavFacePhoto, kLavPhotoFace);
    const balloonauth::LoginAvatarSpec local = balloonauth::ResolveLoginAvatar(_T("alice"), &info, kLavMissingPhoto);
    CHECK(local.m_photoPath == kLavMissingPhoto);
    // 照片画不出来时备用的字形
    CHECK(local.m_glyph == _T("张"));

    const balloonauth::LoginAvatarSpec absent = balloonauth::ResolveLoginAvatar(_T("alice"), &info, std::wstring());
    CHECK(absent.m_photoPath.empty());
    CHECK(absent.m_glyph == _T("张"));
    CHECK(absent.m_color == balloonmain::AvatarColorForUser(kLavUserId));

    // 不是照片头像时，调用方即便给了路径也不画照片
    const LOGIN_ACCOUNT_INFO glyph = MakeAccount(L"bob", L"李四", kLavUserId, kLavFaceGlyph, kLavGlyphFace);
    CHECK(balloonauth::ResolveLoginAvatar(_T("bob"), &glyph, kLavMissingPhoto).m_photoPath.empty());
}

TEST_CASE("LAV6 位图：同一头像只生成一次，照片解码失败时改画字形", "[loginavatar]")
{
    balloonauth::CLoginAvatarBitmaps bitmaps;
    balloonauth::LoginAvatarSpec glyph;
    glyph.m_glyph = _T("张");
    glyph.m_color = balloonmain::AvatarColorByIndex(kLavGlyphColorIndex);
    HBITMAP first = bitmaps.GetBitmap(glyph, kLavAvatarPx);
    REQUIRE(first != NULL);
    CHECK(bitmaps.GetBitmap(glyph, kLavAvatarPx) == first);
    CHECK(bitmaps.Count() == 1);

    // 照片文件不存在：改画同一个字形，得到的就是上面那张
    balloonauth::LoginAvatarSpec photo = glyph;
    photo.m_photoPath = kLavMissingPhoto;
    CHECK(bitmaps.GetBitmap(photo, kLavAvatarPx) == first);

    CHECK(bitmaps.GetBitmap(glyph, 0) == NULL);
    bitmaps.Clear();
    CHECK(bitmaps.Count() == 0);
}

TEST_CASE("LAV7 填下拉框：每行账号、头像与姓名", "[loginavatar]")
{
    CLoginAccountList list;
    REQUIRE(list.Add(_T("alice"), _T(""), 1, FALSE, FALSE));
    REQUIRE(list.Add(_T("bob"), _T(""), 1, FALSE, FALSE));
    REQUIRE(list.Add(_T("carol"), _T(""), 1, FALSE, FALSE));
    REQUIRE(list.SetNickName(_T("alice"), L"张三"));
    REQUIRE(list.SetAvatar(_T("alice"), kLavUserId, kLavFaceGlyph, kLavGlyphFace));
    REQUIRE(list.SetNickName(_T("bob"), L"李四"));
    REQUIRE(list.SetAvatar(_T("bob"), kLavUserId + 1, kLavFacePhoto, kLavPhotoFace));
    // carol 是老账号：没有姓名，也没有头像信息

    balloonwjui::DuiComboBox combo;
    balloonauth::CLoginAvatarBitmaps bitmaps;
    CHECK(balloonauth::FillAccountCombo(&combo, &list, bitmaps, &StubPhotoPath) == 3);
    REQUIRE(combo.GetCount() == 3);
    CHECK(combo.GetItemHeight() == balloonauth::kAccountRowHeight);
    CHECK(combo.GetIconSize() == balloonauth::kAccountAvatarPx);

    CHECK(combo.GetItemText(0) == _T("alice"));
    CHECK(combo.GetItemSubText(0) == _T("张三"));
    CHECK(combo.GetItemSubText(1) == _T("李四"));
    CHECK(combo.GetItemSubText(2).IsEmpty());
    for (int i = 0; i < 3; ++i)
        CHECK(combo.GetItemIcon(i) != NULL);
    // 三个人的头像各不相同（字形「狐」、照片解码失败画「李」、账号首字「c」）
    CHECK(combo.GetItemIcon(0) != combo.GetItemIcon(1));
    CHECK(combo.GetItemIcon(1) != combo.GetItemIcon(2));

    // 下拉框与位图持有者都还在时先让下拉框不再引用位图，再释放
    combo.ResetContent();
    bitmaps.Clear();
}

// ---- 大头像（AuthBanner，2026-10-04 起按当前账号的头像画）----

namespace {

// 大头像用例的控件尺寸，单位：像素（与登录窗顶部 Banner 大致相当）
const int kLabBannerW = 380;
const int kLabBannerH = 260;
// 写进测试照片的纯色、字形头像的底色
const COLORREF kLabPhotoColor = RGB(0x20, 0xA0, 0x60);
const COLORREF kLabGlyphColor = RGB(0x40, 0x60, 0xD0);
// 大头像默认的主题橙，与 AuthBanner.cpp 的 kAvatarFillR/G/B 相同
const COLORREF kLabThemeOrange = RGB(0xFF, 0x7A, 0x45);
// 测试照片的边长，单位：像素
const int kLabPhotoPx = 64;
// 取色时每个分量允许的误差（照片缩放的插值）
const int kLabTolerance = 3;
// 取「底色」像素的位置：头像方块顶部往下边长的这几分之一处（字形画在正中，不会碰到这里）
const int kLabTopProbeDivisor = 8;

// 在作用域内启动 GDI+，离开时关闭
class LabScopedGdiplus
{
public:
    // 启动 GDI+；失败时 Ok() 返回 false
    LabScopedGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    // 关闭 GDI+
    ~LabScopedGdiplus()
    {
        if (m_ok)
            Gdiplus::GdiplusShutdown(m_token);
    }

    // GDI+ 是否启动成功
    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌，关闭时要用
    bool      m_ok;      // 是否启动成功
};

// 临时目录下的文件路径，文件名带进程 id（并发跑的测试进程互不干扰），并删掉旧文件
std::wstring LabTempFile(const wchar_t* name)
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

// 写一张 kLabPhotoPx 见方的纯色 PNG。成功返回 true。需要 GDI+ 已启动
bool LabWriteSolidPng(const std::wstring& path, COLORREF color)
{
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0)
        return false;
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
        return false;
    Gdiplus::Bitmap bmp(kLabPhotoPx, kLabPhotoPx, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.Clear(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));
    }
    return bmp.Save(path.c_str(), png, NULL) == Gdiplus::Ok;
}

// 把大头像控件画到一张白底内存位图上，返回指定点的颜色；画失败时返回 CLR_INVALID
//   banner：已设好矩形的控件；points：要取色的点（控件坐标）；out：各点的颜色
bool LabPaintAndSample(balloonauth::AuthBanner& banner, const std::vector<POINT>& points, std::vector<COLORREF>& out)
{
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kLabBannerW;
    bi.bmiHeader.biHeight = -kLabBannerH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC dc = ::CreateCompatibleDC(NULL);
    HBITMAP bmp = ::CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (dc == NULL || bmp == NULL || bits == NULL)
        return false;
    HGDIOBJ old = ::SelectObject(dc, bmp);
    RECT rc = { 0, 0, kLabBannerW, kLabBannerH };
    ::FillRect(dc, &rc, (HBRUSH)::GetStockObject(WHITE_BRUSH));
    banner.OnPaint(dc, rc);
    const DWORD* px = static_cast<const DWORD*>(bits);
    out.clear();
    for (size_t i = 0; i < points.size(); ++i)
    {
        const DWORD v = px[points[i].y * kLabBannerW + points[i].x];
        out.push_back(RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF));
    }
    ::SelectObject(dc, old);
    ::DeleteObject(bmp);
    ::DeleteDC(dc);
    return true;
}

// 两种颜色每个分量都相差不超过 kLabTolerance
bool LabColorNear(COLORREF a, COLORREF b)
{
    return abs((int)GetRValue(a) - (int)GetRValue(b)) <= kLabTolerance
        && abs((int)GetGValue(a) - (int)GetGValue(b)) <= kLabTolerance
        && abs((int)GetBValue(a) - (int)GetBValue(b)) <= kLabTolerance;
}

} // namespace

TEST_CASE("LAB1 大头像默认画法：没有照片，主题橙底色", "[loginavatar][banner]")
{
    balloonauth::AuthBanner banner;
    CHECK_FALSE(banner.HasAvatarPhoto());
    CHECK(banner.GetAvatarColor() == kLabThemeOrange);
    CHECK(::IsRectEmpty(&banner.GetAvatarRect()));
}

TEST_CASE("LAB2 大头像画当前账号的照片", "[loginavatar][banner]")
{
    LabScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    const std::wstring photo = LabTempFile(L"flamingo_lab2.png");
    REQUIRE(LabWriteSolidPng(photo, kLabPhotoColor));

    balloonauth::AuthBanner banner;
    const RECT rc = { 0, 0, kLabBannerW, kLabBannerH };
    banner.SetRect(rc);
    banner.SetAvatar(_T("张"), kLabGlyphColor, photo.c_str());
    CHECK(banner.HasAvatarPhoto());

    // 先画一次得到头像方块的位置，再取方块中心的颜色：是照片的颜色，不是字形底色
    std::vector<POINT> none;
    std::vector<COLORREF> colors;
    REQUIRE(LabPaintAndSample(banner, none, colors));
    const RECT av = banner.GetAvatarRect();
    REQUIRE_FALSE(::IsRectEmpty(&av));
    std::vector<POINT> points(1);
    points[0].x = (av.left + av.right) / 2;
    points[0].y = (av.top + av.bottom) / 2;
    REQUIRE(LabPaintAndSample(banner, points, colors));
    CHECK(LabColorNear(colors[0], kLabPhotoColor));
    ::DeleteFileW(photo.c_str());
}

TEST_CASE("LAB3 照片不存在时画底色与字形；换到没有照片的账号后不再画照片", "[loginavatar][banner]")
{
    LabScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    balloonauth::AuthBanner banner;
    const RECT rc = { 0, 0, kLabBannerW, kLabBannerH };
    banner.SetRect(rc);
    banner.SetAvatar(_T("张"), kLabGlyphColor, L"C:\\flamingo_lab_no_such_dir\\photo.png");
    CHECK_FALSE(banner.HasAvatarPhoto());
    CHECK(banner.GetAvatarColor() == kLabGlyphColor);

    std::vector<POINT> none;
    std::vector<COLORREF> colors;
    REQUIRE(LabPaintAndSample(banner, none, colors));
    const RECT av = banner.GetAvatarRect();
    REQUIRE_FALSE(::IsRectEmpty(&av));
    std::vector<POINT> points(1);
    points[0].x = (av.left + av.right) / 2;
    points[0].y = av.top + (av.bottom - av.top) / kLabTopProbeDivisor;
    REQUIRE(LabPaintAndSample(banner, points, colors));
    CHECK(LabColorNear(colors[0], kLabGlyphColor));

    // 先切到有照片的账号，再切回没有照片的：不再画照片
    const std::wstring photo = LabTempFile(L"flamingo_lab3.png");
    REQUIRE(LabWriteSolidPng(photo, kLabPhotoColor));
    banner.SetAvatar(_T("张"), kLabGlyphColor, photo.c_str());
    CHECK(banner.HasAvatarPhoto());
    banner.SetAvatar(_T("李"), kLabGlyphColor, NULL);
    CHECK_FALSE(banner.HasAvatarPhoto());
    ::DeleteFileW(photo.c_str());
}

// ---- 状态胶囊（2026-10-04 起：细边框 + 小三角，悬停时底色加深、手形光标、提示可点击切换）----

namespace {

// 设计稿的高度与胶囊尺寸（与 AuthBanner.cpp 的 kPillHeightRatio、kPillWidthRatio 同一出处：按 240 高的设计稿）
const float kLapDesignH = 240.0f;
const float kLapOldPillW = 62.0f;   // 加小三角之前的胶囊宽度
// 悬停提示的文字，与 AuthBanner.cpp 的 kPillTipText 相同
const wchar_t* const kLapPillTip = L"点击切换登录状态";
// 胶囊外的一点：控件左上角往里一点
const int kLapOutsideXY = 2;
// 认为「比白色深」的阈值：三个分量都低于它
const int kLapNotWhite = 245;

// 像素是否不是（接近）白色
bool LapNotWhite(COLORREF c)
{
    return GetRValue(c) < kLapNotWhite || GetGValue(c) < kLapNotWhite || GetBValue(c) < kLapNotWhite;
}

// 三个分量之和，比较两点谁更深
int LapSum(COLORREF c)
{
    return (int)GetRValue(c) + (int)GetGValue(c) + (int)GetBValue(c);
}

} // namespace

TEST_CASE("LAP1 状态胶囊加宽放下小三角，带细边框，不压到大头像", "[loginavatar][pill]")
{
    LabScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    balloonauth::AuthBanner banner;
    const RECT rc = { 0, 0, kLabBannerW, kLabBannerH };
    banner.SetRect(rc);
    banner.SetUserName(_T("张三"));
    std::vector<POINT> none;
    std::vector<COLORREF> colors;
    REQUIRE(LabPaintAndSample(banner, none, colors));

    const RECT pill = banner.GetPillHitRect();
    const RECT av = banner.GetAvatarRect();
    REQUIRE_FALSE(::IsRectEmpty(&pill));
    // 比原来的宽：多出放小三角的一截
    CHECK((pill.right - pill.left) > (int)((float)kLabBannerH * kLapOldPillW / kLapDesignH) + 1);
    // 水平居中、在控件之内、在大头像下方
    CHECK(abs((pill.left + pill.right) / 2 - kLabBannerW / 2) <= 1);
    CHECK(pill.left >= 0);
    CHECK(pill.right <= kLabBannerW);
    CHECK(pill.top > av.bottom);

    // 细边框：胶囊上沿正中的像素不是白色；小三角：胶囊右侧（三角所在处）垂直方向中间有比白色深的像素
    const int pillH = pill.bottom - pill.top;
    std::vector<POINT> points(2);
    points[0].x = (pill.left + pill.right) / 2;
    points[0].y = pill.top;
    points[1].x = pill.right - pillH / 2;
    points[1].y = (pill.top + pill.bottom) / 2;
    REQUIRE(LabPaintAndSample(banner, points, colors));
    CHECK(LapNotWhite(colors[0]));
    CHECK(LapNotWhite(colors[1]));
}

TEST_CASE("LAP2 鼠标移入状态胶囊：悬停态、手形光标与提示；移出后恢复", "[loginavatar][pill]")
{
    LabScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    balloonauth::AuthBanner banner;
    const RECT rc = { 0, 0, kLabBannerW, kLabBannerH };
    banner.SetRect(rc);
    banner.SetUserName(_T("张三"));
    std::vector<POINT> none;
    std::vector<COLORREF> colors;
    REQUIRE(LabPaintAndSample(banner, none, colors));
    const RECT pill = banner.GetPillHitRect();
    REQUIRE_FALSE(::IsRectEmpty(&pill));
    // 胶囊里没有文字与图形的一点：左缘往里两像素、垂直居中（徽标从更靠右的位置开始画）
    POINT inside;
    inside.x = pill.left + 2;
    inside.y = (pill.top + pill.bottom) / 2;
    POINT outside;
    outside.x = kLapOutsideXY;
    outside.y = kLapOutsideXY;

    std::vector<POINT> probe(1, inside);
    REQUIRE(LabPaintAndSample(banner, probe, colors));
    const COLORREF normal = colors[0];
    CHECK_FALSE(banner.IsPillHovered());
    CHECK(balloonwjui::DuiToolTipMgr::Inst().GetText(&banner).IsEmpty());

    // 移入：悬停态、提示已登记、光标设为手形、底色比平时深
    banner.OnMouseMove(inside, 0);
    CHECK(banner.IsPillHovered());
    CHECK(balloonwjui::DuiToolTipMgr::Inst().GetText(&banner) == CString(kLapPillTip));
    CHECK(banner.OnSetCursor(inside));
    REQUIRE(LabPaintAndSample(banner, probe, colors));
    CHECK(LapSum(colors[0]) < LapSum(normal));

    // 在胶囊外移动：恢复，提示注销，光标不由本控件设置
    banner.OnMouseMove(outside, 0);
    CHECK_FALSE(banner.IsPillHovered());
    CHECK(balloonwjui::DuiToolTipMgr::Inst().GetText(&banner).IsEmpty());
    CHECK_FALSE(banner.OnSetCursor(outside));

    // 再移入后直接离开控件：同样恢复
    banner.OnMouseMove(inside, 0);
    CHECK(banner.IsPillHovered());
    banner.OnMouseLeave();
    CHECK_FALSE(banner.IsPillHovered());
    CHECK(balloonwjui::DuiToolTipMgr::Inst().GetText(&banner).IsEmpty());
}
