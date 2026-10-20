/**
 *  共享控制条与录制条的图标按钮，说明见 ShareBarButtonArt.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"

#include "ShareBarButtonArt.h"

#include <objidl.h>    //gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>
#include <string.h>

#include "ShareTopBarCollapse.h"   //TopBarCollapseText / TopBarPinText：「收起 / 固定」的提示文字
#include "../../Source/UI/BalloonChatWnd/ChatIcons.h"   //麦克风、电脑声音、字幕、摄像机、三个点、共享屏幕
#include "../../Source/UI/BalloonMain/MainPanelIcons.h" //人形
#include "../../Source/Common/I18n/I18n.h"              //TR：提示文字的多语言查找
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   //DuiToolTipMgr

namespace screenshare {

namespace {

//图标画在 24×24 的视图里（与 ChatIcons、MainPanelIcons 相同），按按钮边长的这个比例缩放
const float kIconRatio = 0.6f;
//图标视图的边长
const float kViewBox = 24.0f;
//图标描边宽度（视图单位）
const float kIconStroke = 2.0f;
//按钮圆角半径（像素）
const float kCornerRadius = 6.0f;
//九宫格拉伸时四角保留的像素：观看者按钮比方形宽，圆角不能被拉伸变形
const int kBgInset = 7;
//斜杠：先用底色画一道更粗的线把图标线条隔开，粗细是斜杠本身的这个倍数；两端各缩进图标边长的这个比例
const float kSlashGapRatio = 2.2f;
const float kSlashInsetRatio = 0.1f;
//斜杠的描边宽度（像素）
const float kSlashStrokePx = 1.8f;
//下拉小三角：底边宽、高与离按钮右下角的距离（像素）
const float kDropW = 6.0f;
const float kDropH = 3.0f;
const float kDropMargin = 4.0f;
//一个像素的字节数（32 位位图）
const int kBytesPerPixel = 4;
//颜色完全不透明
const BYTE kOpaque = 255;
//32 位 DIB 的位深
const WORD kBitsPerPixel32 = 32;

//每种配色在常态、悬停、按下三种状态下的底色，以及图标颜色
struct TonePalette
{
    COLORREF m_fill[3];   //下标取 share_bar_btn_normal / share_bar_btn_hover / share_bar_btn_pressed
    COLORREF m_icon;      //图标颜色
};

//三种配色（下标取 ShareBarTone）：深色条（底色 RGB(44,48,56)）上的按钮
const TonePalette kPalettes[] =
{
    //常态：比条底色亮一档的灰底、浅色图标
    { { RGB(62, 67, 77), RGB(80, 86, 98), RGB(54, 58, 67) }, RGB(232, 235, 240) },
    //打开的开关：蓝底、白色图标
    { { RGB(47, 128, 237), RGB(70, 145, 245), RGB(34, 108, 208) }, RGB(255, 255, 255) },
    //结束、停止、正在录制：红底、白色图标
    { { RGB(229, 57, 53), RGB(240, 84, 80), RGB(196, 40, 40) }, RGB(255, 255, 255) },
    //已申请、等待对方同意（2026-10-12）：橙底、白色图标，与观看窗原来标题栏图标的「申请中」橙色一致
    { { RGB(237, 137, 30), RGB(245, 158, 60), RGB(206, 114, 18) }, RGB(255, 255, 255) },
};
const int kPaletteCount = sizeof(kPalettes) / sizeof(kPalettes[0]);

//禁用态不分配色：比常态更暗的底、灰色图标
const COLORREF kDisabledFill = RGB(54, 58, 66);
const COLORREF kDisabledIcon = RGB(118, 123, 133);

//录制红点的颜色：亮相、暗相、暂停
const COLORREF kDotBright = RGB(239, 68, 68);
const COLORREF kDotDim = RGB(112, 44, 48);
const COLORREF kDotPaused = RGB(150, 155, 165);

//缓存键各字段的位置：图标 8 位、斜杠 1 位、下拉三角 1 位、配色 4 位、边长放在高 16 位
const int kKeyIconMask = 0xFF;
const int kKeySlashBit = 8;
const int kKeyDropBit = 9;
const int kKeyToneShift = 10;
const int kKeyToneMask = 0xF;
const int kKeySizeShift = 16;

//GDI+ 在本文件惰性初始化一次（进程内多次 GdiplusStartup 是安全的，进程退出时由系统回收）
void ensureGdiplus()
{
    static ULONG_PTR token = 0;
    if (token != 0)
        return;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&token, &input, NULL);
}

Gdiplus::Color toGdiColor(COLORREF c)
{
    return Gdiplus::Color(kOpaque, GetRValue(c), GetGValue(c), GetBValue(c));
}

ShareBarButtonLook makeLook(int icon, bool slash, bool dropMark, int tone)
{
    ShareBarButtonLook look;
    look.m_icon = icon;
    look.m_slash = slash;
    look.m_dropMark = dropMark;
    look.m_tone = tone;
    return look;
}

//建一张 32 位自上而下的 DIB（全部为 0，完全透明）
HBITMAP createDib(int size, void*& bits)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp != NULL && bits == NULL)
    {
        ::DeleteObject(bmp);
        return NULL;
    }
    return bmp;
}

//圆角矩形路径
void addRoundRect(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float r)
{
    const float d = r * 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}

/**
 *  在 (x, y) 起、边长 size 的区域里画一个图标。本文件自画的图标按 24 视图坐标描述，变换后画出。
 */
void drawIcon(Gdiplus::Graphics& g, int icon, float x, float y, float size, const Gdiplus::Color& color)
{
    switch (icon)
    {
    //麦克风：取 ChatIcons
    case share_bar_icon_mic:
        balloonchatwnd::DrawChatIcon(g, balloonchatwnd::kChatIconMic, x, y, size, color, kIconStroke);
        return;
    //电脑声音：显示器里一个音符（ChatIcons）
    case share_bar_icon_speaker:
        balloonchatwnd::DrawChatIcon(g, balloonchatwnd::kChatIconSystemAudio, x, y, size, color, kIconStroke);
        return;
    //字幕框（ChatIcons）
    case share_bar_icon_caption:
        balloonchatwnd::DrawChatIcon(g, balloonchatwnd::kChatIconSubtitle, x, y, size, color, kIconStroke);
        return;
    //摄像机（ChatIcons）
    case share_bar_icon_camera:
        balloonchatwnd::DrawChatIcon(g, balloonchatwnd::kChatIconVideo, x, y, size, color, kIconStroke);
        return;
    //三个点（ChatIcons）
    case share_bar_icon_more:
        balloonchatwnd::DrawChatIcon(g, balloonchatwnd::kChatIconMore, x, y, size, color, kIconStroke);
        return;
    //显示器 + 向上箭头（ChatIcons 的共享屏幕）
    case share_bar_icon_switch:
        balloonchatwnd::DrawChatIcon(g, balloonchatwnd::kChatIconScreenShare, x, y, size, color, kIconStroke);
        return;
    //图钉（ChatIcons）
    case share_bar_icon_pushpin:
        balloonchatwnd::DrawChatIcon(g, balloonchatwnd::kChatIconPin, x, y, size, color, kIconStroke);
        return;
    //人形：取 MainPanelIcons
    case share_bar_icon_people:
        balloonmain::DrawLineIcon(g, balloonmain::kIconUser, x, y, size, color, kIconStroke);
        return;
    //其余为本文件自画，先换到 24 视图坐标
    default:
        break;
    }
    Gdiplus::GraphicsState saved = g.Save();
    g.TranslateTransform(x, y);
    g.ScaleTransform(size / kViewBox, size / kViewBox);
    Gdiplus::SolidBrush brush(color);
    Gdiplus::Pen pen(color, kIconStroke);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    switch (icon)
    {
    //暂停：两道圆角竖条
    case share_bar_icon_pause:
        {
            Gdiplus::GraphicsPath path;
            addRoundRect(path, 6.5f, 5.0f, 4.0f, 14.0f, 1.0f);
            addRoundRect(path, 13.5f, 5.0f, 4.0f, 14.0f, 1.0f);
            g.FillPath(&brush, &path);
        }
        break;
    //继续：向右的实心三角
    case share_bar_icon_play:
        {
            Gdiplus::PointF pts[3] = { Gdiplus::PointF(8.0f, 5.0f), Gdiplus::PointF(19.0f, 12.0f),
                                       Gdiplus::PointF(8.0f, 19.0f) };
            g.FillPolygon(&brush, pts, 3);
        }
        break;
    //停止：实心圆角方块
    case share_bar_icon_stop:
        {
            Gdiplus::GraphicsPath path;
            addRoundRect(path, 6.0f, 6.0f, 12.0f, 12.0f, 2.0f);
            g.FillPath(&brush, &path);
        }
        break;
    //画笔：斜放的铅笔轮廓与笔尖的一道分隔线
    case share_bar_icon_pen:
        {
            Gdiplus::PointF pts[5] = { Gdiplus::PointF(15.5f, 4.5f), Gdiplus::PointF(19.5f, 8.5f),
                                       Gdiplus::PointF(8.5f, 19.5f), Gdiplus::PointF(4.5f, 19.5f),
                                       Gdiplus::PointF(4.5f, 15.5f) };
            g.DrawPolygon(&pen, pts, 5);
            g.DrawLine(&pen, 13.0f, 7.0f, 17.0f, 11.0f);
        }
        break;
    //录制：圆环 + 中间实心圆点
    case share_bar_icon_record:
        g.DrawEllipse(&pen, 4.0f, 4.0f, 16.0f, 16.0f);
        g.FillEllipse(&brush, 8.0f, 8.0f, 8.0f, 8.0f);
        break;
    //鼠标箭头
    case share_bar_icon_mouse:
        {
            Gdiplus::PointF pts[7] = { Gdiplus::PointF(6.0f, 3.5f), Gdiplus::PointF(6.0f, 18.5f),
                                       Gdiplus::PointF(10.0f, 14.8f), Gdiplus::PointF(12.8f, 20.5f),
                                       Gdiplus::PointF(15.2f, 19.4f), Gdiplus::PointF(12.5f, 13.8f),
                                       Gdiplus::PointF(17.5f, 13.8f) };
            g.DrawPolygon(&pen, pts, 7);
        }
        break;
    //收起：向上的尖括号
    case share_bar_icon_collapse:
        {
            Gdiplus::PointF pts[3] = { Gdiplus::PointF(6.0f, 15.0f), Gdiplus::PointF(12.0f, 9.0f),
                                       Gdiplus::PointF(18.0f, 15.0f) };
            g.DrawLines(&pen, pts, 3);
        }
        break;
    //固定：向下的尖括号
    case share_bar_icon_pin:
        {
            Gdiplus::PointF pts[3] = { Gdiplus::PointF(6.0f, 9.0f), Gdiplus::PointF(12.0f, 15.0f),
                                       Gdiplus::PointF(18.0f, 9.0f) };
            g.DrawLines(&pen, pts, 3);
        }
        break;
    //隐藏：眼睛轮廓（上下两段弧）、瞳孔，再加一道左上到右下的斜线
    case share_bar_icon_hide:
        {
            Gdiplus::GraphicsPath eye;
            eye.AddBezier(3.0f, 12.0f, 7.0f, 6.0f, 17.0f, 6.0f, 21.0f, 12.0f);
            eye.AddBezier(21.0f, 12.0f, 17.0f, 18.0f, 7.0f, 18.0f, 3.0f, 12.0f);
            eye.CloseFigure();
            g.DrawPath(&pen, &eye);
            g.DrawEllipse(&pen, 9.5f, 9.5f, 5.0f, 5.0f);
            g.DrawLine(&pen, 4.5f, 4.5f, 19.5f, 19.5f);
        }
        break;
    //白板：圆角矩形的板面、下方的支脚，板面上一支斜放的笔（新建白板，2026-10-12）
    case share_bar_icon_board:
        {
            Gdiplus::GraphicsPath board;
            addRoundRect(board, 3.0f, 4.0f, 18.0f, 13.0f, 1.5f);
            g.DrawPath(&pen, &board);
            g.DrawLine(&pen, 9.0f, 17.0f, 7.0f, 21.0f);
            g.DrawLine(&pen, 15.0f, 17.0f, 17.0f, 21.0f);
            g.DrawLine(&pen, 8.0f, 13.0f, 15.5f, 7.5f);
        }
        break;
    //共享说明：圆圈里一个 i（上面一个圆点、下面一道竖线）
    case share_bar_icon_info:
        g.DrawEllipse(&pen, 3.0f, 3.0f, 18.0f, 18.0f);
        g.FillEllipse(&brush, 10.8f, 6.8f, 2.4f, 2.4f);
        g.DrawLine(&pen, 12.0f, 11.0f, 12.0f, 17.0f);
        break;
    //皇冠：三个尖的冠身加一道底边（主持人菜单）
    case share_bar_icon_crown:
        {
            Gdiplus::PointF pts[7] = { Gdiplus::PointF(4.0f, 17.0f), Gdiplus::PointF(3.0f, 7.5f),
                                       Gdiplus::PointF(8.5f, 11.5f), Gdiplus::PointF(12.0f, 5.0f),
                                       Gdiplus::PointF(15.5f, 11.5f), Gdiplus::PointF(21.0f, 7.5f),
                                       Gdiplus::PointF(20.0f, 17.0f) };
            g.DrawPolygon(&pen, pts, 7);
            g.DrawLine(&pen, 4.5f, 20.5f, 19.5f, 20.5f);
        }
        break;
    //全屏：四个角各一道向外的直角折线
    case share_bar_icon_fullscreen:
        {
            Gdiplus::PointF tl[3] = { Gdiplus::PointF(4.0f, 9.0f), Gdiplus::PointF(4.0f, 4.0f), Gdiplus::PointF(9.0f, 4.0f) };
            Gdiplus::PointF tr[3] = { Gdiplus::PointF(15.0f, 4.0f), Gdiplus::PointF(20.0f, 4.0f),
                                      Gdiplus::PointF(20.0f, 9.0f) };
            Gdiplus::PointF br[3] = { Gdiplus::PointF(20.0f, 15.0f), Gdiplus::PointF(20.0f, 20.0f),
                                      Gdiplus::PointF(15.0f, 20.0f) };
            Gdiplus::PointF bl[3] = { Gdiplus::PointF(9.0f, 20.0f), Gdiplus::PointF(4.0f, 20.0f),
                                      Gdiplus::PointF(4.0f, 15.0f) };
            g.DrawLines(&pen, tl, 3);
            g.DrawLines(&pen, tr, 3);
            g.DrawLines(&pen, br, 3);
            g.DrawLines(&pen, bl, 3);
        }
        break;
    //退出全屏：四个角各一道向内的直角折线
    case share_bar_icon_exit_fullscreen:
        {
            Gdiplus::PointF tl[3] = { Gdiplus::PointF(4.0f, 9.0f), Gdiplus::PointF(9.0f, 9.0f), Gdiplus::PointF(9.0f, 4.0f) };
            Gdiplus::PointF tr[3] = { Gdiplus::PointF(15.0f, 4.0f), Gdiplus::PointF(15.0f, 9.0f),
                                      Gdiplus::PointF(20.0f, 9.0f) };
            Gdiplus::PointF br[3] = { Gdiplus::PointF(20.0f, 15.0f), Gdiplus::PointF(15.0f, 15.0f),
                                      Gdiplus::PointF(15.0f, 20.0f) };
            Gdiplus::PointF bl[3] = { Gdiplus::PointF(9.0f, 20.0f), Gdiplus::PointF(9.0f, 15.0f),
                                      Gdiplus::PointF(4.0f, 15.0f) };
            g.DrawLines(&pen, tl, 3);
            g.DrawLines(&pen, tr, 3);
            g.DrawLines(&pen, br, 3);
            g.DrawLines(&pen, bl, 3);
        }
        break;
    //离开：左边一个开口朝右的门框，中间一支向右穿出门框的箭头
    case share_bar_icon_leave:
        {
            Gdiplus::PointF frame[4] = { Gdiplus::PointF(11.0f, 4.0f), Gdiplus::PointF(4.0f, 4.0f),
                                         Gdiplus::PointF(4.0f, 20.0f), Gdiplus::PointF(11.0f, 20.0f) };
            g.DrawLines(&pen, frame, 4);
            g.DrawLine(&pen, 9.0f, 12.0f, 20.0f, 12.0f);
            Gdiplus::PointF head[3] = { Gdiplus::PointF(16.0f, 8.0f), Gdiplus::PointF(20.0f, 12.0f),
                                        Gdiplus::PointF(16.0f, 16.0f) };
            g.DrawLines(&pen, head, 3);
        }
        break;
    //不画图标或未知取值
    default:
        break;
    }
    g.Restore(saved);
}

} // namespace

ShareBarButtonLook::ShareBarButtonLook()
    : m_icon(share_bar_icon_none)
    , m_slash(false)
    , m_dropMark(false)
    , m_tone(share_bar_tone_normal)
{
}

ShareBarButtonLook ShareBarLookFor(int action, bool on)
{
    switch (action)
    {
    //暂停 / 继续：暂停中画「继续」
    case share_bar_pause:
        return makeLook(on ? share_bar_icon_play : share_bar_icon_pause, false, false, share_bar_tone_normal);
    //停止录制、结束共享：红底方块
    case share_bar_stop_record:
    case share_bar_stop_share:
        return makeLook(share_bar_icon_stop, false, false, share_bar_tone_danger);
    //麦克风：打开时蓝底，关着时画斜杠
    case share_bar_mic:
        return makeLook(share_bar_icon_mic, !on, false, on ? share_bar_tone_active : share_bar_tone_normal);
    //电脑声音：同上
    case share_bar_system_audio:
        return makeLook(share_bar_icon_speaker, !on, false, on ? share_bar_tone_active : share_bar_tone_normal);
    //字幕：同上
    case share_bar_caption:
        return makeLook(share_bar_icon_caption, !on, false, on ? share_bar_tone_active : share_bar_tone_normal);
    //摄像头：同上
    case share_bar_camera:
        return makeLook(share_bar_icon_camera, !on, false, on ? share_bar_tone_active : share_bar_tone_normal);
    //标注 / 画笔：在标注时蓝底
    case share_bar_annotate:
        return makeLook(share_bar_icon_pen, false, false, on ? share_bar_tone_active : share_bar_tone_normal);
    //录制这场共享：在录时红底
    case share_bar_record:
        return makeLook(share_bar_icon_record, false, false, on ? share_bar_tone_danger : share_bar_tone_normal);
    //鼠标效果：点开菜单，带下拉三角
    case share_bar_mouse:
        return makeLook(share_bar_icon_mouse, false, true, share_bar_tone_normal);
    //更多
    case share_bar_more:
        return makeLook(share_bar_icon_more, false, false, share_bar_tone_normal);
    //切换共享内容
    case share_bar_switch:
        return makeLook(share_bar_icon_switch, false, false, share_bar_tone_normal);
    //观看者：底图不画图标，人形另作前置图标、人数写在按钮文字里
    case share_bar_viewers:
        return makeLook(share_bar_icon_none, false, false, share_bar_tone_normal);
    //收起 / 固定：收起或临时展开时画「固定」
    case share_bar_collapse:
        return makeLook(on ? share_bar_icon_pin : share_bar_icon_collapse, false, false, share_bar_tone_normal);
    //隐藏工具条
    case share_bar_hide:
        return makeLook(share_bar_icon_hide, false, false, share_bar_tone_normal);
    //申请控制：控制中蓝底（申请中的橙底由 ShareBarLookForTriState 给出）
    case share_bar_control:
        return makeLook(share_bar_icon_mouse, false, false, on ? share_bar_tone_active : share_bar_tone_normal);
    //共享我的屏幕：共享中蓝底
    case share_bar_share_mine:
        return makeLook(share_bar_icon_switch, false, false, on ? share_bar_tone_active : share_bar_tone_normal);
    //新建白板：白板开着时蓝底
    case share_bar_board:
        return makeLook(share_bar_icon_board, false, false, on ? share_bar_tone_active : share_bar_tone_normal);
    //共享说明
    case share_bar_info:
        return makeLook(share_bar_icon_info, false, false, share_bar_tone_normal);
    //主持人菜单：点开是菜单，带下拉三角
    case share_bar_host:
        return makeLook(share_bar_icon_crown, false, true, share_bar_tone_normal);
    //全屏 / 退出全屏：正在全屏时画「退出全屏」
    case share_bar_fullscreen:
        return makeLook(on ? share_bar_icon_exit_fullscreen : share_bar_icon_fullscreen, false, false,
                        share_bar_tone_normal);
    //离开共享 / 断开远程桌面：红底
    case share_bar_leave:
        return makeLook(share_bar_icon_leave, false, false, share_bar_tone_danger);
    //未知种类：常态、不画图标
    default:
        return ShareBarButtonLook();
    }
}

ShareBarButtonLook ShareBarLookForTriState(int action, int state)
{
    const bool triStateAction = (action == share_bar_control || action == share_bar_share_mine ||
                                 action == share_bar_board);
    //申请中只对三态按钮有意义：图标与关着时相同、换成橙底
    if (state == share_bar_tri_pending && triStateAction)
    {
        ShareBarButtonLook look = ShareBarLookFor(action, false);
        look.m_tone = share_bar_tone_pending;
        return look;
    }
    return ShareBarLookFor(action, state == share_bar_tri_on);
}

std::wstring ShareBarTooltipFor(int action, bool on, bool recordBar)
{
    switch (action)
    {
    //暂停 / 继续：说明点击之后会怎样
    case share_bar_pause:
        return on ? TR(L"继续") : TR(L"暂停");
    //录制条的「停止」
    case share_bar_stop_record:
        return TR(L"停止");
    //共享控制条的「结束共享」
    case share_bar_stop_share:
        return TR(L"结束共享");
    //麦克风：录制条写当前状态，共享控制条写点击之后会怎样（与改版前的按钮文字一致）
    case share_bar_mic:
        if (recordBar)
            return on ? TR(L"麦克风：开") : TR(L"麦克风：关");
        return on ? TR(L"关闭麦克风") : TR(L"打开麦克风");
    //电脑声音：同上
    case share_bar_system_audio:
        if (recordBar)
            return on ? TR(L"电脑声音：开") : TR(L"电脑声音：关");
        return on ? TRC(L"屏幕共享控制条按钮", L"停止电脑声音") : TRC(L"屏幕共享控制条按钮", L"共享电脑声音");
    //字幕：录制条写当前状态；共享控制条上是「为我的发言生成字幕」的开关
    case share_bar_caption:
        if (recordBar)
            return on ? TR(L"字幕：开") : TR(L"字幕：关");
        return on ? TR(L"停止为我的发言生成字幕") : TR(L"为我的发言生成字幕");
    //摄像头：同麦克风
    case share_bar_camera:
        if (recordBar)
            return on ? TR(L"摄像头：开") : TR(L"摄像头：关");
        return on ? TR(L"关闭摄像头") : TR(L"打开摄像头");
    //标注：录制条上是「画笔」
    case share_bar_annotate:
        if (recordBar)
            return TR(L"画笔");
        return on ? TR(L"退出标注") : TR(L"标注");
    //录制这场共享
    case share_bar_record:
        return on ? TR(L"停止录制") : TR(L"录制");
    //鼠标效果
    case share_bar_mouse:
        return TR(L"鼠标效果");
    //更多
    case share_bar_more:
        return TR(L"更多");
    //切换共享内容
    case share_bar_switch:
        return TRC(L"屏幕共享控制条按钮", L"切换内容");
    //观看者名单
    case share_bar_viewers:
        return TR(L"观看者名单");
    //收起 / 固定
    case share_bar_collapse:
        return on ? TopBarPinText() : TopBarCollapseText();
    //隐藏工具条：提示里说明怎样让它重新出现
    case share_bar_hide:
        return TopBarHideText();
    //申请控制：已生效与未申请时的文字；申请中的「取消申请」见 ShareBarTooltipForTriState
    case share_bar_control:
        return on ? TR(L"结束控制") : TR(L"申请控制");
    //共享我的屏幕：同上
    case share_bar_share_mine:
        return on ? TR(L"停止我的共享") : TR(L"共享我的屏幕");
    //新建白板：同上
    case share_bar_board:
        return on ? TR(L"关闭我的白板") : TR(L"新建白板");
    //共享说明
    case share_bar_info:
        return TR(L"共享说明");
    //主持人菜单
    case share_bar_host:
        return TR(L"主持人");
    //全屏 / 退出全屏
    case share_bar_fullscreen:
        return on ? TR(L"退出全屏") : TR(L"全屏");
    //离开共享（远程桌面的「断开」由使用方另给提示）。「离开」在别处是在线状态，用上下文区分
    case share_bar_leave:
        return TRC(L"离开屏幕共享的按钮", L"离开");
    //未知种类：没有提示
    default:
        return std::wstring();
    }
}

std::wstring ShareBarTooltipForTriState(int action, int state)
{
    if (state == share_bar_tri_pending)
    {
        //三态按钮申请中：点了是撤回申请
        if (action == share_bar_control)
            return TR(L"取消申请");
        if (action == share_bar_share_mine)
            return TR(L"取消共享申请");
        if (action == share_bar_board)
            return TR(L"取消白板申请");
    }
    return ShareBarTooltipFor(action, state == share_bar_tri_on, false);
}

int ShareRecordDotStateAt(bool recording, bool paused, int64_t nowMs)
{
    if (!recording)
        return share_record_dot_hidden;
    if (paused)
        return share_record_dot_paused;
    if (nowMs < 0)
        nowMs = 0;
    return ((nowMs / kShareRecordBlinkMs) % 2) == 0 ? share_record_dot_bright : share_record_dot_dim;
}

COLORREF ShareRecordDotColor(int state, COLORREF barBg)
{
    switch (state)
    {
    //亮相
    case share_record_dot_bright:
        return kDotBright;
    //暗相
    case share_record_dot_dim:
        return kDotDim;
    //暂停：灰色
    case share_record_dot_paused:
        return kDotPaused;
    //隐藏：与条的底色相同
    default:
        return barBg;
    }
}

HBITMAP MakeShareBarButtonBitmap(const ShareBarButtonLook& look, int state, int size)
{
    if (size <= 0 || state < 0 || state >= share_bar_btn_state_count)
        return NULL;
    ensureGdiplus();
    void* bits = NULL;
    HBITMAP bmp = createDib(size, bits);
    if (bmp == NULL)
        return NULL;
    //配色：禁用态统一暗灰，其余按配色与状态取
    const int tone = (look.m_tone >= 0 && look.m_tone < kPaletteCount) ? look.m_tone : share_bar_tone_normal;
    const COLORREF fill = (state == share_bar_btn_disabled) ? kDisabledFill : kPalettes[tone].m_fill[state];
    const COLORREF iconColor = (state == share_bar_btn_disabled) ? kDisabledIcon : kPalettes[tone].m_icon;
    {
        //GDI+ 直接画进 DIB 的像素：预乘透明度格式，与 AlphaBlend 要求的格式一致
        Gdiplus::Bitmap canvas(size, size, size * kBytesPerPixel, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
        Gdiplus::Graphics g(&canvas);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        Gdiplus::GraphicsPath bg;
        addRoundRect(bg, 0.5f, 0.5f, static_cast<float>(size) - 1.0f, static_cast<float>(size) - 1.0f, kCornerRadius);
        Gdiplus::SolidBrush fillBrush(toGdiColor(fill));
        g.FillPath(&fillBrush, &bg);
        //图标居中
        const float iconSize = static_cast<float>(size) * kIconRatio;
        const float iconX = (static_cast<float>(size) - iconSize) / 2;
        const float iconY = iconX;
        drawIcon(g, look.m_icon, iconX, iconY, iconSize, toGdiColor(iconColor));
        if (look.m_slash)
        {
            //斜杠：先用底色画一道更粗的线把图标线条隔开，再用图标颜色画斜杠本身
            const float inset = iconSize * kSlashInsetRatio;
            const float x1 = iconX + inset;
            const float y1 = iconY + inset;
            const float x2 = iconX + iconSize - inset;
            const float y2 = iconY + iconSize - inset;
            Gdiplus::Pen gap(toGdiColor(fill), kSlashStrokePx * kSlashGapRatio);
            gap.SetStartCap(Gdiplus::LineCapRound);
            gap.SetEndCap(Gdiplus::LineCapRound);
            g.DrawLine(&gap, x1, y1, x2, y2);
            Gdiplus::Pen slash(toGdiColor(iconColor), kSlashStrokePx);
            slash.SetStartCap(Gdiplus::LineCapRound);
            slash.SetEndCap(Gdiplus::LineCapRound);
            g.DrawLine(&slash, x1, y1, x2, y2);
        }
        if (look.m_dropMark)
        {
            //右下角的下拉小三角
            const float right = static_cast<float>(size) - kDropMargin;
            const float bottom = static_cast<float>(size) - kDropMargin;
            Gdiplus::PointF pts[3] = { Gdiplus::PointF(right - kDropW, bottom - kDropH), Gdiplus::PointF(right, bottom - kDropH),
                                       Gdiplus::PointF(right - kDropW / 2, bottom) };
            Gdiplus::SolidBrush iconBrush(toGdiColor(iconColor));
            g.FillPolygon(&iconBrush, pts, 3);
        }
    }
    ::GdiFlush();
    return bmp;
}

HBITMAP MakeShareBarIconBitmap(int icon, int size, COLORREF color)
{
    if (size <= 0)
        return NULL;
    ensureGdiplus();
    void* bits = NULL;
    HBITMAP bmp = createDib(size, bits);
    if (bmp == NULL)
        return NULL;
    {
        Gdiplus::Bitmap canvas(size, size, size * kBytesPerPixel, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
        Gdiplus::Graphics g(&canvas);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        drawIcon(g, icon, 0.0f, 0.0f, static_cast<float>(size), toGdiColor(color));
    }
    ::GdiFlush();
    return bmp;
}

HBITMAP MakeShareBarBackgroundBitmap(COLORREF color)
{
    //背景图的边长（像素）：九宫格拉伸铺满窗口，取很小即可
    const int side = 8;
    void* bits = NULL;
    HBITMAP bmp = createDib(side, bits);
    if (bmp == NULL)
        return NULL;
    BYTE* p = static_cast<BYTE*>(bits);
    for (int i = 0; i < side * side; ++i)
    {
        p[i * kBytesPerPixel + 0] = GetBValue(color);
        p[i * kBytesPerPixel + 1] = GetGValue(color);
        p[i * kBytesPerPixel + 2] = GetRValue(color);
        p[i * kBytesPerPixel + 3] = kOpaque;
    }
    return bmp;
}

ShareBarButtonArtCache::StateBitmaps::StateBitmaps()
{
    for (int i = 0; i < share_bar_btn_state_count; ++i)
        m_states[i] = NULL;
}

ShareBarButtonArtCache::ShareBarButtonArtCache()
{
}

ShareBarButtonArtCache::~ShareBarButtonArtCache()
{
    for (std::map<int, StateBitmaps>::iterator it = m_bitmaps.begin(); it != m_bitmaps.end(); ++it)
    {
        for (int i = 0; i < share_bar_btn_state_count; ++i)
        {
            if (it->second.m_states[i] != NULL)
                ::DeleteObject(it->second.m_states[i]);
        }
    }
    m_bitmaps.clear();
}

size_t ShareBarButtonArtCache::LookCount() const
{
    return m_bitmaps.size();
}

int ShareBarButtonArtCache::keyOf(const ShareBarButtonLook& look)
{
    return (look.m_icon & kKeyIconMask) | ((look.m_slash ? 1 : 0) << kKeySlashBit) |
           ((look.m_dropMark ? 1 : 0) << kKeyDropBit) | ((look.m_tone & kKeyToneMask) << kKeyToneShift) |
           (kShareBarBtnPx << kKeySizeShift);
}

bool ShareBarButtonArtCache::Apply(balloonwjui::DuiButton* button, const ShareBarButtonLook& look,
                                   const std::wstring& tooltip, bool keepText)
{
    if (button == NULL)
        return false;
    const int key = keyOf(look);
    std::map<int, StateBitmaps>::iterator it = m_bitmaps.find(key);
    if (it == m_bitmaps.end())
    {
        //第一次用到这种外观：四张位图一起画，任何一张失败都不缓存，下次再试
        StateBitmaps made;
        bool ok = true;
        for (int i = 0; i < share_bar_btn_state_count; ++i)
        {
            made.m_states[i] = MakeShareBarButtonBitmap(look, i, kShareBarBtnPx);
            if (made.m_states[i] == NULL)
                ok = false;
        }
        if (!ok)
        {
            for (int i = 0; i < share_bar_btn_state_count; ++i)
            {
                if (made.m_states[i] != NULL)
                    ::DeleteObject(made.m_states[i]);
            }
            return false;
        }
        it = m_bitmaps.insert(std::make_pair(key, made)).first;
    }
    const StateBitmaps& bmps = it->second;
    //图标按钮不接收焦点：DuiButton 默认接收焦点，点击后会在按钮内侧画一圈虚线焦点框
    button->SetTabStop(false);
    //文字（观看者人数）用主色变体的白字，与深色底图对比清楚；底图优先于变体的填充色
    button->SetVariant(balloonwjui::DuiButton::Variant::Primary);
    button->SetBgBitmap(bmps.m_states[share_bar_btn_normal], bmps.m_states[share_bar_btn_hover],
                        bmps.m_states[share_bar_btn_pressed], bmps.m_states[share_bar_btn_disabled]);
    button->SetBgInsets(kBgInset, kBgInset, kBgInset, kBgInset);
    if (!keepText)
        button->SetText(_T(""));
    if (tooltip.empty())
        balloonwjui::DuiToolTipMgr::Inst().Unregister(button);
    else
        balloonwjui::DuiToolTipMgr::Inst().Register(button, tooltip.c_str());
    button->Invalidate();
    return true;
}

} // namespace screenshare
