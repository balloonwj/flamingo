/**
 *  「屏幕录制」图标的单元测试（标签 [recordicon]），RecordIconTests.cpp
 *  背景：2026-10-06 用户要求重新设计录制图标，两轮候选后选定「场记板」：上方是带两道斜纹的打板，板身里一个实心圆点
 *  （候选效果图在 tmp/rec_icon/）。图标有两份画法，聊天窗工具栏的 kChatIconRecord（ChatIcons.cpp）与系统菜单、托盘菜单的
 *  kIconRecord（MainPanelIcons.cpp），两份须保持同一造型。
 *  覆盖：
 *    - RIC1 工具栏图标与菜单图标是同一造型（按同样的尺寸与笔宽画出来几乎逐像素一致）；
 *    - RIC2 工具栏图标是场记板：打板顶边横贯整个宽度、板身两侧与底边有墨、板身中下部的圆点有墨，且与视频通话的摄像机明显不同；
 *    - RIC3 菜单图标同样是场记板，且与「录制管理」「发起屏幕共享」明显不同；
 *    - RICS 效果图（标签 [.][snapshot][recordicon]，默认不运行）：设了环境变量 FLAMINGO_SNAPSHOT_PATH 时存成 record_icon.png。
 *  图标按 viewBox 24 画到 48 像素见方的透明位图里（放大 2 倍），检查点都取在线条中心，避开抗锯齿的边缘。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <vector>
#include <gdiplus.h>

#include "../UI/BalloonChatWnd/ChatIcons.h"
#include "../UI/BalloonMain/MainPanelIcons.h"

namespace {

//渲染边长（像素）：viewBox 24 放大 2 倍，viewBox 里的坐标乘 2 即像素坐标
const int kRicPx = 48;
//描边宽度（viewBox 单位），与工具栏按钮、菜单图标的实际取值相同
const Gdiplus::REAL kRicStroke = 2.0f;
//透明度超过这个值的像素算作「有墨」
const BYTE kRicInkAlpha = 64;
//判定两个图标「同一造型」时允许的最多差异像素：两份画法的辅助函数不同，抗锯齿边缘可能差几个像素
const int kRicMaxSameDiff = 12;
//判定两个图标「明显不同」时的最少差异像素
const int kRicMinDiff = 60;

//检查点（像素坐标，均为 viewBox 坐标乘 2）
//打板顶边 y=5.5 → 11，取左、中、右三点
const int kTopEdgeY = 11;
const int kTopEdgeXs[3] = { 10, 24, 38 };
//板身底边 y=20 → 40，取左、中、右三点
const int kBottomEdgeY = 40;
const int kBottomEdgeXs[3] = { 12, 24, 36 };
//板身左右两侧 x=3.5 → 7 与 x=20.5 → 41，取 y=17 → 34
const int kSideY = 34;
const int kLeftSideX = 7;
const int kRightSideX = 41;
//板身中下部的圆点，圆心 (12, 15) → (24, 30)
const int kDotX = 24;
const int kDotY = 30;

//在测试期间启动 GDI+，析构时关闭
class RicScopedGdiplus
{
public:
    RicScopedGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    ~RicScopedGdiplus()
    {
        if (m_ok)
            Gdiplus::GdiplusShutdown(m_token);
    }

    //GDI+ 是否启动成功
    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   //GdiplusStartup 返回的令牌，关闭时交回
    bool      m_ok;      //是否启动成功
};

//把位图逐像素转成「是否有墨」
std::vector<bool> InkOf(Gdiplus::Bitmap& bmp)
{
    std::vector<bool> ink;
    for (int y = 0; y < kRicPx; ++y)
    {
        for (int x = 0; x < kRicPx; ++x)
        {
            Gdiplus::Color c;
            bmp.GetPixel(x, y, &c);
            ink.push_back(c.GetA() > kRicInkAlpha);
        }
    }
    return ink;
}

//用工具栏的画法（DrawChatIcon）画一个图标
std::vector<bool> RenderChatIcon(int kind)
{
    Gdiplus::Bitmap bmp(kRicPx, kRicPx, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        balloonchatwnd::DrawChatIcon(g, kind, 0.0f, 0.0f, (Gdiplus::REAL)kRicPx, Gdiplus::Color(255, 0, 0, 0), kRicStroke);
    }
    return InkOf(bmp);
}

//用菜单的画法（DrawLineIcon）画一个图标
std::vector<bool> RenderMenuIcon(int kind)
{
    Gdiplus::Bitmap bmp(kRicPx, kRicPx, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        balloonmain::DrawLineIcon(g, kind, 0.0f, 0.0f, (Gdiplus::REAL)kRicPx, Gdiplus::Color(255, 0, 0, 0), kRicStroke);
    }
    return InkOf(bmp);
}

//某个像素是否有墨
bool InkAt(const std::vector<bool>& ink, int x, int y)
{
    return ink[static_cast<size_t>(y * kRicPx + x)];
}

//两个图标不同的像素数
int CountDiff(const std::vector<bool>& a, const std::vector<bool>& b)
{
    int n = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
    {
        if (a[i] != b[i])
            ++n;
    }
    return n;
}

//检查场记板的几处特征，每处单独断言，失败时能看出缺的是哪一处
void CheckClapperboard(const std::vector<bool>& ink)
{
    for (int i = 0; i < 3; ++i)
    {
        INFO("clapper top edge at x=" << kTopEdgeXs[i]);
        CHECK(InkAt(ink, kTopEdgeXs[i], kTopEdgeY));
    }
    for (int i = 0; i < 3; ++i)
    {
        INFO("board bottom edge at x=" << kBottomEdgeXs[i]);
        CHECK(InkAt(ink, kBottomEdgeXs[i], kBottomEdgeY));
    }
    CHECK(InkAt(ink, kLeftSideX, kSideY));
    CHECK(InkAt(ink, kRightSideX, kSideY));
    CHECK(InkAt(ink, kDotX, kDotY));
}

} // namespace

TEST_CASE("RIC1 工具栏与菜单的录制图标是同一造型", "[recordicon]")
{
    RicScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    const std::vector<bool> toolbar = RenderChatIcon(balloonchatwnd::kChatIconRecord);
    const std::vector<bool> menu = RenderMenuIcon(balloonmain::kIconRecord);
    CHECK(CountDiff(toolbar, menu) <= kRicMaxSameDiff);
}

TEST_CASE("RIC2 工具栏的录制图标是场记板，与视频通话的摄像机不同", "[recordicon]")
{
    RicScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    const std::vector<bool> record = RenderChatIcon(balloonchatwnd::kChatIconRecord);
    CheckClapperboard(record);
    CHECK(CountDiff(record, RenderChatIcon(balloonchatwnd::kChatIconVideo)) >= kRicMinDiff);
    CHECK(CountDiff(record, RenderChatIcon(balloonchatwnd::kChatIconScreenShare)) >= kRicMinDiff);
}

TEST_CASE("RIC3 菜单的录制图标是场记板，与录制管理、发起屏幕共享不同", "[recordicon]")
{
    RicScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    const std::vector<bool> record = RenderMenuIcon(balloonmain::kIconRecord);
    CheckClapperboard(record);
    CHECK(CountDiff(record, RenderMenuIcon(balloonmain::kIconRecordList)) >= kRicMinDiff);
    CHECK(CountDiff(record, RenderMenuIcon(balloonmain::kIconScreenShare)) >= kRicMinDiff);
}

namespace {

//RICS 效果图：画布尺寸（像素）
const int kRicsSheetW = 560;
const int kRicsSheetH = 200;
//工具栏图标的显示尺寸：100% 为 18 像素，125% 为 22.5 像素（与 ChatIconButton.cpp 的 kIconSize 相同）
const Gdiplus::REAL kRicsToolbar100 = 18.0f;
const Gdiplus::REAL kRicsToolbar125 = 22.5f;
//菜单图标的显示尺寸（像素），按 125% 缩放下菜单里的大小近似取值
const Gdiplus::REAL kRicsMenuIcon = 20.0f;
//放大图的尺寸（像素）
const Gdiplus::REAL kRicsZoom = 96.0f;
//图标之间的水平间距与两行的纵坐标（像素）
const Gdiplus::REAL kRicsStep = 40.0f;
const Gdiplus::REAL kRicsRow1Y = 30.0f;
const Gdiplus::REAL kRicsRow2Y = 100.0f;
const Gdiplus::REAL kRicsLeft = 20.0f;
//放大图的左上角
const Gdiplus::REAL kRicsZoomX = 420.0f;
const Gdiplus::REAL kRicsZoomY = 40.0f;
//工具栏图标的颜色（与 ChatIconButton.cpp 的 kIconColor 相同）
const Gdiplus::Color kRicsInk(255, 0x6B, 0x6B, 0x6B);

//在 GDI+ 已注册的图像编码器里找 PNG 编码器的 CLSID
bool RicsPngEncoder(CLSID& out)
{
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0)
        return false;
    std::vector<BYTE> buf(bytes);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok)
        return false;
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

} // namespace

//效果图（默认不运行）：设了环境变量 FLAMINGO_SNAPSHOT_PATH（一个目录）时，把工具栏与菜单里的新录制图标按实际画法画进
//record_icon.png，供人工核对。第一行是工具栏：100% 与 125% 下的「剪刀、屏幕录制、共享屏幕」；第二行是菜单：「屏幕录制、
//录制管理、发起屏幕共享」；右侧是放大 4 倍的工具栏录制图标
TEST_CASE("RICS record icon snapshot", "[.][snapshot][recordicon]")
{
    wchar_t dir[MAX_PATH] = { 0 };
    if (::GetEnvironmentVariableW(L"FLAMINGO_SNAPSHOT_PATH", dir, MAX_PATH) == 0)
    {
        SUCCEED("FLAMINGO_SNAPSHOT_PATH not set, snapshot skipped");
        return;
    }
    RicScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    Gdiplus::Bitmap sheet(kRicsSheetW, kRicsSheetH, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&sheet);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.Clear(Gdiplus::Color(255, 255, 255, 255));
        const int toolbar[3] = { balloonchatwnd::kChatIconCut, balloonchatwnd::kChatIconRecord,
                                 balloonchatwnd::kChatIconScreenShare };
        for (int i = 0; i < 3; ++i)
        {
            balloonchatwnd::DrawChatIcon(g, toolbar[i], kRicsLeft + kRicsStep * i, kRicsRow1Y,
                                         kRicsToolbar100, kRicsInk, kRicStroke);
            balloonchatwnd::DrawChatIcon(g, toolbar[i], kRicsLeft + kRicsStep * (i + 4), kRicsRow1Y,
                                         kRicsToolbar125, kRicsInk, kRicStroke);
        }
        const int menu[3] = { balloonmain::kIconRecord, balloonmain::kIconRecordList, balloonmain::kIconScreenShare };
        for (int i = 0; i < 3; ++i)
        {
            balloonmain::DrawLineIcon(g, menu[i], kRicsLeft + kRicsStep * i, kRicsRow2Y, kRicsMenuIcon,
                                      kRicsInk, kRicStroke);
        }
        balloonchatwnd::DrawChatIcon(g, balloonchatwnd::kChatIconRecord, kRicsZoomX, kRicsZoomY, kRicsZoom,
                                     kRicsInk, kRicStroke);
    }
    CLSID png;
    REQUIRE(RicsPngEncoder(png));
    const std::wstring path = std::wstring(dir) + L"/record_icon.png";
    CHECK(sheet.Save(path.c_str(), &png, NULL) == Gdiplus::Ok);
}
