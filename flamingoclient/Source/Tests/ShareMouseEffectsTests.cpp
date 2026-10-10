/**
 *  屏幕共享与录屏的鼠标效果单元测试（标签 [mouseeffect]），ShareMouseEffectsTests.cpp
 *
 *  覆盖 FlamingoMedia/Share/ShareMouseEffects.h（docs/屏幕共享与录制方案.md 第 16 期，2026-10-03 与用户确认的用例）：
 *    - SSME1 坐标换算：屏幕坐标换算到画面坐标，画面与采集矩形大小不同时按比例换算，光标在采集矩形外时不画；
 *    - SSME2 轨迹：只保留最近 0.6 秒的点，到时清空；「效果还在变化」的判断；关掉轨迹或光标在画面外时不记点；
 *    - SSME3 光圈：圆心像素混进光圈颜色，圆外像素不变，只改光圈包围盒里的像素；关掉或光标在画面外时整幅不变；靠边时不越界；
 *    - SSME4 点击效果：只在按下的那一刻触发，左键红、右键蓝，圆环随时间变大，0.4 秒后消失；关掉时不触发；
 *    - SSME5 预设颜色与设置：默认只开光圈、光圈黄轨迹红，五种颜色的名字，设置写成文本再读回，坏数据回到默认值；
 *    - SSME6 菜单模型：三个开关与两个颜色子菜单的文字与打勾，点了之后设置怎么变，不认识的 id 不改设置；
 *    - SSME7 物理按键换算成逻辑按键：交换了主次按钮时左右对调（16b）；
 *    - SSME8 推流线程里的效果一环：光标动、效果在淡出、设置变了才多出一帧，静止时不出（16b）；
 *    - SSME9 要编码时把效果画进画面副本，原始画面不改；没有可见效果时原样返回（16b）；
 *    - SSME10 账号配置里的鼠标效果设置：没有时为空（按默认设置），保存后读回原文（16c）；
 *    - SSME11 录制画面在屏幕上的矩形：显示器矩形加上裁剪矩形（16d）；
 *    - SSME12 独立录屏的鼠标效果：按录制区域换算光标、就地画光圈，区域外与关掉时不画（16d）；
 *    - SSME14 颜色菜单项的图标：透明底、同色方块带加深细边，当前颜色的方块里有白色对勾（第 16 期补充）。
 *  框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[mouseeffect]"
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../../FlamingoMedia/Share/ShareMouseEffects.h"
#include "../Session/UserConfig.h"   // SSME10：账号配置里的鼠标效果设置
#include "../../FlamingoMedia/Record/RecordMouseEffects.h"   // SSME11、SSME12：独立录屏的鼠标效果

using namespace screenshare;

namespace {

//测试画面的宽高（像素）与每像素字节数
const int kSmeFrameW = 100;
const int kSmeFrameH = 100;
const int kSmeBytesPerPixel = 4;
//白色的分量
const uint8_t kSmeWhite = 255;
//菜单项 id 的起点
const UINT kSmeIdBase = 1000;

//一幅全白的 BGRA 画面
std::vector<uint8_t> smeWhiteFrame()
{
    return std::vector<uint8_t>(static_cast<size_t>(kSmeFrameW) * kSmeFrameH * kSmeBytesPerPixel, kSmeWhite);
}

//取 (x, y) 处的 B、G、R
void smePixel(const std::vector<uint8_t>& frame, int x, int y, int& b, int& g, int& r)
{
    const size_t i = (static_cast<size_t>(y) * kSmeFrameW + x) * kSmeBytesPerPixel;
    b = frame[i];
    g = frame[i + 1];
    r = frame[i + 2];
}

//(x, y) 处是否还是白色
bool smeIsWhite(const std::vector<uint8_t>& frame, int x, int y)
{
    int b = 0;
    int g = 0;
    int r = 0;
    smePixel(frame, x, y, b, g, r);
    return b == kSmeWhite && g == kSmeWhite && r == kSmeWhite;
}

POINT smePt(int x, int y)
{
    POINT pt;
    pt.x = x;
    pt.y = y;
    return pt;
}

//只开某一种效果的设置
ShareMouseEffectSettings smeOnly(bool halo, bool click, bool trail)
{
    ShareMouseEffectSettings s;
    s.m_halo = halo;
    s.m_click = click;
    s.m_trail = trail;
    return s;
}

} // namespace

TEST_CASE("SSME1 屏幕坐标换算到画面坐标，采集矩形外不画", "[mouseeffect]")
{
    RECT source;
    ::SetRect(&source, 100, 50, 1100, 650);   // 1000 x 600
    POINT out;
    REQUIRE(MapScreenToFrame(smePt(100, 50), source, 1000, 600, out));
    CHECK(out.x == 0);
    CHECK(out.y == 0);
    REQUIRE(MapScreenToFrame(smePt(600, 350), source, 1000, 600, out));
    CHECK(out.x == 500);
    CHECK(out.y == 300);
    REQUIRE(MapScreenToFrame(smePt(1099, 649), source, 1000, 600, out));
    CHECK(out.x == 999);
    CHECK(out.y == 599);
    //采集矩形外（含右下边）
    CHECK_FALSE(MapScreenToFrame(smePt(99, 50), source, 1000, 600, out));
    CHECK_FALSE(MapScreenToFrame(smePt(1100, 300), source, 1000, 600, out));
    CHECK_FALSE(MapScreenToFrame(smePt(600, 650), source, 1000, 600, out));
    //画面是采集矩形的一半：按比例换算
    REQUIRE(MapScreenToFrame(smePt(600, 350), source, 500, 300, out));
    CHECK(out.x == 250);
    CHECK(out.y == 150);
    //矩形或画面为空
    RECT empty;
    ::SetRect(&empty, 0, 0, 0, 0);
    CHECK_FALSE(MapScreenToFrame(smePt(0, 0), empty, 1000, 600, out));
    CHECK_FALSE(MapScreenToFrame(smePt(600, 350), source, 0, 0, out));
}

TEST_CASE("SSME2 轨迹只保留最近 0.6 秒的点，到时清空", "[mouseeffect]")
{
    ShareMouseEffects effects;
    effects.SetSettings(smeOnly(false, false, true));
    effects.Update(smePt(10, 10), true, false, false, 0);
    effects.Update(smePt(20, 10), true, false, false, 100);
    effects.Update(smePt(30, 10), true, false, false, 200);
    CHECK(effects.TrailPointCount(200) == 3);
    CHECK(effects.IsAnimating(200));
    //t = 0 的点超过 0.6 秒，不再算
    CHECK(effects.TrailPointCount(650) == 2);
    CHECK(effects.IsAnimating(700));
    //停下 0.6 秒之后全部消失，不再需要出帧
    CHECK(effects.TrailPointCount(200 + kShareMouseTrailLifeMs + 1) == 0);
    CHECK_FALSE(effects.IsAnimating(200 + kShareMouseTrailLifeMs + 1));
    //轨迹点全部过期之后光标仍停在原处：不再记新点（16b 联测发现过期后会把静止的光标再记成新点，一直需要出帧）
    effects.Update(smePt(30, 10), true, false, false, 200 + kShareMouseTrailLifeMs + 1);
    CHECK(effects.TrailPointCount(200 + kShareMouseTrailLifeMs + 1) == 0);
    CHECK_FALSE(effects.IsAnimating(200 + kShareMouseTrailLifeMs + 1));
    //再动一下：照常记点
    effects.Update(smePt(31, 10), true, false, false, 200 + kShareMouseTrailLifeMs + 40);
    CHECK(effects.TrailPointCount(200 + kShareMouseTrailLifeMs + 40) == 1);

    //光标在画面外：不记点
    ShareMouseEffects outside;
    outside.SetSettings(smeOnly(false, false, true));
    outside.Update(smePt(10, 10), false, false, false, 0);
    CHECK(outside.TrailPointCount(0) == 0);

    //关掉轨迹：不记点；开着时关掉，已记的点随之清空
    ShareMouseEffects off;
    off.SetSettings(smeOnly(false, false, false));
    off.Update(smePt(10, 10), true, false, false, 0);
    CHECK(off.TrailPointCount(0) == 0);
    effects.Update(smePt(40, 10), true, false, false, 300);
    effects.SetSettings(smeOnly(false, false, false));
    CHECK(effects.TrailPointCount(300) == 0);

    //画轨迹：路径经过的像素被画上颜色
    ShareMouseEffects drawn;
    drawn.SetSettings(smeOnly(false, false, true));
    drawn.Update(smePt(10, 50), true, false, false, 0);
    drawn.Update(smePt(90, 50), true, false, false, 50);
    std::vector<uint8_t> frame = smeWhiteFrame();
    drawn.Render(&frame[0], kSmeFrameW, kSmeFrameH, kSmeFrameW * kSmeBytesPerPixel, 60);
    CHECK_FALSE(smeIsWhite(frame, 50, 50));
    CHECK(smeIsWhite(frame, 50, 20));
}

TEST_CASE("SSME3 光圈：圆心混进光圈颜色，圆外不变，关掉或在画面外时整幅不变", "[mouseeffect]")
{
    ShareMouseEffects effects;
    effects.SetSettings(smeOnly(true, false, false));
    effects.Update(smePt(50, 50), true, false, false, 0);
    std::vector<uint8_t> frame = smeWhiteFrame();
    effects.Render(&frame[0], kSmeFrameW, kSmeFrameH, kSmeFrameW * kSmeBytesPerPixel, 0);
    //黄色光圈：圆心的蓝分量明显降低，红绿分量仍高
    int b = 0;
    int g = 0;
    int r = 0;
    smePixel(frame, 50, 50, b, g, r);
    CHECK(b < kSmeWhite - 50);
    CHECK(r > 200);
    CHECK(g > 150);
    //圆外与包围盒外不变
    CHECK(smeIsWhite(frame, 50 + kShareMouseHaloRadius + 2, 50));
    for (int y = 0; y < kSmeFrameH; ++y)
    {
        for (int x = 0; x < kSmeFrameW; ++x)
        {
            const bool inBox = x >= 50 - kShareMouseHaloRadius - 1 && x <= 50 + kShareMouseHaloRadius + 1 &&
                               y >= 50 - kShareMouseHaloRadius - 1 && y <= 50 + kShareMouseHaloRadius + 1;
            if (!inBox && !smeIsWhite(frame, x, y))
            {
                FAIL("pixel outside the halo box changed: " << x << "," << y);
            }
        }
    }

    //关掉光圈：整幅不变
    ShareMouseEffects off;
    off.SetSettings(smeOnly(false, false, false));
    off.Update(smePt(50, 50), true, false, false, 0);
    std::vector<uint8_t> clean = smeWhiteFrame();
    off.Render(&clean[0], kSmeFrameW, kSmeFrameH, kSmeFrameW * kSmeBytesPerPixel, 0);
    CHECK(clean == smeWhiteFrame());
    //光标在画面外：整幅不变
    ShareMouseEffects outside;
    outside.SetSettings(smeOnly(true, false, false));
    outside.Update(smePt(50, 50), false, false, false, 0);
    outside.Render(&clean[0], kSmeFrameW, kSmeFrameH, kSmeFrameW * kSmeBytesPerPixel, 0);
    CHECK(clean == smeWhiteFrame());

    //靠在左上角：不越界，角上的像素被画上
    ShareMouseEffects corner;
    corner.SetSettings(smeOnly(true, false, false));
    corner.Update(smePt(1, 1), true, false, false, 0);
    std::vector<uint8_t> edge = smeWhiteFrame();
    corner.Render(&edge[0], kSmeFrameW, kSmeFrameH, kSmeFrameW * kSmeBytesPerPixel, 0);
    CHECK_FALSE(smeIsWhite(edge, 0, 0));

    //换成蓝色光圈：圆心的红分量明显降低
    ShareMouseEffectSettings blue = smeOnly(true, false, false);
    blue.m_haloColor = share_mouse_color_blue;
    effects.SetSettings(blue);
    std::vector<uint8_t> blueFrame = smeWhiteFrame();
    effects.Render(&blueFrame[0], kSmeFrameW, kSmeFrameH, kSmeFrameW * kSmeBytesPerPixel, 0);
    smePixel(blueFrame, 50, 50, b, g, r);
    CHECK(r < kSmeWhite - 50);
    CHECK(b > 200);
}

TEST_CASE("SSME4 点击效果：按下那一刻触发，左红右蓝，圆环变大，0.4 秒后消失", "[mouseeffect]")
{
    ShareMouseEffects effects;
    effects.SetSettings(smeOnly(false, true, false));
    effects.Update(smePt(50, 50), true, true, false, 0);
    CHECK(effects.ActiveClickCount(0) == 1);
    //一直按着不再触发
    effects.Update(smePt(50, 50), true, true, false, 50);
    CHECK(effects.ActiveClickCount(50) == 1);
    //松开再按：第二个
    effects.Update(smePt(50, 50), true, false, false, 100);
    effects.Update(smePt(50, 50), true, true, false, 150);
    CHECK(effects.ActiveClickCount(150) == 2);
    //第一个在 0.4 秒后消失，第二个还在
    CHECK(effects.ActiveClickCount(kShareMouseClickLifeMs + 50) == 1);
    CHECK(effects.IsAnimating(kShareMouseClickLifeMs + 50));
    CHECK(effects.ActiveClickCount(150 + kShareMouseClickLifeMs + 1) == 0);

    //左键红：圆环在一半时间时的半径为起止半径的中点，圆环上的像素偏红；圆心不画
    const int64_t half = kShareMouseClickLifeMs / 2;
    const int midRadius = (kShareMouseClickStartRadius + kShareMouseClickEndRadius) / 2;
    ShareMouseEffects left;
    left.SetSettings(smeOnly(false, true, false));
    left.Update(smePt(50, 50), true, true, false, 0);
    std::vector<uint8_t> frame = smeWhiteFrame();
    left.Render(&frame[0], kSmeFrameW, kSmeFrameH, kSmeFrameW * kSmeBytesPerPixel, half);
    int b = 0;
    int g = 0;
    int r = 0;
    smePixel(frame, 50 + midRadius, 50, b, g, r);
    CHECK(r > b + 50);
    CHECK(r > g + 50);
    CHECK(smeIsWhite(frame, 50, 50));
    //右键蓝
    ShareMouseEffects right;
    right.SetSettings(smeOnly(false, true, false));
    right.Update(smePt(50, 50), true, false, true, 0);
    std::vector<uint8_t> blue = smeWhiteFrame();
    right.Render(&blue[0], kSmeFrameW, kSmeFrameH, kSmeFrameW * kSmeBytesPerPixel, half);
    smePixel(blue, 50 + midRadius, 50, b, g, r);
    CHECK(b > r + 50);

    //关掉点击效果：不触发
    ShareMouseEffects off;
    off.SetSettings(smeOnly(true, false, false));
    off.Update(smePt(50, 50), true, true, false, 0);
    CHECK(off.ActiveClickCount(0) == 0);
    //光标在画面外按下：不触发
    ShareMouseEffects outside;
    outside.SetSettings(smeOnly(false, true, false));
    outside.Update(smePt(50, 50), false, true, false, 0);
    CHECK(outside.ActiveClickCount(0) == 0);
}

TEST_CASE("SSME5 预设颜色与设置：默认只开光圈，设置写成文本再读回，坏数据回到默认", "[mouseeffect]")
{
    const ShareMouseEffectSettings def;
    CHECK(def.m_halo);
    CHECK_FALSE(def.m_click);
    CHECK_FALSE(def.m_trail);
    CHECK(def.m_haloColor == share_mouse_color_yellow);
    CHECK(def.m_trailColor == share_mouse_color_red);

    CHECK(ShareMouseColorName(share_mouse_color_yellow) == L"黄色");
    CHECK(ShareMouseColorName(share_mouse_color_red) == L"红色");
    CHECK(ShareMouseColorName(share_mouse_color_blue) == L"蓝色");
    CHECK(ShareMouseColorName(share_mouse_color_green) == L"绿色");
    CHECK(ShareMouseColorName(share_mouse_color_purple) == L"紫色");
    //超出范围按黄色
    CHECK(ShareMouseColorName(kShareMouseColorCount) == L"黄色");
    CHECK(ShareMouseColorRgb(-1) == ShareMouseColorRgb(share_mouse_color_yellow));
    //五种颜色互不相同
    for (int i = 0; i < kShareMouseColorCount; ++i)
    {
        for (int j = i + 1; j < kShareMouseColorCount; ++j)
            CHECK(ShareMouseColorRgb(i) != ShareMouseColorRgb(j));
    }

    ShareMouseEffectSettings s;
    s.m_halo = false;
    s.m_click = true;
    s.m_trail = true;
    s.m_haloColor = share_mouse_color_green;
    s.m_trailColor = share_mouse_color_purple;
    const std::string text = SerializeMouseEffectSettings(s);
    CHECK(text == "halo=0;click=1;trail=1;halocolor=3;trailcolor=4");
    const ShareMouseEffectSettings back = ParseMouseEffectSettings(text);
    CHECK_FALSE(back.m_halo);
    CHECK(back.m_click);
    CHECK(back.m_trail);
    CHECK(back.m_haloColor == share_mouse_color_green);
    CHECK(back.m_trailColor == share_mouse_color_purple);

    //空串与乱写：全部默认
    const ShareMouseEffectSettings empty = ParseMouseEffectSettings("");
    CHECK(empty.m_halo);
    CHECK(empty.m_trailColor == share_mouse_color_red);
    const ShareMouseEffectSettings junk = ParseMouseEffectSettings("abc;;=;halo");
    CHECK(junk.m_halo);
    CHECK_FALSE(junk.m_trail);
    //只写了一部分、颜色超出范围：其余按默认，超出范围的颜色按默认
    const ShareMouseEffectSettings part = ParseMouseEffectSettings("halo=0;trailcolor=4;halocolor=9");
    CHECK_FALSE(part.m_halo);
    CHECK_FALSE(part.m_click);
    CHECK(part.m_trailColor == share_mouse_color_purple);
    CHECK(part.m_haloColor == share_mouse_color_yellow);
}

TEST_CASE("SSME6 鼠标效果菜单：三个开关与两个颜色子菜单，点了之后改设置", "[mouseeffect]")
{
    ShareMouseEffectSettings s;
    std::vector<ShareMouseMenuItem> menu = BuildMouseEffectMenu(s, kSmeIdBase);
    REQUIRE(menu.size() == 5);
    //颜色项带上它代表的颜色（第 16 期补充），开关项与两个子菜单本身不带
    for (size_t i = 0; i < 3; ++i)
        CHECK(menu[i].m_swatchColor == -1);
    CHECK(menu[3].m_swatchColor == -1);
    CHECK(menu[4].m_swatchColor == -1);
    REQUIRE(menu[3].m_children.size() == static_cast<size_t>(kShareMouseColorCount));
    for (int c = 0; c < kShareMouseColorCount; ++c)
    {
        CHECK(menu[3].m_children[c].m_swatchColor == c);
        CHECK(menu[4].m_children[c].m_swatchColor == c);
    }
    CHECK(menu[0].m_text == L"鼠标光圈");
    CHECK(menu[0].m_id == kSmeIdBase);
    CHECK(menu[0].m_checked);
    CHECK(menu[1].m_text == L"点击效果");
    CHECK_FALSE(menu[1].m_checked);
    CHECK(menu[2].m_text == L"鼠标轨迹");
    CHECK_FALSE(menu[2].m_checked);
    CHECK(menu[3].m_text == L"光圈颜色");
    REQUIRE(menu[3].m_children.size() == static_cast<size_t>(kShareMouseColorCount));
    CHECK(menu[3].m_children[0].m_text == L"黄色");
    CHECK(menu[3].m_children[0].m_checked);
    CHECK_FALSE(menu[3].m_children[1].m_checked);
    CHECK(menu[4].m_text == L"轨迹颜色");
    REQUIRE(menu[4].m_children.size() == static_cast<size_t>(kShareMouseColorCount));
    CHECK(menu[4].m_children[share_mouse_color_red].m_checked);

    //开关取反、选颜色
    REQUIRE(ApplyMouseEffectMenuCommand(s, menu[1].m_id, kSmeIdBase));
    CHECK(s.m_click);
    REQUIRE(ApplyMouseEffectMenuCommand(s, menu[0].m_id, kSmeIdBase));
    CHECK_FALSE(s.m_halo);
    REQUIRE(ApplyMouseEffectMenuCommand(s, menu[3].m_children[share_mouse_color_blue].m_id, kSmeIdBase));
    CHECK(s.m_haloColor == share_mouse_color_blue);
    REQUIRE(ApplyMouseEffectMenuCommand(s, menu[4].m_children[share_mouse_color_purple].m_id, kSmeIdBase));
    CHECK(s.m_trailColor == share_mouse_color_purple);
    //重新生成菜单：打勾随之变化
    menu = BuildMouseEffectMenu(s, kSmeIdBase);
    CHECK(menu[1].m_checked);
    CHECK(menu[3].m_children[share_mouse_color_blue].m_checked);
    CHECK_FALSE(menu[3].m_children[share_mouse_color_yellow].m_checked);
    //不认识的 id：不改
    const ShareMouseEffectSettings before = s;
    CHECK_FALSE(ApplyMouseEffectMenuCommand(s, kSmeIdBase + 99, kSmeIdBase));
    CHECK(s.m_halo == before.m_halo);
    CHECK(s.m_haloColor == before.m_haloColor);
}

TEST_CASE("SSME7 物理按键换算成逻辑按键：交换了主次按钮时左右对调", "[mouseeffect]")
{
    bool left = false;
    bool right = false;
    LogicalMouseButtons(true, false, false, left, right);
    CHECK(left);
    CHECK_FALSE(right);
    LogicalMouseButtons(true, false, true, left, right);
    CHECK_FALSE(left);
    CHECK(right);
    LogicalMouseButtons(false, true, true, left, right);
    CHECK(left);
    CHECK_FALSE(right);
    LogicalMouseButtons(false, false, true, left, right);
    CHECK_FALSE(left);
    CHECK_FALSE(right);
}

TEST_CASE("SSME8 推流线程里的效果一环：光标动、效果在淡出、设置变了才多出一帧，静止时不出", "[mouseeffect]")
{
    RECT source;
    ::SetRect(&source, 0, 0, kSmeFrameW, kSmeFrameH);

    //光圈：第一轮设置刚生效要出帧；之后光标不动不出、动了出；移出画面出一帧擦掉光圈，在画面外再动不出
    ShareMouseFxStage halo;
    halo.SetSettings(smeOnly(true, false, false));
    CHECK(halo.Tick(smePt(50, 50), false, false, source, kSmeFrameW, kSmeFrameH, 0));
    halo.OnEncoded();
    CHECK_FALSE(halo.Tick(smePt(50, 50), false, false, source, kSmeFrameW, kSmeFrameH, 40));
    CHECK(halo.Tick(smePt(60, 50), false, false, source, kSmeFrameW, kSmeFrameH, 80));
    halo.OnEncoded();
    CHECK(halo.Tick(smePt(150, 50), false, false, source, kSmeFrameW, kSmeFrameH, 120));
    halo.OnEncoded();
    CHECK_FALSE(halo.Tick(smePt(160, 50), false, false, source, kSmeFrameW, kSmeFrameH, 160));
    //还没有画面（宽高为 0）：光标算在画面外
    ShareMouseFxStage noFrame;
    noFrame.SetSettings(smeOnly(true, false, false));
    noFrame.Tick(smePt(50, 50), false, false, source, 0, 0, 0);
    noFrame.OnEncoded();
    CHECK_FALSE(noFrame.Tick(smePt(60, 50), false, false, source, 0, 0, 40));

    //全部关掉：设置变了出一帧（擦掉旧效果），之后光标怎么动都不出
    halo.SetSettings(smeOnly(false, false, false));
    CHECK(halo.Tick(smePt(50, 50), false, false, source, kSmeFrameW, kSmeFrameH, 200));
    halo.OnEncoded();
    CHECK_FALSE(halo.Tick(smePt(70, 50), false, false, source, kSmeFrameW, kSmeFrameH, 240));
    CHECK(halo.Settings().m_halo == false);

    //轨迹：移动时出帧；停下后淡出期间继续出帧；淡完再出一帧擦掉残影；之后不出
    ShareMouseFxStage trail;
    trail.SetSettings(smeOnly(false, false, true));
    CHECK(trail.Tick(smePt(10, 10), false, false, source, kSmeFrameW, kSmeFrameH, 0));
    trail.OnEncoded();
    CHECK(trail.Tick(smePt(20, 10), false, false, source, kSmeFrameW, kSmeFrameH, 40));
    trail.OnEncoded();
    CHECK(trail.Tick(smePt(20, 10), false, false, source, kSmeFrameW, kSmeFrameH, 300));
    trail.OnEncoded();
    CHECK(trail.Tick(smePt(20, 10), false, false, source, kSmeFrameW, kSmeFrameH, 40 + kShareMouseTrailLifeMs + 10));
    trail.OnEncoded();
    CHECK_FALSE(trail.Tick(smePt(20, 10), false, false, source, kSmeFrameW, kSmeFrameH, 40 + kShareMouseTrailLifeMs + 50));

    //点击效果：不点不出，按下那一刻起出帧
    ShareMouseFxStage click;
    click.SetSettings(smeOnly(false, true, false));
    CHECK(click.Tick(smePt(10, 10), false, false, source, kSmeFrameW, kSmeFrameH, 0));
    click.OnEncoded();
    CHECK_FALSE(click.Tick(smePt(10, 10), false, false, source, kSmeFrameW, kSmeFrameH, 40));
    CHECK(click.Tick(smePt(10, 10), true, false, source, kSmeFrameW, kSmeFrameH, 80));
    click.OnEncoded();
    CHECK(click.Tick(smePt(10, 10), true, false, source, kSmeFrameW, kSmeFrameH, 120));

    //Reset：清空「上次编码时」的记录，光圈开着时下一轮要出帧
    ShareMouseFxStage reset;
    reset.SetSettings(smeOnly(true, false, false));
    reset.Tick(smePt(50, 50), false, false, source, kSmeFrameW, kSmeFrameH, 0);
    reset.OnEncoded();
    reset.Reset();
    CHECK(reset.Tick(smePt(50, 50), false, false, source, kSmeFrameW, kSmeFrameH, 40));
}

TEST_CASE("SSME9 要编码时把效果画进画面副本，原始画面不改；没有可见效果时原样返回", "[mouseeffect]")
{
    RECT source;
    ::SetRect(&source, 0, 0, kSmeFrameW, kSmeFrameH);
    const std::vector<uint8_t> raw = smeWhiteFrame();
    const int rawStride = kSmeFrameW * kSmeBytesPerPixel;

    ShareMouseFxStage stage;
    stage.SetSettings(smeOnly(true, false, false));
    stage.Tick(smePt(50, 50), false, false, source, kSmeFrameW, kSmeFrameH, 0);
    int stride = 0;
    const uint8_t* out = stage.Compose(&raw[0], kSmeFrameW, kSmeFrameH, rawStride, 0, stride);
    REQUIRE(out != NULL);
    CHECK(out != &raw[0]);
    CHECK(stride == rawStride);
    const std::vector<uint8_t> composed(out, out + static_cast<size_t>(stride) * kSmeFrameH);
    CHECK_FALSE(smeIsWhite(composed, 50, 50));
    CHECK(smeIsWhite(composed, 5, 5));
    CHECK(raw == smeWhiteFrame());

    //光标移出画面：没有可见效果，原样返回原始画面
    stage.Tick(smePt(150, 50), false, false, source, kSmeFrameW, kSmeFrameH, 40);
    out = stage.Compose(&raw[0], kSmeFrameW, kSmeFrameH, rawStride, 40, stride);
    CHECK(out == &raw[0]);
    CHECK(stride == rawStride);

    //行跨度大于宽 × 4 的原始画面（采集器常见）：副本按紧凑行跨度输出，像素对齐
    const int padded = rawStride + 16;
    std::vector<uint8_t> wide(static_cast<size_t>(padded) * kSmeFrameH, kSmeWhite);
    stage.Tick(smePt(50, 50), false, false, source, kSmeFrameW, kSmeFrameH, 80);
    out = stage.Compose(&wide[0], kSmeFrameW, kSmeFrameH, padded, 80, stride);
    CHECK(stride == rawStride);
    const std::vector<uint8_t> composedWide(out, out + static_cast<size_t>(stride) * kSmeFrameH);
    CHECK_FALSE(smeIsWhite(composedWide, 50, 50));
    CHECK(smeIsWhite(composedWide, kSmeFrameW - 1, kSmeFrameH - 1));
}

TEST_CASE("SSME10 账号配置里的鼠标效果设置：没有时为空（按默认设置），保存后读回原文", "[mouseeffect]")
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t name[64] = { 0 };
    ::swprintf_s(name, L"%u_flamingo_ssme10.ini", static_cast<unsigned int>(::GetCurrentProcessId()));
    const std::wstring path = std::wstring(dir) + name;
    ::DeleteFileW(path.c_str());

    //配置文件里没有这一项：为空，解析出来是默认设置（只开光圈）
    CUserConfig fresh;
    REQUIRE(fresh.LoadConfig(path.c_str()));
    CHECK(std::wstring(fresh.GetMouseEffects()).empty());
    const ShareMouseEffectSettings defaults = ParseMouseEffectSettings(std::string());
    CHECK(defaults.m_halo);
    CHECK_FALSE(defaults.m_trail);

    //改过之后保存再读回：原文不变，解析结果与改的一致
    ShareMouseEffectSettings changed;
    changed.m_trail = true;
    changed.m_trailColor = share_mouse_color_purple;
    const std::string text = SerializeMouseEffectSettings(changed);
    CUserConfig cfg;
    REQUIRE(cfg.LoadConfig(path.c_str()));
    cfg.SetMouseEffects(CString(text.c_str()));
    CHECK(std::wstring(cfg.GetMouseEffects()) == std::wstring(CString(text.c_str())));
    REQUIRE(cfg.SaveConfig(path.c_str()));
    CUserConfig loaded;
    REQUIRE(loaded.LoadConfig(path.c_str()));
    const std::string readBack = std::string(CT2A(loaded.GetMouseEffects()));
    CHECK(readBack == text);
    const ShareMouseEffectSettings parsed = ParseMouseEffectSettings(readBack);
    CHECK(parsed.m_trail);
    CHECK(parsed.m_trailColor == share_mouse_color_purple);
    ::DeleteFileW(path.c_str());
}

namespace {

//SSME12：固定的录制画面矩形
class SmeFixedGeometry : public screenrecord::IRecordSourceGeometry
{
public:
    explicit SmeFixedGeometry(const RECT& rc)
        : m_rc(rc)
    {
    }

    virtual RECT SourceScreenRect() const override
    {
        return m_rc;
    }

private:
    RECT m_rc;   //录制画面对应的屏幕矩形
};

//SSME12：光标固定在 m_pt 的装饰器
class SmeFixedCursorDecorator : public screenrecord::RecordMouseDecorator
{
public:
    SmeFixedCursorDecorator()
    {
        m_pt.x = 0;
        m_pt.y = 0;
    }

    POINT m_pt;   //光标的屏幕坐标

protected:
    virtual void sampleCursor(POINT& pt, bool& left, bool& right, int64_t nowMs) override
    {
        (void)nowMs;
        pt = m_pt;
        left = false;
        right = false;
    }
};

} // namespace

TEST_CASE("SSME11 录制画面在屏幕上的矩形：显示器矩形加上裁剪矩形，不裁剪时就是显示器", "[mouseeffect]")
{
    RECT monitor;
    ::SetRect(&monitor, 100, 50, 1100, 650);
    RECT none;
    ::SetRect(&none, 0, 0, 0, 0);
    const RECT whole = screenrecord::RecordSourceScreenRect(monitor, none);
    CHECK(whole.left == 100);
    CHECK(whole.top == 50);
    CHECK(whole.right == 1100);
    CHECK(whole.bottom == 650);
    RECT crop;
    ::SetRect(&crop, 10, 20, 110, 220);
    const RECT part = screenrecord::RecordSourceScreenRect(monitor, crop);
    CHECK(part.left == 110);
    CHECK(part.top == 70);
    CHECK(part.right == 210);
    CHECK(part.bottom == 270);
}

TEST_CASE("SSME12 独立录屏的鼠标效果：按录制区域换算光标、就地画光圈，区域外与关掉时不画", "[mouseeffect]")
{
    //录制区域：屏幕上 (1000,500) 起的 100 x 100
    RECT region;
    ::SetRect(&region, 1000, 500, 1000 + kSmeFrameW, 500 + kSmeFrameH);
    SmeFixedGeometry geometry(region);
    SmeFixedCursorDecorator deco;
    deco.SetGeometry(&geometry);
    deco.SetSettings(smeOnly(true, false, false));

    //光标在区域中央：画面中央画上光圈，角上不变
    deco.m_pt = smePt(1050, 550);
    std::vector<uint8_t> frame = smeWhiteFrame();
    deco.Decorate(&frame[0], kSmeFrameW, kSmeFrameH, 0);
    CHECK_FALSE(smeIsWhite(frame, 50, 50));
    CHECK(smeIsWhite(frame, 2, 2));

    //光标在区域外：整幅不变
    deco.m_pt = smePt(900, 550);
    std::vector<uint8_t> outside = smeWhiteFrame();
    deco.Decorate(&outside[0], kSmeFrameW, kSmeFrameH, 40);
    CHECK(outside == smeWhiteFrame());

    //全部关掉：整幅不变；当前设置读得回来
    deco.SetSettings(smeOnly(false, false, false));
    deco.m_pt = smePt(1050, 550);
    std::vector<uint8_t> off = smeWhiteFrame();
    deco.Decorate(&off[0], kSmeFrameW, kSmeFrameH, 80);
    CHECK(off == smeWhiteFrame());
    CHECK_FALSE(deco.Settings().m_halo);

    //没有登记几何信息：不画
    SmeFixedCursorDecorator noGeometry;
    noGeometry.SetSettings(smeOnly(true, false, false));
    noGeometry.m_pt = smePt(50, 50);
    std::vector<uint8_t> clean = smeWhiteFrame();
    noGeometry.Decorate(&clean[0], kSmeFrameW, kSmeFrameH, 0);
    CHECK(clean == smeWhiteFrame());
}

TEST_CASE("SSME14 颜色菜单项的图标：透明底、同色方块带加深细边，当前颜色的方块里有白色对勾", "[mouseeffect]")
{
    const int size = kShareMouseSwatchSize;
    const int stride = size * kSmeBytesPerPixel;
    const COLORREF blue = ShareMouseColorRgb(share_mouse_color_blue);
    //先填满杂值，确认函数会把透明处清零
    std::vector<uint8_t> plain(static_cast<size_t>(stride) * size, 0x7F);
    RenderMouseColorSwatch(&plain[0], size, stride, blue, false);
    //取 (x, y) 处的 B、G、R、A
    struct Px
    {
        int b;
        int g;
        int r;
        int a;
    };
    Px corner = { plain[0], plain[1], plain[2], plain[3] };
    CHECK(corner.b == 0);
    CHECK(corner.g == 0);
    CHECK(corner.r == 0);
    CHECK(corner.a == 0);
    const size_t center = (static_cast<size_t>(8) * size + 8) * kSmeBytesPerPixel;
    CHECK(plain[center] == GetBValue(blue));
    CHECK(plain[center + 1] == GetGValue(blue));
    CHECK(plain[center + 2] == GetRValue(blue));
    CHECK(plain[center + 3] == 255);
    //方块最外一圈是同色加深的细边：比中间暗、不透明
    const size_t edge = (static_cast<size_t>(8) * size + 2) * kSmeBytesPerPixel;
    CHECK(plain[edge + 3] == 255);
    CHECK(plain[edge] < GetBValue(blue));
    //当前颜色：对勾经过的像素接近白色；同一像素在未选中的图标里是方块颜色
    std::vector<uint8_t> checked(static_cast<size_t>(stride) * size, 0x7F);
    RenderMouseColorSwatch(&checked[0], size, stride, blue, true);
    const size_t onCheck = (static_cast<size_t>(10) * size + 7) * kSmeBytesPerPixel;
    CHECK(checked[onCheck] > 200);
    CHECK(checked[onCheck + 1] > 200);
    CHECK(checked[onCheck + 2] > 200);
    CHECK(checked[onCheck + 3] == 255);
    CHECK(plain[onCheck + 2] == GetRValue(blue));
    //角上仍是透明
    CHECK(checked[3] == 0);
    //参数不对：什么也不做
    std::vector<uint8_t> untouched(static_cast<size_t>(stride) * size, 0x7F);
    RenderMouseColorSwatch(&untouched[0], size, stride / 2, blue, true);
    CHECK(untouched[0] == 0x7F);
}
