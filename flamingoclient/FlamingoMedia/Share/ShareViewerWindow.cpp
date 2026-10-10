/**
 *  屏幕共享观看窗，ShareViewerWindow.cpp
 *  说明见 ShareViewerWindow.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareViewerWindow.h"

#include <math.h>        // pow：白板滚轮缩放（第 23 期）
#include <algorithm>
#include <memory>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM（控制中换算鼠标位置，第 18 期）
#include "ShareAnnotationRender.h"
#include "../RemoteDesktop/RdInputDef.h"   // rd_input_key（控制中记下按着的键，第 18 期）
#include "ShareAnnotationSession.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"   // 标题栏「字幕」图标的菜单（语音字幕）
#include "ShareMessages.h"
#include "ShareRoomPolicy.h"   // MakeShareViewerChrome、ShareViewerCountText（第 13 期）、MakeShareViewerCaption（第 15 期）
#include "ShareScreens.h"      // ComputeScreenTiles、HitScreenTile（第 22 期多画面布局）
#include "ShareSources.h"      // IsBarCaptureAllowed：「主持人 ▾」菜单里「允许截图」的勾选状态（2026-10-08）
#include "../../Source/net/Protocol.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Source/UI/BalloonMain/P2PBadge.h"   // 「P2P」徽章（2026-10-04 起）

#pragma comment(lib, "msimg32.lib")   //AlphaBlend（提示文字的半透明底块）

namespace screenshare {

namespace {

//非全屏时的初始整窗尺寸、最小尺寸与标题栏高（逻辑像素）
const int kWindowW = 1280;
const int kWindowH = 760;
const int kMinW = 640;
const int kMinH = 400;
const int kTitleBarH = 36;
//标题栏帧率刷新定时器：每秒一次；id 取较大值避开 balloonui 基类可能占用的小 id
const UINT_PTR kFpsTimerId = 201;
const UINT kFpsTimerMs = 1000;
//录制期间刷新白板图的定时器（2026-10-07，方案第 14.1 节 ④-2）：当前那块是白板时每 200 毫秒画一次，即录像里每秒 5 张。
//id 与帧率（201）、摄像头小窗（202）、拉回前台（203）三个定时器错开
const UINT_PTR kRecordBoardTimerId = 204;
const UINT kRecordBoardTimerMs = 200;
//白板还没在屏幕上显示过时，画给录制用的图的尺寸（像素，16:9）
const int kRecordBoardDefaultW = 1280;
const int kRecordBoardDefaultH = 720;
//不透明的 alpha 值、每像素字节数与 alpha 所在的字节（GDI 画进 32 位位图时不写 alpha，取出后补上）
const BYTE kOpaqueAlpha = 255;
const int kBgraBytes = 4;
const int kAlphaByte = 3;
//提示文字的底块内边距、底色与文字色
const int kStatusPadX = 24;
const int kStatusPadY = 14;
const COLORREF kStatusBg = RGB(40, 40, 44);
const COLORREF kStatusText = RGB(240, 240, 240);
//提示底块的不透明度（0~255）
const BYTE kStatusAlpha = 200;
//32 位 DIB 的位深
const WORD kBitsPerPixel32 = 32;
//多人共享的多块画面（第 22 期）：块与块的间距、名字标签距块左上角的距离与内边距、标签底色与文字色、当前那一块的描边色
const int kTileGapPx = 4;
const int kTileLabelMargin = 6;
const int kTileLabelPadX = 8;
const int kTileLabelPadY = 3;
const COLORREF kTileLabelBg = RGB(30, 32, 38);
const COLORREF kTileLabelText = RGB(240, 240, 240);
const COLORREF kTileActiveBorder = RGB(30, 136, 229);
//标题栏麦克风图标：边长、每像素字节数、墨色（打开时深灰，关闭时浅灰加一道红斜线）
const int kMicIconSide = 16;
const int kMicIconBpp = 4;
const BYTE kMicInkOn = 70;
const BYTE kMicInkOff = 150;
const BYTE kMicSlashR = 210;
const BYTE kMicSlashG = 60;
const BYTE kMicSlashB = 50;
//麦克风图标各部分的像素范围（含两端）：话筒头、托架两侧竖线、托架底边、支杆、底座
const int kMicHeadLeft = 6;
const int kMicHeadRight = 9;
const int kMicHeadTop = 2;
const int kMicHeadBottom = 9;
const int kMicCradleLeft = 4;
const int kMicCradleRight = 11;
const int kMicCradleTop = 7;
const int kMicCradleBottom = 10;
const int kMicStemLeft = 7;
const int kMicStemRight = 8;
const int kMicStemBottom = 13;
const int kMicBaseLeft = 5;
const int kMicBaseRight = 10;
//关闭状态的红斜线：从左上到右下，两像素粗
const int kMicSlashFrom = 2;
const int kMicSlashTo = 13;
//全屏工具条额外按钮的文字
const wchar_t* const kMicOnButtonText = TR_NOOP(L"关闭麦克风");
const wchar_t* const kMicOffButtonText = TR_NOOP(L"打开麦克风");
const wchar_t* const kCamOnButtonText = TR_NOOP(L"关闭摄像头");
const wchar_t* const kCamOffButtonText = TR_NOOP(L"打开摄像头");
const wchar_t* const kAnnotButtonText = TR_NOOP(L"标注");
const wchar_t* const kAnnotExitButtonText = TR_NOOP(L"退出标注");
const wchar_t* const kRecordButtonText = TR_NOOP(L"录制");
const wchar_t* const kRecordStopButtonText = TR_NOOP(L"停止录制");
//标题栏录制图标：实心圆点的半径（像素）、颜色（未录制为灰，录制中为红）
const int kRecDotRadius = 5;
const BYTE kRecIdleInk = 120;
const BYTE kRecOnR = 229;
const BYTE kRecOnG = 57;
const BYTE kRecOnB = 53;
//标题栏字幕图标（语音字幕）：一个矩形框，框里两行长短不一的横线表示字幕；为本人生成字幕时为蓝色，否则为灰色。
//各部分的像素范围（含两端）：外框、上一行横线、下一行横线
const int kCaptionBoxLeft = 1;
const int kCaptionBoxRight = 14;
const int kCaptionBoxTop = 3;
const int kCaptionBoxBottom = 12;
const int kCaptionLine1Y = 6;
const int kCaptionLine1Left = 4;
const int kCaptionLine1Right = 9;
const int kCaptionLine2Y = 9;
const int kCaptionLine2Left = 4;
const int kCaptionLine2Right = 11;
const BYTE kCaptionIdleInk = 120;
//标题栏字幕图标的提示文字
const wchar_t* const kCaptionButtonText = TR_NOOP(L"字幕");
//标题栏「字幕」菜单的两项
const UINT kCaptionMenuSelf = 1;   //为我的发言生成字幕
const UINT kCaptionMenuShow = 2;   //显示字幕
//标题栏标注图标：笔杆从右上到左下的斜线（两像素粗）与左下角的笔尖；标注模式中笔为蓝色
const int kPenFrom = 3;
const int kPenTo = 12;
const int kPenTipX = 2;
const int kPenTipY = 13;
const BYTE kPenActiveR = 30;
const BYTE kPenActiveG = 136;
const BYTE kPenActiveB = 229;
//标题栏观看者图标（第 13 期）：头部圆的圆心与半径、肩部（半椭圆）的范围，灰色
const int kPeopleHeadX = 8;
const int kPeopleHeadY = 5;
const int kPeopleHeadR = 3;
const int kPeopleBodyTop = 10;
const int kPeopleBodyBottom = 14;
const int kPeopleBodyHalfW = 6;
const BYTE kPeopleInk = 120;
//全屏工具条上「申请控制 / 取消申请 / 结束控制」是第几个额外按钮（麦克风、摄像头、标注、录制之后，第 18 期）
const size_t kControlExtraIndex = 4;
//全屏工具条上「共享我的屏幕」是第几个额外按钮（第 22 期，排在「申请控制」之后）
const size_t kShareMineExtraIndex = 5;
//全屏工具条上「新建白板」是第几个额外按钮（第 23 期，排在「共享我的屏幕」之后）
const size_t kBoardExtraIndex = 6;
//全屏工具条上「N 人观看 ▾」是第几个额外按钮（第 13 期；第 18 期起排在「申请控制」之后，第 22 期起排在「共享我的屏幕」之后，
//第 23 期起排在「新建白板」之后）
const size_t kViewersExtraIndex = 7;
//窗口模式下观看者名单的锚点：标题栏右端往左这么宽的一块（逻辑像素）
const int kViewersAnchorW = 330;
//全屏工具条上「共享说明」是第几个额外按钮（第 15 期，排在「N 人观看 ▾」之后，只在共享有描述时出现）
const size_t kInfoExtraIndex = 8;
//「主持人 ▾」按钮的文字与标题栏皇冠图标的提示（2026-10-07，经移交成为主持人的人的入口）
const wchar_t* const kHostButtonText = TR_NOOP(L"主持人 ▾");
//「主持人 ▾」菜单「关闭白板 ▸」（2026-10-08）：入口项与第一块白板的菜单项 id，避开 share_host_menu_item 的取值（1～5）
const UINT kHostMenuCloseBoardRoot = 99;
const UINT kHostMenuCloseBoardBase = 100;
const wchar_t* const kHostIconTip = TR_NOOP(L"主持人");
//标题栏皇冠图标（2026-10-07）：三个尖顶连成的皇冠，金色。三个尖顶的横坐标按半像素计（乘 2），尖顶所在的行、
//皇冠左右边界与底行按像素计；某列的上边沿 = 尖顶行 + 该列中心到最近尖顶的半像素距离（斜边每横走半个像素下降一个像素）
const int kCrownPeak2[] = { 5, 16, 27 };
const int kCrownPeakTop = 3;
const int kCrownLeft = 2;
const int kCrownRight = 13;
const int kCrownBottom = 12;
const BYTE kCrownR = 214;
const BYTE kCrownG = 158;
const BYTE kCrownB = 30;
//「新建白板」按钮的文字（第 23 期），按状态取（取值同 share_viewer_mine_state）
const wchar_t* const kBoardButtonTexts[] = { TR_NOOP(L"新建白板"), TR_NOOP(L"取消白板申请"), TR_NOOP(L"关闭我的白板") };
//标题栏「新建白板」图标（第 23 期）：一块白板加一支斜放的笔；板面的左右、上下边界，笔的起止
const int kBoardFrameLeft = 1;
const int kBoardFrameRight = 14;
const int kBoardFrameTop = 2;
const int kBoardFrameBottom = 12;
const int kBoardPenFrom = 4;
const int kBoardPenTo = 10;
//「共享我的屏幕」按钮的文字（第 22 期），按 share_viewer_mine_state 取
const wchar_t* const kShareMineButtonTexts[] = { TR_NOOP(L"共享我的屏幕"), TR_NOOP(L"取消共享申请"), TR_NOOP(L"停止我的共享") };
//标题栏「共享我的屏幕」图标（第 22 期）：一台显示器，屏幕框的左右、上下边界，底座的范围；颜色同控制图标（灰、橙、蓝）
const int kMineScreenLeft = 1;
const int kMineScreenRight = 14;
const int kMineScreenTop = 2;
const int kMineScreenBottom = 11;
const int kMineStandLeft = 6;
const int kMineStandRight = 9;
const int kMineBaseLeft = 4;
const int kMineBaseRight = 11;
const int kMineBaseRow = 14;
//控制键鼠按钮的文字（第 18 期），按 share_viewer_control_state 取
const wchar_t* const kControlButtonTexts[] = { TR_NOOP(L"申请控制"), TR_NOOP(L"取消申请"), TR_NOOP(L"结束控制") };
//控制键鼠的状态个数（share_viewer_control_state 的取值个数）
const int kControlStateCount = 3;
//标题栏控制图标（第 18 期）：鼠标指针，箭头部分的顶点与行范围、尾巴的列与行范围；三种状态三种颜色
//（没有申请为灰，申请中为橙，控制中为蓝）
const int kArrowTipX = 4;
const int kArrowTop = 2;
const int kArrowBottom = 11;
const int kArrowTailLeft = 7;
const int kArrowTailRight = 8;
const int kArrowTailTop = 9;
const int kArrowTailBottom = 14;
const BYTE kCtrlRequestedR = 230;
const BYTE kCtrlRequestedG = 140;
const BYTE kCtrlRequestedB = 20;
//「共享说明」的标题栏图标提示与全屏工具条按钮文字（第 15 期）
const wchar_t* const kInfoButtonText = TR_NOOP(L"共享说明");
//标题栏共享说明图标（第 15 期）：圆环加字母 i，灰色。坐标按半像素计（乘 2），圆心在图标正中：
//圆环内外半径、i 的点与竖笔所在的列与行（含两端）
const int kInfoRingInner2 = 11;
const int kInfoRingOuter2 = 14;
const int kInfoGlyphLeft = 7;
const int kInfoGlyphRight = 8;
const int kInfoDotTop = 4;
const int kInfoDotBottom = 5;
const int kInfoStemTop = 7;
const int kInfoStemBottom = 11;
const BYTE kInfoInk = 120;
//强制模式拉回前台的定时器（第 13 期）：每秒检查一次前台窗口
const UINT_PTR kPullBackTimerId = 203;
const UINT kPullBackTimerMs = 1000;
//右侧摄像头小窗条的宽度
const int kCameraPanelW = 240;
//摄像头小窗的刷新定时器：每 100 毫秒一次
const UINT_PTR kCameraTimerId = 202;
const UINT kCameraTimerMs = 100;
//标题栏摄像头图标各部分的像素范围（含两端）：机身、镜头三角
const int kCamBodyLeft = 2;
const int kCamBodyRight = 10;
const int kCamBodyTop = 4;
const int kCamBodyBottom = 11;
const int kCamLensLeft = 11;
const int kCamLensRight = 14;

//写一个预乘透明度的不透明像素
void setMicPixel(BYTE* px, int x, int y, BYTE r, BYTE g, BYTE b)
{
    if (x < 0 || y < 0 || x >= kMicIconSide || y >= kMicIconSide)
        return;
    BYTE* p = px + (static_cast<size_t>(y) * kMicIconSide + x) * kMicIconBpp;
    p[0] = b;
    p[1] = g;
    p[2] = r;
    p[3] = 255;
}

//画一个 16×16 的麦克风图标（透明底）：话筒头、托架、支杆与底座；关闭状态再加一道红斜线
HBITMAP makeMicIcon(bool on)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kMicIconSide;
    bi.bmiHeader.biHeight = -kMicIconSide;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
        return NULL;
    BYTE* px = static_cast<BYTE*>(bits);
    memset(px, 0, static_cast<size_t>(kMicIconSide) * kMicIconSide * kMicIconBpp);
    const BYTE ink = on ? kMicInkOn : kMicInkOff;
    for (int y = kMicHeadTop; y <= kMicHeadBottom; ++y)
    {
        for (int x = kMicHeadLeft; x <= kMicHeadRight; ++x)
            setMicPixel(px, x, y, ink, ink, ink);
    }
    for (int y = kMicCradleTop; y <= kMicCradleBottom; ++y)
    {
        setMicPixel(px, kMicCradleLeft, y, ink, ink, ink);
        setMicPixel(px, kMicCradleRight, y, ink, ink, ink);
    }
    for (int x = kMicCradleLeft; x <= kMicCradleRight; ++x)
        setMicPixel(px, x, kMicCradleBottom, ink, ink, ink);
    for (int y = kMicCradleBottom; y <= kMicStemBottom; ++y)
    {
        setMicPixel(px, kMicStemLeft, y, ink, ink, ink);
        setMicPixel(px, kMicStemRight, y, ink, ink, ink);
    }
    for (int x = kMicBaseLeft; x <= kMicBaseRight; ++x)
        setMicPixel(px, x, kMicStemBottom, ink, ink, ink);
    if (!on)
    {
        for (int t = kMicSlashFrom; t <= kMicSlashTo; ++t)
        {
            setMicPixel(px, t, t, kMicSlashR, kMicSlashG, kMicSlashB);
            setMicPixel(px, t + 1, t, kMicSlashR, kMicSlashG, kMicSlashB);
        }
    }
    return bmp;
}

//画一个 16×16 的摄像机图标（透明底）：机身方框与右侧的镜头三角；关闭状态再加一道红斜线
HBITMAP makeCameraIcon(bool on)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kMicIconSide;
    bi.bmiHeader.biHeight = -kMicIconSide;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
        return NULL;
    BYTE* px = static_cast<BYTE*>(bits);
    memset(px, 0, static_cast<size_t>(kMicIconSide) * kMicIconSide * kMicIconBpp);
    const BYTE ink = on ? kMicInkOn : kMicInkOff;
    for (int y = kCamBodyTop; y <= kCamBodyBottom; ++y)
    {
        for (int x = kCamBodyLeft; x <= kCamBodyRight; ++x)
        {
            //机身只描边：上下两行与左右两列
            if (y == kCamBodyTop || y == kCamBodyBottom || x == kCamBodyLeft || x == kCamBodyRight)
                setMicPixel(px, x, y, ink, ink, ink);
        }
    }
    //镜头：从机身右侧向外张开的三角，每往右一列上下各多一像素
    const int midY = (kCamBodyTop + kCamBodyBottom) / 2;
    for (int x = kCamLensLeft; x <= kCamLensRight; ++x)
    {
        const int spread = x - kCamLensLeft + 1;
        for (int y = midY - spread; y <= midY + 1 + spread; ++y)
            setMicPixel(px, x, y, ink, ink, ink);
    }
    if (!on)
    {
        for (int t = kMicSlashFrom; t <= kMicSlashTo; ++t)
        {
            setMicPixel(px, t, t, kMicSlashR, kMicSlashG, kMicSlashB);
            setMicPixel(px, t + 1, t, kMicSlashR, kMicSlashG, kMicSlashB);
        }
    }
    return bmp;
}

//画一个 16×16 的笔形图标（透明底）：一支从右上斜向左下的笔；active 时为蓝色
HBITMAP makePenIcon(bool active)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kMicIconSide;
    bi.bmiHeader.biHeight = -kMicIconSide;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
        return NULL;
    BYTE* px = static_cast<BYTE*>(bits);
    memset(px, 0, static_cast<size_t>(kMicIconSide) * kMicIconSide * kMicIconBpp);
    const BYTE r = active ? kPenActiveR : kMicInkOn;
    const BYTE g = active ? kPenActiveG : kMicInkOn;
    const BYTE b = active ? kPenActiveB : kMicInkOn;
    //笔杆：x + y = 常数的斜线，两像素粗
    for (int t = kPenFrom; t <= kPenTo; ++t)
    {
        setMicPixel(px, t, kMicIconSide - 1 - t, r, g, b);
        setMicPixel(px, t + 1, kMicIconSide - 1 - t, r, g, b);
    }
    setMicPixel(px, kPenTipX, kPenTipY, r, g, b);
    return bmp;
}

//画一个 16×16 的「新建白板」图标（透明底，第 23 期）：一块白板与一支斜放的笔。state 取值同 share_viewer_mine_state：
//没有白板为灰、申请中为橙、白板开着为蓝（与「共享我的屏幕」图标的配色一致）
HBITMAP makeBoardIcon(int state)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kMicIconSide;
    bi.bmiHeader.biHeight = -kMicIconSide;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
        return NULL;
    BYTE* px = static_cast<BYTE*>(bits);
    memset(px, 0, static_cast<size_t>(kMicIconSide) * kMicIconSide * kMicIconBpp);
    BYTE r = kMicInkOn;
    BYTE g = kMicInkOn;
    BYTE b = kMicInkOn;
    if (state == share_viewer_mine_requested)
    {
        r = kCtrlRequestedR;
        g = kCtrlRequestedG;
        b = kCtrlRequestedB;
    }
    else if (state == share_viewer_mine_sharing)
    {
        r = kPenActiveR;
        g = kPenActiveG;
        b = kPenActiveB;
    }
    //板面的边框
    for (int x = kBoardFrameLeft; x <= kBoardFrameRight; ++x)
    {
        setMicPixel(px, x, kBoardFrameTop, r, g, b);
        setMicPixel(px, x, kBoardFrameBottom, r, g, b);
    }
    for (int y = kBoardFrameTop; y <= kBoardFrameBottom; ++y)
    {
        setMicPixel(px, kBoardFrameLeft, y, r, g, b);
        setMicPixel(px, kBoardFrameRight, y, r, g, b);
    }
    //板面上一支斜放的笔（左下到右上）
    for (int i = kBoardPenFrom; i <= kBoardPenTo; ++i)
        setMicPixel(px, i, kBoardFrameBottom - (i - kBoardPenFrom) - 1, r, g, b);
    return bmp;
}

//画一个 16×16 的「共享我的屏幕」图标（透明底，第 22 期）：一台显示器。state 取 share_viewer_mine_state：没有申请为灰、
//申请中为橙、共享中为蓝（与控制图标的配色一致）
HBITMAP makeShareMineIcon(int state)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kMicIconSide;
    bi.bmiHeader.biHeight = -kMicIconSide;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
        return NULL;
    BYTE* px = static_cast<BYTE*>(bits);
    memset(px, 0, static_cast<size_t>(kMicIconSide) * kMicIconSide * kMicIconBpp);
    BYTE r = kMicInkOn;
    BYTE g = kMicInkOn;
    BYTE b = kMicInkOn;
    if (state == share_viewer_mine_requested)
    {
        r = kCtrlRequestedR;
        g = kCtrlRequestedG;
        b = kCtrlRequestedB;
    }
    else if (state == share_viewer_mine_sharing)
    {
        r = kPenActiveR;
        g = kPenActiveG;
        b = kPenActiveB;
    }
    //屏幕框
    for (int x = kMineScreenLeft; x <= kMineScreenRight; ++x)
    {
        setMicPixel(px, x, kMineScreenTop, r, g, b);
        setMicPixel(px, x, kMineScreenBottom, r, g, b);
    }
    for (int y = kMineScreenTop; y <= kMineScreenBottom; ++y)
    {
        setMicPixel(px, kMineScreenLeft, y, r, g, b);
        setMicPixel(px, kMineScreenRight, y, r, g, b);
    }
    //共享中：屏幕里填满，表示「正在出画面」
    if (state == share_viewer_mine_sharing)
    {
        for (int y = kMineScreenTop + 2; y <= kMineScreenBottom - 2; ++y)
        {
            for (int x = kMineScreenLeft + 2; x <= kMineScreenRight - 2; ++x)
                setMicPixel(px, x, y, r, g, b);
        }
    }
    //支架与底座
    for (int y = kMineScreenBottom + 1; y < kMineBaseRow; ++y)
    {
        for (int x = kMineStandLeft; x <= kMineStandRight; ++x)
            setMicPixel(px, x, y, r, g, b);
    }
    for (int x = kMineBaseLeft; x <= kMineBaseRight; ++x)
        setMicPixel(px, x, kMineBaseRow, r, g, b);
    return bmp;
}

//画一个 16×16 的控制图标（透明底，第 18 期）：鼠标指针。state 取 share_viewer_control_state：没有申请为灰、申请中为橙、
//控制中为蓝
HBITMAP makeControlIcon(int state)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kMicIconSide;
    bi.bmiHeader.biHeight = -kMicIconSide;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
        return NULL;
    BYTE* px = static_cast<BYTE*>(bits);
    memset(px, 0, static_cast<size_t>(kMicIconSide) * kMicIconSide * kMicIconBpp);
    BYTE r = kMicInkOn;
    BYTE g = kMicInkOn;
    BYTE b = kMicInkOn;
    if (state == share_viewer_control_requested)
    {
        r = kCtrlRequestedR;
        g = kCtrlRequestedG;
        b = kCtrlRequestedB;
    }
    else if (state == share_viewer_control_active)
    {
        r = kPenActiveR;
        g = kPenActiveG;
        b = kPenActiveB;
    }
    //箭头：左边竖直，每往下一行向右多出约三分之二像素，形成指向左上的三角
    for (int y = kArrowTop; y <= kArrowBottom; ++y)
    {
        const int right = kArrowTipX + (y - kArrowTop) * 2 / 3;
        for (int x = kArrowTipX; x <= right; ++x)
            setMicPixel(px, x, y, r, g, b);
    }
    //尾巴：两像素宽，往右下斜
    for (int y = kArrowTailTop; y <= kArrowTailBottom; ++y)
    {
        const int shift = (y - kArrowTailTop) / 2;
        for (int x = kArrowTailLeft; x <= kArrowTailRight; ++x)
            setMicPixel(px, x + shift, y, r, g, b);
    }
    return bmp;
}

//画一个 16×16 的录制图标（透明底）：居中的实心圆点；录制中为红色，否则为灰色
//画一个 16×16 的观看者图标（透明底，第 13 期）：圆形的头与半椭圆的肩，灰色
HBITMAP makePeopleIcon()
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kMicIconSide;
    bi.bmiHeader.biHeight = -kMicIconSide;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
        return NULL;
    BYTE* px = static_cast<BYTE*>(bits);
    memset(px, 0, static_cast<size_t>(kMicIconSide) * kMicIconSide * kMicIconBpp);
    const int bodyH = kPeopleBodyBottom - kPeopleBodyTop;
    for (int y = 0; y < kMicIconSide; ++y)
    {
        for (int x = 0; x < kMicIconSide; ++x)
        {
            const int hx = x - kPeopleHeadX;
            const int hy = y - kPeopleHeadY;
            bool ink = (hx * hx + hy * hy <= kPeopleHeadR * kPeopleHeadR);
            //肩：以肩部底边中点为圆心的上半椭圆
            if (!ink && y >= kPeopleBodyTop && y <= kPeopleBodyBottom)
            {
                const int bx = x - kPeopleHeadX;
                const int by = kPeopleBodyBottom - y;
                ink = (bx * bx * bodyH * bodyH + by * by * kPeopleBodyHalfW * kPeopleBodyHalfW <=
                       kPeopleBodyHalfW * kPeopleBodyHalfW * bodyH * bodyH);
            }
            if (ink)
                setMicPixel(px, x, y, kPeopleInk, kPeopleInk, kPeopleInk);
        }
    }
    return bmp;
}

//画一个 16×16 的共享说明图标（透明底，第 15 期）：灰色圆环里一个字母 i
HBITMAP makeInfoIcon()
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kMicIconSide;
    bi.bmiHeader.biHeight = -kMicIconSide;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
        return NULL;
    BYTE* px = static_cast<BYTE*>(bits);
    memset(px, 0, static_cast<size_t>(kMicIconSide) * kMicIconSide * kMicIconBpp);
    for (int y = 0; y < kMicIconSide; ++y)
    {
        for (int x = 0; x < kMicIconSide; ++x)
        {
            //像素中心到图标中心的距离，按半像素计
            const int dx2 = 2 * x + 1 - kMicIconSide;
            const int dy2 = 2 * y + 1 - kMicIconSide;
            const int d2 = dx2 * dx2 + dy2 * dy2;
            bool ink = (d2 >= kInfoRingInner2 * kInfoRingInner2 && d2 <= kInfoRingOuter2 * kInfoRingOuter2);
            if (!ink && x >= kInfoGlyphLeft && x <= kInfoGlyphRight)
                ink = (y >= kInfoDotTop && y <= kInfoDotBottom) || (y >= kInfoStemTop && y <= kInfoStemBottom);
            if (ink)
                setMicPixel(px, x, y, kInfoInk, kInfoInk, kInfoInk);
        }
    }
    return bmp;
}

//画一个 16×16 的皇冠图标（透明底，2026-10-07）：主持人入口
HBITMAP makeHostIcon()
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kMicIconSide;
    bi.bmiHeader.biHeight = -kMicIconSide;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
        return NULL;
    BYTE* px = static_cast<BYTE*>(bits);
    memset(px, 0, static_cast<size_t>(kMicIconSide) * kMicIconSide * kMicIconBpp);
    const size_t peakCount = sizeof(kCrownPeak2) / sizeof(kCrownPeak2[0]);
    for (int x = kCrownLeft; x <= kCrownRight; ++x)
    {
        //这一列中心到最近尖顶的距离（半像素）
        const int center2 = 2 * x + 1;
        int nearest = kMicIconSide * 2;
        for (size_t i = 0; i < peakCount; ++i)
        {
            const int d = (center2 > kCrownPeak2[i]) ? (center2 - kCrownPeak2[i]) : (kCrownPeak2[i] - center2);
            if (d < nearest)
                nearest = d;
        }
        for (int y = kCrownPeakTop + nearest; y <= kCrownBottom; ++y)
            setMicPixel(px, x, y, kCrownR, kCrownG, kCrownB);
    }
    return bmp;
}

//画一个 16×16 的字幕图标（透明底，语音字幕）：矩形框里两行横线；on 为真时为蓝色
HBITMAP makeCaptionIcon(bool on)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kMicIconSide;
    bi.bmiHeader.biHeight = -kMicIconSide;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
        return NULL;
    BYTE* px = static_cast<BYTE*>(bits);
    memset(px, 0, static_cast<size_t>(kMicIconSide) * kMicIconSide * kMicIconBpp);
    const BYTE r = on ? kPenActiveR : kCaptionIdleInk;
    const BYTE g = on ? kPenActiveG : kCaptionIdleInk;
    const BYTE b = on ? kPenActiveB : kCaptionIdleInk;
    for (int y = kCaptionBoxTop; y <= kCaptionBoxBottom; ++y)
    {
        for (int x = kCaptionBoxLeft; x <= kCaptionBoxRight; ++x)
        {
            const bool border = (y == kCaptionBoxTop || y == kCaptionBoxBottom || x == kCaptionBoxLeft
                                 || x == kCaptionBoxRight);
            const bool line1 = (y == kCaptionLine1Y && x >= kCaptionLine1Left && x <= kCaptionLine1Right);
            const bool line2 = (y == kCaptionLine2Y && x >= kCaptionLine2Left && x <= kCaptionLine2Right);
            if (border || line1 || line2)
                setMicPixel(px, x, y, r, g, b);
        }
    }
    return bmp;
}

HBITMAP makeRecordIcon(bool on)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kMicIconSide;
    bi.bmiHeader.biHeight = -kMicIconSide;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL)
        return NULL;
    BYTE* px = static_cast<BYTE*>(bits);
    memset(px, 0, static_cast<size_t>(kMicIconSide) * kMicIconSide * kMicIconBpp);
    const int c = kMicIconSide / 2;
    for (int y = 0; y < kMicIconSide; ++y)
    {
        for (int x = 0; x < kMicIconSide; ++x)
        {
            const int dx = x - c;
            const int dy = y - c;
            if (dx * dx + dy * dy <= kRecDotRadius * kRecDotRadius)
            {
                if (on)
                    setMicPixel(px, x, y, kRecOnR, kRecOnG, kRecOnB);
                else
                    setMicPixel(px, x, y, kRecIdleInk, kRecIdleInk, kRecIdleInk);
            }
        }
    }
    return bmp;
}

} // namespace

namespace {

//白板上滚轮一格滚动的距离（像素，第 23 期）
const int kBoardWheelStepPx = 48;

//用纯色填充矩形（第 23 期白板）
void fillSolid(HDC hdc, const RECT& rc, COLORREF color)
{
    HBRUSH brush = ::CreateSolidBrush(color);
    ::FillRect(hdc, &rc, brush);
    ::DeleteObject(brush);
}

//画一条水平或竖直的细线（第 23 期白板的背景线）
void drawThinLine(HDC hdc, int x0, int y0, int x1, int y1, COLORREF color)
{
    HPEN pen = ::CreatePen(PS_SOLID, 1, color);
    HGDIOBJ old = ::SelectObject(hdc, pen);
    ::MoveToEx(hdc, x0, y0, NULL);
    ::LineTo(hdc, x1, y1);
    ::SelectObject(hdc, old);
    ::DeleteObject(pen);
}

} // namespace

//==================== CShareVideoCanvas ====================

CShareVideoCanvas::CShareVideoCanvas()
    : m_width(0)
    , m_height(0)
    , m_annot(NULL)
    , m_annotInput(NULL)
    , m_annotating(false)
    , m_pressed(false)
    , m_hovering(false)
    , m_linkPath(0)
    , m_enlarged(-1)
    , m_screensGone(false)
    , m_annotCanvas(kAnnotDefaultCanvas)
{
    memset(&m_videoRect, 0, sizeof(m_videoRect));
    memset(&m_annotRect, 0, sizeof(m_annotRect));
    memset(&m_annotContent, 0, sizeof(m_annotContent));
}

CShareVideoCanvas::ScreenTile::ScreenTile()
    : m_seat(-1)
    , m_width(0)
    , m_height(0)
    , m_isBoard(false)
    , m_ownerSeat(-1)
    , m_viewReady(false)
    , m_follow(true)
{
    memset(&m_labelRect, 0, sizeof(m_labelRect));
}

void CShareVideoCanvas::SetScreens(const std::vector<int>& seats, const std::vector<std::wstring>& names)
{
    const int oldActive = ActiveScreenSeat();
    //清单从有到无：最后一帧照旧留着（与「对方已暂停共享」时一样），观看窗据此叠一行提示
    if (seats.empty())
        m_screensGone = m_screensGone || !m_tiles.empty();
    else
        m_screensGone = false;
    std::vector<ScreenTile> next;
    for (size_t i = 0; i < seats.size(); ++i)
    {
        ScreenTile t;
        t.m_seat = seats[i];
        t.m_name = (i < names.size()) ? names[i] : std::wstring();
        //还在清单里的画面保留已有的帧，免得闪黑；白板保留视野与跟随状态（第 23 期）
        for (size_t k = 0; k < m_tiles.size(); ++k)
        {
            if (m_tiles[k].m_seat == seats[i])
            {
                t.m_frame.swap(m_tiles[k].m_frame);
                t.m_width = m_tiles[k].m_width;
                t.m_height = m_tiles[k].m_height;
                t.m_isBoard = m_tiles[k].m_isBoard;
                t.m_ownerSeat = m_tiles[k].m_ownerSeat;
                t.m_view = m_tiles[k].m_view;
                t.m_viewReady = m_tiles[k].m_viewReady;
                t.m_follow = m_tiles[k].m_follow;
                break;
            }
        }
        next.push_back(t);
    }
    m_tiles.swap(next);
    //放大的那一块还在就继续放大，不在了就回到并列
    m_enlarged = -1;
    for (size_t i = 0; i < m_tiles.size() && oldActive >= 0; ++i)
    {
        if (m_tiles[i].m_seat == oldActive && m_tiles.size() > 1)
            m_enlarged = static_cast<int>(i);
    }
    //只剩一块（或没有）时回到原来的单画面：它的帧交给单画面缓冲
    if (m_tiles.size() == 1 && !m_tiles[0].m_frame.empty())
    {
        m_frame = m_tiles[0].m_frame;
        m_width = m_tiles[0].m_width;
        m_height = m_tiles[0].m_height;
    }
    Invalidate();
}

bool CShareVideoCanvas::SwapScreenFrame(int seat, std::vector<uint8_t>& bgra, int width, int height)
{
    for (size_t i = 0; i < m_tiles.size(); ++i)
    {
        if (m_tiles[i].m_seat != seat)
            continue;
        //只有一块：按原来的单画面绘制，帧同时记在单画面缓冲里
        if (m_tiles.size() == 1)
        {
            m_tiles[i].m_frame = bgra;
            m_tiles[i].m_width = width;
            m_tiles[i].m_height = height;
            SwapFrame(bgra, width, height);
            return true;
        }
        m_tiles[i].m_frame.swap(bgra);
        m_tiles[i].m_width = width;
        m_tiles[i].m_height = height;
        Invalidate();
        return true;
    }
    return false;
}

std::vector<int> CShareVideoCanvas::ScreenSeats() const
{
    std::vector<int> out;
    for (size_t i = 0; i < m_tiles.size(); ++i)
        out.push_back(m_tiles[i].m_seat);
    return out;
}

bool CShareVideoCanvas::ScreensGone() const
{
    return m_screensGone;
}

int CShareVideoCanvas::activeTileIndex() const
{
    if (m_tiles.empty())
        return -1;
    return (m_enlarged >= 0 && m_enlarged < static_cast<int>(m_tiles.size())) ? m_enlarged : 0;
}

int CShareVideoCanvas::ActiveScreenSeat() const
{
    const int i = activeTileIndex();
    return i >= 0 ? m_tiles[i].m_seat : -1;
}

void CShareVideoCanvas::SetBoards(const std::vector<int>& seats, const std::vector<int>& ownerSeats)
{
    for (size_t i = 0; i < m_tiles.size(); ++i)
    {
        bool board = false;
        int owner = -1;
        for (size_t k = 0; k < seats.size(); ++k)
        {
            if (seats[k] == m_tiles[i].m_seat)
            {
                board = true;
                owner = (k < ownerSeats.size()) ? ownerSeats[k] : -1;
                break;
            }
        }
        m_tiles[i].m_isBoard = board;
        m_tiles[i].m_ownerSeat = owner;
    }
    Invalidate();
}

bool CShareVideoCanvas::ActiveIsBoard() const
{
    const int i = activeTileIndex();
    return i >= 0 && m_tiles[i].m_isBoard;
}

bool CShareVideoCanvas::RenderActiveBoard(std::vector<uint8_t>& bgra, int& width, int& height)
{
    const int index = activeTileIndex();
    if (index < 0 || !m_tiles[index].m_isBoard)
        return false;
    ScreenTile& t = m_tiles[index];
    //按这一块现在的大小画，与屏幕上看到的可见区域一致；还没显示过时用默认尺寸
    const int w = t.m_viewReady ? t.m_view.ViewWidth() : kRecordBoardDefaultW;
    const int h = t.m_viewReady ? t.m_view.ViewHeight() : kRecordBoardDefaultH;
    if (w <= 0 || h <= 0)
        return false;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (dib == NULL || bits == NULL)
    {
        if (dib != NULL)
            ::DeleteObject(dib);
        return false;
    }
    HDC mem = ::CreateCompatibleDC(NULL);
    HGDIOBJ old = ::SelectObject(mem, dib);
    RECT area = { 0, 0, w, h };
    RECT content = area;
    drawBoardIn(mem, area, t, content);
    if (m_annot != NULL)
        DrawSessionCanvasInView(mem, area, content, *m_annot, t.m_seat, static_cast<int64_t>(::GetTickCount64()));
    ::GdiFlush();
    const size_t bytes = static_cast<size_t>(w) * h * kBgraBytes;
    bgra.assign(static_cast<const uint8_t*>(bits), static_cast<const uint8_t*>(bits) + bytes);
    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    ::DeleteObject(dib);
    //GDI 画进 32 位位图时不写 alpha：一律补成不透明
    for (size_t i = kAlphaByte; i < bytes; i += kBgraBytes)
        bgra[i] = kOpaqueAlpha;
    width = w;
    height = h;
    return true;
}

int CShareVideoCanvas::boardTileAt(POINT pt) const
{
    const int index = HitScreenTile(m_tileVideoRects, pt);
    if (index < 0 || index >= static_cast<int>(m_tiles.size()) || !m_tiles[index].m_isBoard)
        return -1;
    return index;
}

void CShareVideoCanvas::drawBoardIn(HDC hdc, const RECT& area, ScreenTile& t, RECT& content)
{
    content = area;
    fillSolid(hdc, area, kWhiteboardOutsideColor);
    const int w = area.right - area.left;
    const int h = area.bottom - area.top;
    if (w <= 0 || h <= 0)
        return;
    //第一次按这块的大小定下世界，之后大小变了按比例重算
    if (!t.m_viewReady)
    {
        t.m_view.InitShared(w, h);
        t.m_viewReady = true;
    }
    else if (w != t.m_view.ViewWidth() || h != t.m_view.ViewHeight())
    {
        t.m_view.SetViewSize(w, h);
    }
    //建立者广播的白板视野：背景；跟随时按他的可见区域显示
    int background = wb_bg_grid;
    AnnotationBoardView bv;
    if (m_annot != NULL && m_annot->Model().BoardView(t.m_seat, bv) && bv.m_senderSeat == t.m_ownerSeat)
    {
        background = bv.m_background;
        if (t.m_follow)
            t.m_view.ShowRegion(bv.m_left, bv.m_top, bv.m_width);
    }
    //画布（就是整个世界）：白底与背景线
    RECT canvas = t.m_view.CanvasRect();
    ::OffsetRect(&canvas, area.left, area.top);
    RECT visible;
    if (::IntersectRect(&visible, &canvas, &area))
    {
        fillSolid(hdc, visible, kWhiteboardCanvasColor);
        std::vector<int> xs;
        std::vector<int> ys;
        t.m_view.BackgroundLines(background, xs, ys);
        const COLORREF lineColor = (background == wb_bg_lines) ? kWhiteboardLineColor : kWhiteboardGridColor;
        for (size_t i = 0; i < xs.size(); ++i)
            drawThinLine(hdc, area.left + xs[i], visible.top, area.left + xs[i], visible.bottom, lineColor);
        for (size_t i = 0; i < ys.size(); ++i)
            drawThinLine(hdc, visible.left, area.top + ys[i], visible.right, area.top + ys[i], lineColor);
    }
    content = t.m_view.ContentRect();
    ::OffsetRect(&content, area.left, area.top);
}

bool CShareVideoCanvas::OnMouseWheel(POINT pt, short zDelta, UINT mkFlags)
{
    const int index = boardTileAt(pt);
    if (index < 0 || !m_tiles[index].m_viewReady)
        return false;
    ScreenTile& t = m_tiles[index];
    const RECT& area = m_tileVideoRects[index];
    //自己滚动或缩放：这块白板改为自由查看
    t.m_follow = false;
    if ((mkFlags & MK_CONTROL) != 0)
    {
        const double factor = pow(kWhiteboardZoomStep, static_cast<double>(zDelta) / WHEEL_DELTA);
        t.m_view.SetZoom(t.m_view.Zoom() * factor, pt.x - area.left, pt.y - area.top);
    }
    else
    {
        const int step = -zDelta * kBoardWheelStepPx / WHEEL_DELTA;
        const bool horizontal = (mkFlags & MK_SHIFT) != 0;
        t.m_view.ScrollBy(horizontal ? step : 0, horizontal ? 0 : step);
    }
    Invalidate();
    return true;
}

int CShareVideoCanvas::canvasOfTile(int index) const
{
    if (index >= 0 && index < static_cast<int>(m_tiles.size()))
        return m_tiles[index].m_seat;
    return kAnnotDefaultCanvas;
}

bool CShareVideoCanvas::pickAnnotTile(POINT pt)
{
    const int index = HitScreenTile(m_tileVideoRects, pt);
    if (index < 0 || ::IsRectEmpty(&m_tileVideoRects[index]))
        return false;
    m_annotRect = m_tileVideoRects[index];
    m_annotContent = (index < static_cast<int>(m_tileContentRects.size())) ? m_tileContentRects[index] : m_annotRect;
    m_annotCanvas = canvasOfTile(index);
    if (m_annotInput != NULL)
        m_annotInput->OnAnnotCanvas(m_annotCanvas);
    return true;
}

bool CShareVideoCanvas::ScreenVideoRect(int seat, RECT& out) const
{
    return PickScreenRect(ScreenSeats(), m_tileVideoRects, seat, out);
}

RECT CShareVideoCanvas::drawFrameIn(HDC hdc, const RECT& area, const std::vector<uint8_t>& frame, int width, int height)
{
    HBRUSH black = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
    RECT out = { 0, 0, 0, 0 };
    const int areaW = area.right - area.left;
    const int areaH = area.bottom - area.top;
    ::FillRect(hdc, &area, black);
    if (frame.empty() || width <= 0 || height <= 0 || areaW <= 0 || areaH <= 0)
        return out;
    //等比缩放进区域，四周黑边
    int drawW = areaW;
    int drawH = static_cast<int>(static_cast<long long>(height) * areaW / width);
    if (drawH > areaH)
    {
        drawH = areaH;
        drawW = static_cast<int>(static_cast<long long>(width) * areaH / height);
    }
    const int x = area.left + (areaW - drawW) / 2;
    const int y = area.top + (areaH - drawH) / 2;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    ::SetStretchBltMode(hdc, HALFTONE);
    ::SetBrushOrgEx(hdc, 0, 0, NULL);
    ::StretchDIBits(hdc, x, y, drawW, drawH, 0, 0, width, height, &frame[0], &bi, DIB_RGB_COLORS, SRCCOPY);
    out.left = x;
    out.top = y;
    out.right = x + drawW;
    out.bottom = y + drawH;
    return out;
}

void CShareVideoCanvas::paintScreens(HDC hdc, const RECT& rc)
{
    HBRUSH black = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
    ::FillRect(hdc, &rc, black);
    const std::vector<RECT> tiles = ComputeScreenTiles(rc, static_cast<int>(m_tiles.size()), m_enlarged, kTileGapPx);
    const int active = activeTileIndex();
    memset(&m_videoRect, 0, sizeof(m_videoRect));
    m_tileVideoRects.assign(m_tiles.size(), m_videoRect);
    m_tileContentRects.assign(m_tiles.size(), m_videoRect);
    HGDIOBJ oldFont = ::SelectObject(hdc, ::GetStockObject(DEFAULT_GUI_FONT));
    for (size_t i = 0; i < m_tiles.size() && i < tiles.size(); ++i)
    {
        ScreenTile& t = m_tiles[i];
        //白板（第 23 期）：整块都是它，标注按世界换算；画面：等比画进来，标注按画面所占的矩形换算
        RECT video = tiles[i];
        RECT content = tiles[i];
        if (t.m_isBoard)
        {
            drawBoardIn(hdc, tiles[i], t, content);
        }
        else
        {
            video = drawFrameIn(hdc, tiles[i], t.m_frame, t.m_width, t.m_height);
            content = video;
        }
        m_tileVideoRects[i] = video;
        m_tileContentRects[i] = content;
        if (static_cast<int>(i) == active)
            m_videoRect = video;
        //名字标签：左上角半透明深色底、白字；当前那一块描一圈品牌蓝。白板标出跟随状态（点一下切换）
        std::wstring label = t.m_name;
        if (t.m_isBoard)
            label += L"  " + std::wstring(t.m_follow ? TR(L"跟随创建者") : TR(L"自由查看"));
        else if (t.m_frame.empty())
            label += L"  " + std::wstring(TR(L"等待画面…"));
        RECT textRc = { 0, 0, 0, 0 };
        ::DrawTextW(hdc, label.c_str(), -1, &textRc, DT_CALCRECT | DT_SINGLELINE);
        RECT box = { tiles[i].left + kTileLabelMargin, tiles[i].top + kTileLabelMargin, 0, 0 };
        box.right = box.left + (textRc.right - textRc.left) + 2 * kTileLabelPadX;
        box.bottom = box.top + (textRc.bottom - textRc.top) + 2 * kTileLabelPadY;
        HBRUSH dark = ::CreateSolidBrush(kTileLabelBg);
        ::FillRect(hdc, &box, dark);
        ::DeleteObject(dark);
        t.m_labelRect = box;
        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextColor(hdc, kTileLabelText);
        ::DrawTextW(hdc, label.c_str(), -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (static_cast<int>(i) == active && m_tiles.size() > 1)
        {
            HBRUSH accent = ::CreateSolidBrush(kTileActiveBorder);
            ::FrameRect(hdc, &tiles[i], accent);
            ::DeleteObject(accent);
        }
    }
    ::SelectObject(hdc, oldFont);
    //正在接标注的那块画面换了位置（窗口缩放、放大或还原、白板滚动缩放）：矩形跟着更新
    for (size_t i = 0; i < m_tiles.size() && i < m_tileVideoRects.size(); ++i)
    {
        if (m_tiles[i].m_seat == m_annotCanvas)
        {
            m_annotRect = m_tileVideoRects[i];
            m_annotContent = m_tileContentRects[i];
        }
    }
}

void CShareVideoCanvas::SetLinkPath(int linkPath)
{
    if (linkPath == m_linkPath)
        return;
    m_linkPath = linkPath;
    Invalidate();
}

void CShareVideoCanvas::SetAnnotation(const ShareAnnotationSession* session)
{
    m_annot = session;
    Invalidate();
}

void CShareVideoCanvas::SetAnnotating(bool on, IShareAnnotationInput* input)
{
    //退出时若还按着左键，先按松开收尾
    if (!on && m_pressed)
    {
        m_pressed = false;
        ReleaseCapture();
    }
    m_annotating = on;
    m_annotInput = on ? input : NULL;
}

RECT CShareVideoCanvas::VideoRect() const
{
    return m_videoRect;
}

void CShareVideoCanvas::deliverPointer(int phase, POINT pt)
{
    if (m_annotInput == NULL || m_annotContent.right <= m_annotContent.left || m_annotContent.bottom <= m_annotContent.top)
        return;
    POINT screen = pt;
    if (GetHost() != NULL)
        ::ClientToScreen(GetHost()->m_hWnd, &screen);
    //按标注换算用的矩形换算：画面为画面所占的矩形，白板为整个世界（第 23 期）
    const double aspect = static_cast<double>(m_annotContent.right - m_annotContent.left) /
                          (m_annotContent.bottom - m_annotContent.top);
    m_annotInput->OnAnnotPointer(phase, ScreenToAnnot(m_annotContent, pt),
                                 AnnotTolerance(m_annotContent, kAnnotEraserTolerancePx), aspect, screen);
}

bool CShareVideoCanvas::OnLButtonDown(POINT pt, UINT mkFlags)
{
    (void)mkFlags;
    //白板的名字标签（第 23 期）：点一下切换「跟随创建者」
    for (size_t i = 0; i < m_tiles.size(); ++i)
    {
        if (m_tiles[i].m_isBoard && ::PtInRect(&m_tiles[i].m_labelRect, pt))
        {
            m_tiles[i].m_follow = !m_tiles[i].m_follow;
            Invalidate();
            return true;
        }
    }
    //按在哪块画面上就画在哪块画面上（第 22 期 22e）
    if (!m_annotating || !HasFrame() || !pickAnnotTile(pt))
        return false;
    m_pressed = true;
    Capture();
    deliverPointer(annot_pointer_down, pt);
    return true;
}

bool CShareVideoCanvas::OnMouseMove(POINT pt, UINT mkFlags)
{
    (void)mkFlags;
    if (m_pressed)
    {
        deliverPointer(annot_pointer_move, pt);
        return true;
    }
    //标注模式下没按着：在画面上时报悬停（广播本人的画笔位置），移出画面时报离开（第 19 期）
    if (!m_annotating || !HasFrame())
        return false;
    if (pickAnnotTile(pt))
    {
        m_hovering = true;
        deliverPointer(annot_pointer_hover, pt);
        return true;
    }
    if (m_hovering)
    {
        m_hovering = false;
        deliverPointer(annot_pointer_leave, pt);
    }
    return false;
}

bool CShareVideoCanvas::OnLButtonDblClk(POINT pt, UINT mkFlags)
{
    (void)mkFlags;
    //多人共享（第 22 期）：不在标注时双击某一块放大，再双击还原
    if (!m_annotating && m_tiles.size() > 1)
    {
        const std::vector<RECT> tiles = ComputeScreenTiles(GetRect(), static_cast<int>(m_tiles.size()), m_enlarged,
                                                           kTileGapPx);
        const int hit = HitScreenTile(tiles, pt);
        if (hit < 0)
            return false;
        m_enlarged = (m_enlarged == hit) ? -1 : hit;
        Invalidate();
        return true;
    }
    if (!m_annotating || !HasFrame() || !::PtInRect(&m_annotRect, pt))
        return false;
    deliverPointer(annot_pointer_finish, pt);
    return true;
}

bool CShareVideoCanvas::OnRButtonDown(POINT pt, UINT mkFlags)
{
    (void)mkFlags;
    if (!m_annotating || !HasFrame() || !::PtInRect(&m_annotRect, pt))
        return false;
    deliverPointer(annot_pointer_finish, pt);
    return true;
}

bool CShareVideoCanvas::OnMouseLeave()
{
    if (!m_hovering)
        return false;
    m_hovering = false;
    POINT pt = { m_annotRect.left, m_annotRect.top };
    deliverPointer(annot_pointer_leave, pt);
    return true;
}

bool CShareVideoCanvas::OnLButtonUp(POINT pt, UINT mkFlags)
{
    (void)mkFlags;
    if (!m_pressed)
        return false;
    m_pressed = false;
    ReleaseCapture();
    deliverPointer(annot_pointer_up, pt);
    return true;
}

bool CShareVideoCanvas::OnSetCursor(POINT pt)
{
    if (!m_annotating || HitScreenTile(m_tileVideoRects, pt) < 0)
        return false;
    //当前工具的光标（第 19 期）；取不到时用十字
    HCURSOR cursor = (m_annotInput != NULL) ? m_annotInput->AnnotCursor() : NULL;
    ::SetCursor(cursor != NULL ? cursor : ::LoadCursor(NULL, IDC_CROSS));
    return true;
}

void CShareVideoCanvas::SwapFrame(std::vector<uint8_t>& bgra, int width, int height)
{
    m_frame.swap(bgra);
    m_width = width;
    m_height = height;
    Invalidate();
}

void CShareVideoCanvas::SetStatusText(const std::wstring& text)
{
    if (text == m_status)
        return;
    m_status = text;
    Invalidate();
}

bool CShareVideoCanvas::HasFrame() const
{
    if (!m_frame.empty() && m_width > 0 && m_height > 0)
        return true;
    //多人共享（第 22 期）：任何一块有画面都算；白板（第 23 期）不需要视频，也算
    for (size_t i = 0; i < m_tiles.size(); ++i)
    {
        if (!m_tiles[i].m_frame.empty() || m_tiles[i].m_isBoard)
            return true;
    }
    return false;
}

void CShareVideoCanvas::OnPaint(HDC hdc, const RECT& rcDirty)
{
    (void)rcDirty;
    const RECT& rc = GetRect();
    HBRUSH black = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
    const int areaW = rc.right - rc.left;
    const int areaH = rc.bottom - rc.top;
    //多人共享（第 22 期）：两块及以上画面按布局分块画，每块叠它自己的标注（22e）；只有一块白板（第 23 期）时同样走这里
    const bool multi = m_tiles.size() > 1 || (m_tiles.size() == 1 && m_tiles[0].m_isBoard);
    if (multi && areaW > 0 && areaH > 0)
    {
        paintScreens(hdc, rc);
        if (m_annot != NULL)
        {
            const int64_t now = static_cast<int64_t>(::GetTickCount64());
            for (size_t i = 0; i < m_tileVideoRects.size() && i < m_tiles.size(); ++i)
            {
                if (::IsRectEmpty(&m_tileVideoRects[i]))
                    continue;
                //白板的世界可能比这一块大很多：只按看得见的部分画
                if (m_tiles[i].m_isBoard)
                    DrawSessionCanvasInView(hdc, m_tileVideoRects[i], m_tileContentRects[i], *m_annot,
                                            canvasOfTile(static_cast<int>(i)), now);
                else
                    DrawSessionCanvas(hdc, m_tileVideoRects[i], *m_annot, canvasOfTile(static_cast<int>(i)), false, now);
            }
        }
    }
    else if (!HasFrame() || areaW <= 0 || areaH <= 0)
    {
        ::FillRect(hdc, &rc, black);
        memset(&m_annotRect, 0, sizeof(m_annotRect));
        memset(&m_annotContent, 0, sizeof(m_annotContent));
        m_tileVideoRects.clear();
        m_tileContentRects.clear();
    }
    else
    {
        //等比缩放进控件矩形，四周黑边
        int drawW = areaW;
        int drawH = static_cast<int>(static_cast<long long>(m_height) * areaW / m_width);
        if (drawH > areaH)
        {
            drawH = areaH;
            drawW = static_cast<int>(static_cast<long long>(m_width) * areaH / m_height);
        }
        const int x = rc.left + (areaW - drawW) / 2;
        const int y = rc.top + (areaH - drawH) / 2;
        RECT top = { rc.left, rc.top, rc.right, y };
        RECT bottom = { rc.left, y + drawH, rc.right, rc.bottom };
        RECT left = { rc.left, y, x, y + drawH };
        RECT right = { x + drawW, y, rc.right, y + drawH };
        ::FillRect(hdc, &top, black);
        ::FillRect(hdc, &bottom, black);
        ::FillRect(hdc, &left, black);
        ::FillRect(hdc, &right, black);

        BITMAPINFO bi;
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = m_width;
        bi.bmiHeader.biHeight = -m_height;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = kBitsPerPixel32;
        bi.bmiHeader.biCompression = BI_RGB;
        //屏幕内容多是文字，缩放用半色调模式更清晰
        ::SetStretchBltMode(hdc, HALFTONE);
        ::SetBrushOrgEx(hdc, 0, 0, NULL);
        ::StretchDIBits(hdc, x, y, drawW, drawH, 0, 0, m_width, m_height, &m_frame[0], &bi, DIB_RGB_COLORS, SRCCOPY);
        m_videoRect.left = x;
        m_videoRect.top = y;
        m_videoRect.right = x + drawW;
        m_videoRect.bottom = y + drawH;
        //单画面：这块画面就是标注落笔的地方；它的座位取清单里唯一的那一块，没有清单时为默认画面（22e）
        m_tileVideoRects.assign(1, m_videoRect);
        m_tileContentRects.assign(1, m_videoRect);
        if (m_annotCanvas == canvasOfTile(0))
        {
            m_annotRect = m_videoRect;
            m_annotContent = m_videoRect;
        }
        //标注叠在画面上，坐标按画面实际所占的矩形换算：这块画面的笔迹、别人的画笔位置与名字（第 19 期），
        //以及本人正在画的预览与选中框（第 20 期）
        if (m_annot != NULL)
            DrawSessionCanvas(hdc, m_videoRect, *m_annot, canvasOfTile(0), false, static_cast<int64_t>(::GetTickCount64()));
        //数据经直连链路传输时，画面左上角叠一枚「P2P」徽章
        if (balloonmain::ShouldShowP2PBadge(m_linkPath))
            balloonmain::DrawP2PBadgeOverlay(hdc, m_videoRect);
    }

    if (m_status.empty())
        return;
    //提示文字：画面中央一块半透明深色底
    HGDIOBJ oldFont = ::SelectObject(hdc, ::GetStockObject(DEFAULT_GUI_FONT));
    RECT textRc = { 0, 0, 0, 0 };
    ::DrawTextW(hdc, m_status.c_str(), -1, &textRc, DT_CALCRECT | DT_SINGLELINE);
    const int boxW = (textRc.right - textRc.left) + 2 * kStatusPadX;
    const int boxH = (textRc.bottom - textRc.top) + 2 * kStatusPadY;
    RECT box = { rc.left + (areaW - boxW) / 2, rc.top + (areaH - boxH) / 2, 0, 0 };
    box.right = box.left + boxW;
    box.bottom = box.top + boxH;

    HDC memDc = ::CreateCompatibleDC(hdc);
    HBITMAP solid = ::CreateCompatibleBitmap(hdc, 1, 1);
    HGDIOBJ oldBmp = ::SelectObject(memDc, solid);
    ::SetPixel(memDc, 0, 0, kStatusBg);
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, kStatusAlpha, 0 };
    ::AlphaBlend(hdc, box.left, box.top, boxW, boxH, memDc, 0, 0, 1, 1, blend);
    ::SelectObject(memDc, oldBmp);
    ::DeleteObject(solid);
    ::DeleteDC(memDc);

    ::SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, kStatusText);
    ::DrawTextW(hdc, m_status.c_str(), -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    ::SelectObject(hdc, oldFont);
}

//==================== CShareViewerWindow ====================

CShareViewerWindow::CShareViewerWindow()
    : m_canvas(NULL)
    , m_notifyWnd(NULL)
    , m_lastSerial(0)
    , m_hostOnline(true)
    , m_sourceState(screenshare_source_sharing)
    , m_relayBroken(false)
    , m_framesSinceTick(0)
    , m_lastFps(0)
    , m_micOn(false)
    , m_micOnIcon(NULL)
    , m_micOffIcon(NULL)
    , m_micIconId(0)
    , m_cameraOn(false)
    , m_camOnIcon(NULL)
    , m_camOffIcon(NULL)
    , m_camIconId(0)
    , m_cameraPanel(NULL)
    , m_cameraSource(NULL)
    , m_annotSession(NULL)
    , m_annotInput(NULL)
    , m_annotating(false)
    , m_annotPointer(false)
    , m_penIcon(NULL)
    , m_penOnIcon(NULL)
    , m_penIconId(0)
    , m_recording(false)
    , m_recordDotOn(false)
    , m_recordBoardW(0)
    , m_recordBoardH(0)
    , m_recordBoardSerial(0)
    , m_recordBoardActive(false)
    , m_recIcon(NULL)
    , m_recOnIcon(NULL)
    , m_recIconId(0)
    , m_viewerCount(0)
    , m_forced(false)
    , m_cloudRecording(false)
    , m_cloudRecordPaused(false)
    , m_pullBackFailed(false)
    , m_peopleIcon(NULL)
    , m_peopleIconId(0)
    , m_infoIcon(NULL)
    , m_infoIconId(0)
    , m_controlState(share_viewer_control_none)
    , m_controlInput(NULL)
    , m_ctrlIconId(0)
    , m_mineState(share_viewer_mine_none)
    , m_mineIconId(0)
    , m_boardState(share_viewer_mine_none)
    , m_boardIconId(0)
    , m_captionAvailable(false)
    , m_captionSelf(false)
    , m_captionShow(true)
    , m_captionIcon(NULL)
    , m_captionOnIcon(NULL)
    , m_captionIconId(0)
    , m_hostTools(false)
    , m_hostRecordAllowed(true)
    , m_hostAnnotAllowed(true)
    , m_hostIcon(NULL)
    , m_hostIconId(0)
    , m_controlScreen(-1)
{
    for (int i = 0; i < kControlStateCount; ++i)
        m_ctrlIcons[i] = NULL;
    for (int i = 0; i < kShareMineStateCount; ++i)
        m_mineIcons[i] = NULL;
    for (int i = 0; i < kShareMineStateCount; ++i)
        m_boardIcons[i] = NULL;
}

CShareViewerWindow::~CShareViewerWindow()
{
    CloseViewer();
    for (int i = 0; i < kControlStateCount; ++i)
    {
        if (m_ctrlIcons[i] != NULL)
            ::DeleteObject(m_ctrlIcons[i]);
    }
    for (int i = 0; i < kShareMineStateCount; ++i)
    {
        if (m_mineIcons[i] != NULL)
            ::DeleteObject(m_mineIcons[i]);
        if (m_boardIcons[i] != NULL)
            ::DeleteObject(m_boardIcons[i]);
    }
    if (m_micOnIcon != NULL)
        ::DeleteObject(m_micOnIcon);
    if (m_micOffIcon != NULL)
        ::DeleteObject(m_micOffIcon);
    if (m_camOnIcon != NULL)
        ::DeleteObject(m_camOnIcon);
    if (m_camOffIcon != NULL)
        ::DeleteObject(m_camOffIcon);
    if (m_penIcon != NULL)
        ::DeleteObject(m_penIcon);
    if (m_penOnIcon != NULL)
        ::DeleteObject(m_penOnIcon);
    if (m_recIcon != NULL)
        ::DeleteObject(m_recIcon);
    if (m_recOnIcon != NULL)
        ::DeleteObject(m_recOnIcon);
    if (m_peopleIcon != NULL)
        ::DeleteObject(m_peopleIcon);
    if (m_infoIcon != NULL)
        ::DeleteObject(m_infoIcon);
    if (m_captionIcon != NULL)
        ::DeleteObject(m_captionIcon);
    if (m_captionOnIcon != NULL)
        ::DeleteObject(m_captionOnIcon);
    if (m_hostIcon != NULL)
        ::DeleteObject(m_hostIcon);
}

void CShareViewerWindow::EnsureWindow()
{
    if (IsWindow())
    {
        //字幕菜单弹出期间被关闭、等待销毁的窗口：直接复用并取消推迟的销毁（否则 m_hWnd 不为空，再 Create 会触发 atlwin.h
        //的断言）。CloseViewer 已清掉画面控件与标题栏图标，按当前状态重建
        if (m_captionPopup.TakePendingDestroy())
        {
            LOG_INFO("screenshare viewer: window reused while its caption menu is closing.");
            buildContent();
        }
        return;
    }
    if (Create(NULL, CWindow::rcDefault, TR(L"屏幕共享"), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("screenshare: create viewer window failed.");
        return;
    }
    SetTitleBarHeight(kTitleBarH);
    SetButtons(true, true, true);
    SetResizable(true);
    SetMinSize(kMinW, kMinH);
    ResizeClient(kWindowW, kWindowH);
    //标题栏左侧的程序图标（2026-10-07，开发约定文档 UI 约定第 10 条）：标题栏不透明，底色与它一致
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    //没有所有者、会出现在任务栏：任务栏与 Alt-Tab 也用程序图标
    m_programIcons.ApplyTaskIcons(m_hWnd);
    buildContent();
}

void CShareViewerWindow::buildContent()
{
    //画面占满左侧；右侧是摄像头小窗条，没有人开摄像头时收起
    std::unique_ptr<balloonwjui::DuiHBox> root(new balloonwjui::DuiHBox());
    std::unique_ptr<CShareVideoCanvas> canvas(new CShareVideoCanvas());
    canvas->SetAnnotation(m_annotSession);
    canvas->SetAnnotating(m_annotating && !m_annotPointer, m_annotInput);
    //多人共享（第 22 期）：窗口重建时沿用已知的画面清单与白板（第 23 期）
    canvas->SetScreens(m_screenSeats, m_screenNames);
    canvas->SetBoards(m_boardSeats, m_boardOwnerSeats);
    m_canvas = canvas.get();
    root->AddChild(std::move(canvas), balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<CShareCameraPanel> panel(new CShareCameraPanel());
    panel->SetSource(m_cameraSource);
    panel->SetVisible(false);
    m_cameraPanel = panel.get();
    root->AddChild(std::move(panel), balloonwjui::DuiLayout::Hint().Fixed(kCameraPanelW));
    SetClientContent(std::move(root));
    refreshMicIcon();
}

void CShareViewerWindow::refreshMicIcon()
{
    if (!IsWindow())
        return;
    if (m_micOnIcon == NULL)
        m_micOnIcon = makeMicIcon(true);
    if (m_micOffIcon == NULL)
        m_micOffIcon = makeMicIcon(false);
    if (m_micIconId != 0)
        RemoveCaptionIcon(m_micIconId);
    m_micIconId = 0;
    HBITMAP icon = m_micOn ? m_micOnIcon : m_micOffIcon;
    if (icon != NULL)
        m_micIconId = AddCaptionIcon(icon, m_micOn ? TR(kMicOnButtonText) : TR(kMicOffButtonText));
    //摄像头图标跟在麦克风图标后面，一起重放以保持先后顺序
    if (m_camOnIcon == NULL)
        m_camOnIcon = makeCameraIcon(true);
    if (m_camOffIcon == NULL)
        m_camOffIcon = makeCameraIcon(false);
    if (m_camIconId != 0)
        RemoveCaptionIcon(m_camIconId);
    m_camIconId = 0;
    HBITMAP camIcon = m_cameraOn ? m_camOnIcon : m_camOffIcon;
    if (camIcon != NULL)
        m_camIconId = AddCaptionIcon(camIcon, m_cameraOn ? TR(kCamOnButtonText) : TR(kCamOffButtonText));
    //标注图标排在最后
    if (m_penIcon == NULL)
        m_penIcon = makePenIcon(false);
    if (m_penOnIcon == NULL)
        m_penOnIcon = makePenIcon(true);
    if (m_penIconId != 0)
        RemoveCaptionIcon(m_penIconId);
    m_penIconId = 0;
    HBITMAP penIcon = m_annotating ? m_penOnIcon : m_penIcon;
    if (penIcon != NULL)
        m_penIconId = AddCaptionIcon(penIcon, m_annotating ? TR(kAnnotExitButtonText) : TR(kAnnotButtonText));
    //录制图标排在最后
    if (m_recIcon == NULL)
        m_recIcon = makeRecordIcon(false);
    if (m_recOnIcon == NULL)
        m_recOnIcon = makeRecordIcon(true);
    if (m_recIconId != 0)
        RemoveCaptionIcon(m_recIconId);
    m_recIconId = 0;
    HBITMAP recIcon = m_recording ? m_recOnIcon : m_recIcon;
    if (recIcon != NULL)
        m_recIconId = AddCaptionIcon(recIcon, m_recording ? TR(kRecordStopButtonText) : TR(kRecordButtonText));
    //字幕图标（语音字幕）：跟在录制图标后面，字幕不能使用时不放；为本人生成字幕时为蓝色
    if (m_captionIconId != 0)
        RemoveCaptionIcon(m_captionIconId);
    m_captionIconId = 0;
    if (m_captionAvailable)
    {
        if (m_captionIcon == NULL)
            m_captionIcon = makeCaptionIcon(false);
        if (m_captionOnIcon == NULL)
            m_captionOnIcon = makeCaptionIcon(true);
        HBITMAP captionIcon = m_captionSelf ? m_captionOnIcon : m_captionIcon;
        if (captionIcon != NULL)
            m_captionIconId = AddCaptionIcon(captionIcon, TR(kCaptionButtonText));
    }
    //控制图标（第 18 期）：鼠标指针，颜色随控制状态变化，点它申请控制、取消申请或结束控制
    const int ctrlState = (m_controlState >= 0 && m_controlState < kControlStateCount) ? m_controlState
                                                                                       : share_viewer_control_none;
    if (m_ctrlIcons[ctrlState] == NULL)
        m_ctrlIcons[ctrlState] = makeControlIcon(ctrlState);
    if (m_ctrlIconId != 0)
        RemoveCaptionIcon(m_ctrlIconId);
    m_ctrlIconId = 0;
    if (m_ctrlIcons[ctrlState] != NULL)
        m_ctrlIconId = AddCaptionIcon(m_ctrlIcons[ctrlState], TR(kControlButtonTexts[ctrlState]));
    //「共享我的屏幕」图标（第 22 期）：显示器，颜色随状态变化，点它申请共享、取消申请或停止自己的共享
    const int mineState = (m_mineState >= 0 && m_mineState < kShareMineStateCount) ? m_mineState : share_viewer_mine_none;
    if (m_mineIcons[mineState] == NULL)
        m_mineIcons[mineState] = makeShareMineIcon(mineState);
    if (m_mineIconId != 0)
        RemoveCaptionIcon(m_mineIconId);
    m_mineIconId = 0;
    if (m_mineIcons[mineState] != NULL)
        m_mineIconId = AddCaptionIcon(m_mineIcons[mineState], TR(kShareMineButtonTexts[mineState]));
    //「新建白板」图标（第 23 期）：一块白板，颜色随状态变化，点它新建白板、取消申请或关闭自己的白板
    const int boardState = (m_boardState >= 0 && m_boardState < kShareMineStateCount) ? m_boardState
                                                                                      : share_viewer_mine_none;
    if (m_boardIcons[boardState] == NULL)
        m_boardIcons[boardState] = makeBoardIcon(boardState);
    if (m_boardIconId != 0)
        RemoveCaptionIcon(m_boardIconId);
    m_boardIconId = 0;
    if (m_boardIcons[boardState] != NULL)
        m_boardIconId = AddCaptionIcon(m_boardIcons[boardState], TR(kBoardButtonTexts[boardState]));
    //观看者图标（第 13 期）：窗口模式下点它打开观看者名单
    if (m_peopleIcon == NULL)
        m_peopleIcon = makePeopleIcon();
    if (m_peopleIconId != 0)
        RemoveCaptionIcon(m_peopleIconId);
    m_peopleIconId = 0;
    if (m_peopleIcon != NULL)
        m_peopleIconId = AddCaptionIcon(m_peopleIcon, TR(L"观看者"));
    //共享说明图标（第 15 期）：只在共享有描述时放，点它打开「共享说明」浮窗
    if (m_infoIcon == NULL)
        m_infoIcon = makeInfoIcon();
    if (m_infoIconId != 0)
        RemoveCaptionIcon(m_infoIconId);
    m_infoIconId = 0;
    if (m_infoIcon != NULL && !m_shareDesc.empty())
        m_infoIconId = AddCaptionIcon(m_infoIcon, TR(kInfoButtonText));
    //皇冠图标（2026-10-07）：只在经移交成为主持人时放，点它弹出主持人菜单
    if (m_hostIconId != 0)
        RemoveCaptionIcon(m_hostIconId);
    m_hostIconId = 0;
    if (m_hostTools)
    {
        if (m_hostIcon == NULL)
            m_hostIcon = makeHostIcon();
        if (m_hostIcon != NULL)
            m_hostIconId = AddCaptionIcon(m_hostIcon, TR(kHostIconTip));
    }
}

void CShareViewerWindow::SetCaptionSink(ShareCaptionSink* sink)
{
    m_receiver.SetCaptionSink(sink);
}

void CShareViewerWindow::SetCaptionState(bool available, bool selfOn, bool showOn)
{
    if (available == m_captionAvailable && selfOn == m_captionSelf && showOn == m_captionShow)
        return;
    m_captionAvailable = available;
    m_captionSelf = selfOn;
    m_captionShow = showOn;
    refreshMicIcon();
}

bool CShareViewerWindow::CaptionArea(RECT& out) const
{
    if (!IsWindow() || !::IsWindowVisible(m_hWnd) || ::IsIconic(m_hWnd) || m_canvas == NULL)
        return false;
    //画面控件的矩形是客户区坐标（本窗口的客户区就是整个窗口），换成屏幕坐标
    const RECT& rc = m_canvas->GetRect();
    POINT topLeft = { rc.left, rc.top };
    POINT bottomRight = { rc.right, rc.bottom };
    ::ClientToScreen(m_hWnd, &topLeft);
    ::ClientToScreen(m_hWnd, &bottomRight);
    out.left = topLeft.x;
    out.top = topLeft.y;
    out.right = bottomRight.x;
    out.bottom = bottomRight.y;
    return out.right > out.left && out.bottom > out.top;
}

void CShareViewerWindow::showCaptionMenu()
{
    //已经有菜单开着、窗口正等待销毁（共享已结束）时不再弹
    if (!IsWindow() || !m_captionAvailable || m_notifyWnd == NULL || !::IsWindow(m_notifyWnd)
        || m_captionPopup.IsActive() || m_captionPopup.IsPendingDestroy())
        return;
    balloonwjui::DuiMenu menu;
    menu.AppendChecked(kCaptionMenuSelf, TR(L"为我的发言生成字幕"), m_captionSelf);
    menu.AppendChecked(kCaptionMenuShow, TR(L"显示字幕"), m_captionShow);
    //菜单弹在光标处（光标此时就在刚点过的标题栏图标上），越出工作区时由 DuiMenu 自己调整
    POINT pt = { 0, 0 };
    ::GetCursorPos(&pt);
    //记下正在弹出的菜单：期间共享结束、观看窗被关闭时据此关掉它
    const std::vector<std::unique_ptr<balloonwjui::DuiMenu> > noSubMenus;
    m_captionPopup.BeginMenu(&menu, noSubMenus);
    const UINT chosen = menu.TrackPopup(pt.x, pt.y, m_hWnd);
    m_captionPopup.End();
    //菜单期间控制器关闭了观看窗：现在才销毁窗口（本条消息是投递来的，返回后 ATL 即清空 m_hWnd）
    if (m_captionPopup.TakePendingDestroy())
    {
        LOG_INFO("screenshare viewer: window destroyed after its caption menu closed.");
        DestroyWindow();
        return;
    }
    //菜单期间观看窗被关闭过（共享已结束，可能又加入了下一场）或通知窗口没了：丢弃这次选择
    if (!ShareBarShouldDeliverChoice(chosen, m_captionPopup.WasInterrupted(),
                                     m_notifyWnd != NULL && ::IsWindow(m_notifyWnd)))
        return;
    if (chosen == kCaptionMenuSelf)
        ::PostMessage(m_notifyWnd, kMsgShareViewerCommand, kCmdCaptionSelf, m_captionSelf ? 0 : 1);
    else if (chosen == kCaptionMenuShow)
        ::PostMessage(m_notifyWnd, kMsgShareViewerCommand, kCmdCaptionShow, m_captionShow ? 0 : 1);
}

void CShareViewerWindow::requestViewersToggle()
{
    if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
        ::PostMessage(m_notifyWnd, kMsgShareViewerCommand, kCmdViewers, 0);
}

void CShareViewerWindow::SetViewerCount(int count)
{
    if (count == m_viewerCount)
        return;
    m_viewerCount = count;
    m_toolbar.SetExtraButtons(toolbarExtraTexts());
}

void CShareViewerWindow::SetDurationText(const std::wstring& text)
{
    if (text == m_duration)
        return;
    m_duration = text;
    //帧率定时器每秒刷新一次标题；全屏工具条的状态行在这里顺带刷新
    refreshStatus();
}

void CShareViewerWindow::SetForced(bool forced)
{
    if (forced == m_forced)
        return;
    m_forced = forced;
    LOG_INFO("screenshare viewer: forced mode %d, room=%s.", forced ? 1 : 0, m_roomId.c_str());
    applyForced();
}

bool CShareViewerWindow::IsForced() const
{
    return m_forced;
}

void CShareViewerWindow::applyForced()
{
    if (!IsWindow())
        return;
    const ShareViewerChrome chrome = MakeShareViewerChrome(m_forced);
    m_toolbar.SetFixedButtonsVisible(chrome.m_showExitFullscreen, chrome.m_showLeave);
    //强制模式只能全屏；已经全屏时重新显示工具条，按新的按钮组合重建
    if (!IsFullscreen())
    {
        toggleFullscreen();
    }
    else
    {
        m_toolbar.ShowOnMonitor(::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), m_hWnd,
                                WM_SHARE_VIEWER_TOOLBAR);
        refreshStatus();
    }
    ::SetWindowPos(m_hWnd, chrome.m_topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (chrome.m_pullBackToFront)
        SetTimer(kPullBackTimerId, kPullBackTimerMs, NULL);
    else
        KillTimer(kPullBackTimerId);
}

void CShareViewerWindow::pullBackToFront()
{
    if (!m_forced || !IsWindow())
        return;
    const HWND fg = ::GetForegroundWindow();
    if (fg == m_hWnd)
    {
        m_pullBackFailed = false;
        return;
    }
    //本进程自己的窗口（观看者名单、提示框、全屏工具条等）在前台时不拉回，否则这些窗口点不了
    DWORD pid = 0;
    const DWORD fgThread = (fg != NULL) ? ::GetWindowThreadProcessId(fg, &pid) : 0;
    if (fg != NULL && pid == ::GetCurrentProcessId())
        return;
    //Windows 的前台锁定不允许后台程序直接把自己切到前台（第 13 期联调实测 SetForegroundWindow 失败）。
    //先把本线程的输入挂到前台窗口的线程上，此时本线程与前台线程共用输入状态，切前台不再被拦截；切完立即解除挂接
    const DWORD selfThread = ::GetCurrentThreadId();
    const bool attached = (fgThread != 0 && fgThread != selfThread &&
                           ::AttachThreadInput(selfThread, fgThread, TRUE) != FALSE);
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    ::BringWindowToTop(m_hWnd);
    ::SetForegroundWindow(m_hWnd);
    if (attached)
        ::AttachThreadInput(selfThread, fgThread, FALSE);
    //以前台窗口是否真的换成了观看窗为准，只在成败变化时记日志
    const bool ok = (::GetForegroundWindow() == m_hWnd);
    if (!ok && !m_pullBackFailed)
        LOG_WARNING("screenshare viewer: forced mode could not bring the viewer to the foreground, attached=%d, room=%s.",
                    attached ? 1 : 0, m_roomId.c_str());
    else if (ok)
        LOG_INFO("screenshare viewer: forced mode brought the viewer back to the foreground, attached=%d, room=%s.",
                 attached ? 1 : 0, m_roomId.c_str());
    m_pullBackFailed = !ok;
}

bool CShareViewerWindow::GetViewersAnchor(RECT& out) const
{
    return popupAnchor(kViewersExtraIndex, out);
}

bool CShareViewerWindow::popupAnchor(size_t extraIndex, RECT& out) const
{
    if (!IsWindow())
        return false;
    if (IsFullscreen() && m_toolbar.GetExtraButtonScreenRect(extraIndex, out))
        return true;
    //窗口模式：标题栏右端往左的一块，名单弹在标题栏下方
    RECT wr;
    GetWindowRect(&wr);
    out.left = wr.right - kViewersAnchorW;
    out.right = wr.right;
    out.top = wr.top;
    out.bottom = wr.top + kTitleBarH;
    return true;
}

void CShareViewerWindow::requestRecordToggle()
{
    if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
        ::PostMessage(m_notifyWnd, kMsgShareViewerCommand, m_recording ? kCmdRecordStop : kCmdRecordStart, 0);
}

void CShareViewerWindow::SetRecording(bool on)
{
    m_recording = on;
    if (!on)
        m_recordDuration.clear();
    //录制期间当前那块可能是白板（2026-10-07）：定时把白板画成图给录制用；先画一张，录制一开始就有画面
    if (IsWindow())
    {
        if (on)
        {
            RefreshRecordBoardFrame();
            SetTimer(kRecordBoardTimerId, kRecordBoardTimerMs, NULL);
        }
        else
        {
            KillTimer(kRecordBoardTimerId);
        }
    }
    if (!on)
        m_recordBoardActive.store(false);
    m_toolbar.SetExtraButtons(toolbarExtraTexts());
    refreshMicIcon();
}

void CShareViewerWindow::RefreshRecordBoardFrame()
{
    std::vector<uint8_t> img;
    int w = 0;
    int h = 0;
    if (m_canvas == NULL || !m_canvas->RenderActiveBoard(img, w, h))
    {
        m_recordBoardActive.store(false);
        return;
    }
    {
        std::lock_guard<std::mutex> guard(m_recordBoardMutex);
        m_recordBoardFrame.swap(img);
        m_recordBoardW = w;
        m_recordBoardH = h;
        ++m_recordBoardSerial;
    }
    m_recordBoardActive.store(true);
}

void CShareViewerWindow::SetRecordDuration(const std::wstring& text)
{
    //没在录制时不显示（控制器在停止之后可能还会交来最后一次的时长）
    const std::wstring shown = m_recording ? text : std::wstring();
    if (shown.empty() && m_recordDuration.empty())
        return;
    m_recordDuration = shown;
    m_recordDotOn = !m_recordDotOn;
    if (!IsWindow())
        return;
    SetTitle(titleText().c_str());
    refreshStatus();
}

void CShareViewerWindow::requestShareMine()
{
    if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
        ::PostMessage(m_notifyWnd, kMsgShareViewerCommand, kCmdShareMine, 0);
}

void CShareViewerWindow::requestBoard()
{
    if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
        ::PostMessage(m_notifyWnd, kMsgShareViewerCommand, kCmdBoard, 0);
}

void CShareViewerWindow::SetBoardState(int state)
{
    if (state == m_boardState)
        return;
    m_boardState = state;
    m_toolbar.SetExtraButtons(toolbarExtraTexts());
    refreshMicIcon();
}

void CShareViewerWindow::SetShareMineState(int state)
{
    if (state == m_mineState)
        return;
    m_mineState = state;
    m_toolbar.SetExtraButtons(toolbarExtraTexts());
    refreshMicIcon();
}

void CShareViewerWindow::requestControlToggle()
{
    if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
        ::PostMessage(m_notifyWnd, kMsgShareViewerCommand, kCmdControl, 0);
}

void CShareViewerWindow::SetControlState(int state)
{
    if (state == m_controlState)
        return;
    //离开控制：先补发还按着的按钮与按键的抬起（控制器决定还能不能发出去），再切换状态
    if (m_controlState == share_viewer_control_active && state != share_viewer_control_active)
        releaseControlInput();
    m_controlState = state;
    m_forwarder.Reset();
    LOG_INFO("screenshare viewer: control state %d, room=%s.", state, m_roomId.c_str());
    m_toolbar.SetExtraButtons(toolbarExtraTexts());
    refreshMicIcon();
    //开始控制：键盘输入要落在观看窗上
    if (state == share_viewer_control_active && IsWindow())
        ::SetFocus(m_hWnd);
}

void CShareViewerWindow::SetControlInput(IShareControlInput* input)
{
    m_controlInput = input;
}

void CShareViewerWindow::releaseControlInput()
{
    m_forwarder.Release(m_controlInput);
}

ShareControlForwarder::ShareControlForwarder()
    : m_captured(false)
{
}

bool ShareControlForwarder::Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, const RECT& video,
                                   IShareControlInput* input)
{
    if (input == NULL)
        return false;
    const bool keyboard = (msg >= WM_KEYFIRST && msg <= WM_KEYLAST);
    POINT pt = { 0, 0 };
    if (!keyboard)
    {
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        //滚轮消息带的是屏幕坐标
        if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL)
            ::ScreenToClient(hwnd, &pt);
    }
    const bool inVideo = (pt.x >= video.left && pt.x < video.right && pt.y >= video.top && pt.y < video.bottom);
    const bool hadPressed = !m_translator.PendingButtonUps().empty();
    //画面以外、又没有按着的按钮的鼠标消息（标题栏、摄像头小窗条、按钮行）：照常交给窗口自己
    if (!keyboard && !inVideo && !hadPressed)
        return false;
    ShareInputEvent ev;
    if (m_translator.Translate(msg, wParam, pt, video, static_cast<uint32_t>(::GetTickCount()), ev))
    {
        //记下按着的键，失去焦点或结束控制时补发抬起
        if (ev.m_type == remotedesktop::rd_input_key)
        {
            const bool down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
            std::vector<uint32_t>::iterator it = std::find(m_keysDown.begin(), m_keysDown.end(), ev.m_vkey);
            if (down && it == m_keysDown.end())
                m_keysDown.push_back(ev.m_vkey);
            else if (!down && it != m_keysDown.end())
                m_keysDown.erase(it);
        }
        input->OnControlInput(ev);
    }
    //按下后抓住鼠标，拖到窗口外松开也收得到；全部按钮松开后放开
    const bool pressedNow = !m_translator.PendingButtonUps().empty();
    if (pressedNow && !m_captured)
    {
        ::SetCapture(hwnd);
        m_captured = true;
    }
    else if (!pressedNow && m_captured)
    {
        m_captured = false;
        ::ReleaseCapture();
    }
    //控制中的键盘消息与画面上的鼠标消息一律拦下：Alt 不弹系统菜单、Esc 不退出全屏、字符不响提示音
    return true;
}

void ShareControlForwarder::Release(IShareControlInput* input)
{
    const std::vector<uint32_t> ups = m_translator.PendingButtonUps();
    if (input != NULL)
    {
        //按钮松开的坐标取画面中心：画面主人那边只关心松开这个动作
        const int center = kAnnotCoordMax / 2;
        for (size_t i = 0; i < ups.size(); ++i)
            input->OnControlInput(ShareInputEvent::Mouse(ups[i], center, center, 0));
        for (size_t i = 0; i < m_keysDown.size(); ++i)
            input->OnControlInput(ShareInputEvent::Key(WM_KEYUP, m_keysDown[i]));
    }
    Reset();
}

void ShareControlForwarder::Reset()
{
    m_translator.Reset();
    m_keysDown.clear();
    if (m_captured)
    {
        m_captured = false;
        ::ReleaseCapture();
    }
}

LRESULT CShareViewerWindow::OnControlInputMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    //不在控制中、或正在标注（标注优先，鼠标交给画面控件画笔迹）：原样放过
    if (m_controlState != share_viewer_control_active || m_annotating || m_canvas == NULL || m_controlInput == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    RECT video = m_canvas->VideoRect();
    //多人共享（第 22 期）：按被控制的那一块换算；它没有显示出来时当作不在画面上
    if (m_controlScreen >= 0 && !m_canvas->ScreenVideoRect(m_controlScreen, video))
        memset(&video, 0, sizeof(video));
    //画面以外、又没有按着的按钮的鼠标消息（标题栏、摄像头小窗条）：照常交给窗口自己
    if (!m_forwarder.Handle(m_hWnd, uMsg, wParam, lParam, video, m_controlInput))
        bHandled = FALSE;
    return 0;
}

LRESULT CShareViewerWindow::OnKillFocusMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    if (m_controlState == share_viewer_control_active)
        releaseControlInput();
    bHandled = FALSE;
    return 0;
}

bool CShareViewerWindow::GetRecordFrame(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial)
{
    //当前那块是白板（2026-10-07，方案第 14.1 节 ④-2）：白板没有视频，取界面线程按可见区域画好的白板图
    if (m_recordBoardActive.load())
    {
        std::lock_guard<std::mutex> guard(m_recordBoardMutex);
        if (!m_recordBoardFrame.empty())
        {
            bgra = m_recordBoardFrame;
            width = m_recordBoardW;
            height = m_recordBoardH;
            serial = m_recordBoardSerial;
            return true;
        }
    }
    //多人共享（第 22 期）：录当前那一块（放大的那块，没有放大时为第一块）
    const int seat = ActiveScreenSeat();
    if (seat >= 0)
        return m_receiver.GetScreenFrame(seat, bgra, width, height, serial);
    return m_receiver.GetLatestFrame(bgra, width, height, serial);
}

void CShareViewerWindow::SetScreens(const std::vector<int>& seats, const std::vector<std::wstring>& names)
{
    m_screenSeats = seats;
    m_screenNames = names;
    if (m_canvas != NULL)
        m_canvas->SetScreens(seats, names);
    //清单从有到无或又有了画面：提示文字跟着变
    refreshStatus();
    //清单变了：已不在清单里的画面不再记帧序号；拉一次各块的最新一帧
    std::map<int, uint64_t> kept;
    for (size_t i = 0; i < seats.size(); ++i)
    {
        std::map<int, uint64_t>::iterator it = m_lastSerials.find(seats[i]);
        if (it != m_lastSerials.end())
            kept.insert(*it);
    }
    m_lastSerials.swap(kept);
    if (IsWindow())
        ::PostMessage(m_hWnd, WM_SHARE_VIEWER_FRAME, 0, 0);
}

int CShareViewerWindow::ActiveScreenSeat() const
{
    return (m_canvas != NULL) ? m_canvas->ActiveScreenSeat() : -1;
}

void CShareViewerWindow::SetControlScreen(int seat)
{
    m_controlScreen = seat;
}

void CShareViewerWindow::SetBoards(const std::vector<int>& seats, const std::vector<int>& ownerSeats)
{
    m_boardSeats = seats;
    m_boardOwnerSeats = ownerSeats;
    if (m_canvas != NULL)
        m_canvas->SetBoards(seats, ownerSeats);
}

bool CShareViewerWindow::ActiveIsBoard() const
{
    return m_canvas != NULL && m_canvas->ActiveIsBoard();
}

bool CShareViewerWindow::GetScreenFrame(int seat, std::vector<uint8_t>& bgra, int& width, int& height)
{
    uint64_t serial = 0;
    if (m_receiver.GetScreenFrame(seat, bgra, width, height, serial))
        return true;
    //没有按座位解码的那一路（老服务端、只有发起人在共享）：取单画面的最新一帧
    return m_receiver.GetLatestFrame(bgra, width, height, serial);
}

void CShareViewerWindow::requestAnnotateToggle()
{
    if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
        ::PostMessage(m_notifyWnd, kMsgShareViewerCommand, kCmdAnnotate, 0);
}

void CShareViewerWindow::SetAnnotation(const ShareAnnotationSession* session, IShareAnnotationInput* input,
                                       ShareAnnotationSink* sink)
{
    m_annotSession = session;
    m_annotInput = input;
    m_receiver.SetAnnotationSink(sink);
    if (m_canvas != NULL)
    {
        m_canvas->SetAnnotation(session);
        m_canvas->SetAnnotating(m_annotating && !m_annotPointer, input);
    }
}

void CShareViewerWindow::SetAnnotating(bool on)
{
    m_annotating = on;
    //进入或退出标注模式都从「画图」开始，不沿用上一次的「鼠标」状态
    m_annotPointer = false;
    if (m_canvas != NULL)
        m_canvas->SetAnnotating(on, m_annotInput);
    m_toolbar.SetExtraButtons(toolbarExtraTexts());
    refreshMicIcon();
}

void CShareViewerWindow::SetAnnotPointer(bool on)
{
    m_annotPointer = on;
    //只改画面控件接不接标注输入；标注模式本身与标题栏图标、工具条按钮不变
    if (m_canvas != NULL)
        m_canvas->SetAnnotating(m_annotating && !on, m_annotInput);
}

void CShareViewerWindow::AnnotationChanged()
{
    if (m_canvas != NULL)
        m_canvas->Invalidate();
}

void CShareViewerWindow::requestCameraToggle()
{
    if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
        ::PostMessage(m_notifyWnd, kMsgShareViewerCommand, m_cameraOn ? kCmdCameraOff : kCmdCameraOn, 0);
}

std::vector<std::wstring> CShareViewerWindow::toolbarExtraTexts() const
{
    std::vector<std::wstring> texts;
    texts.push_back(m_micOn ? TR(kMicOnButtonText) : TR(kMicOffButtonText));
    texts.push_back(m_cameraOn ? TR(kCamOnButtonText) : TR(kCamOffButtonText));
    texts.push_back(m_annotating ? TR(kAnnotExitButtonText) : TR(kAnnotButtonText));
    texts.push_back(m_recording ? TR(kRecordStopButtonText) : TR(kRecordButtonText));
    //「申请控制 / 取消申请 / 结束控制」（第 18 期）：第 kControlExtraIndex 个额外按钮
    const int ctrlState = (m_controlState >= 0 && m_controlState < kControlStateCount) ? m_controlState
                                                                                       : share_viewer_control_none;
    texts.push_back(TR(kControlButtonTexts[ctrlState]));
    //「共享我的屏幕 / 取消共享申请 / 停止我的共享」（第 22 期）：第 kShareMineExtraIndex 个额外按钮
    const int mineState = (m_mineState >= 0 && m_mineState < kShareMineStateCount) ? m_mineState : share_viewer_mine_none;
    texts.push_back(TR(kShareMineButtonTexts[mineState]));
    //「新建白板 / 取消白板申请 / 关闭我的白板」（第 23 期）：第 kBoardExtraIndex 个额外按钮
    const int boardState = (m_boardState >= 0 && m_boardState < kShareMineStateCount) ? m_boardState
                                                                                      : share_viewer_mine_none;
    texts.push_back(TR(kBoardButtonTexts[boardState]));
    //「N 人观看 ▾」（第 13 期）：第 kViewersExtraIndex 个额外按钮
    texts.push_back(ShareViewerCountText(m_viewerCount));
    //「共享说明」（第 15 期）：第 kInfoExtraIndex 个额外按钮，只在共享有描述时出现
    if (!m_shareDesc.empty())
        texts.push_back(TR(kInfoButtonText));
    //「主持人 ▾」（2026-10-07）：第 hostExtraIndex() 个额外按钮，只在经移交成为主持人时出现
    if (m_hostTools)
        texts.push_back(TR(kHostButtonText));
    return texts;
}

size_t CShareViewerWindow::hostExtraIndex() const
{
    return m_shareDesc.empty() ? kInfoExtraIndex : kInfoExtraIndex + 1;
}

void CShareViewerWindow::SetHostBoards(const std::vector<ShareBoardCloseItem>& items)
{
    m_hostBoards = items;
}

void CShareViewerWindow::SetHostTools(bool on, bool recordAllowed, bool annotateAllowed)
{
    m_hostRecordAllowed = recordAllowed;
    m_hostAnnotAllowed = annotateAllowed;
    if (on == m_hostTools)
        return;
    m_hostTools = on;
    LOG_INFO("screenshare viewer: host tools %s, room=%s.", on ? "shown" : "hidden", m_roomId.c_str());
    m_toolbar.SetExtraButtons(toolbarExtraTexts());
    refreshMicIcon();
}

bool CShareViewerWindow::HasHostTools() const
{
    return m_hostTools;
}

void CShareViewerWindow::showHostMenu()
{
    //已经有菜单开着、窗口正等待销毁（共享已结束）时不再弹
    if (!IsWindow() || !m_hostTools || m_notifyWnd == NULL || !::IsWindow(m_notifyWnd)
        || m_captionPopup.IsActive() || m_captionPopup.IsPendingDestroy())
        return;
    std::vector<ShareHostMenuItem> items;
    BuildShareHostMenu(m_hostRecordAllowed, m_hostAnnotAllowed, IsBarCaptureAllowed(), items);
    balloonwjui::DuiMenu menu;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_separatorBefore)
            menu.AppendSeparator();
        if (items[i].m_checkable)
            menu.AppendChecked(static_cast<UINT>(items[i].m_id), items[i].m_text.c_str(), items[i].m_checked);
        else
            menu.AppendItem(static_cast<UINT>(items[i].m_id), items[i].m_text.c_str());
    }
    //关闭别人的白板（2026-10-08）：子菜单列出别人的白板；按打开菜单这一刻的清单处理选择
    const std::vector<ShareBoardCloseItem> closeBoards = m_hostBoards;
    std::vector<std::unique_ptr<balloonwjui::DuiMenu> > subMenus;
    if (!closeBoards.empty())
    {
        std::unique_ptr<balloonwjui::DuiMenu> boardMenu(new balloonwjui::DuiMenu());
        for (size_t i = 0; i < closeBoards.size(); ++i)
            boardMenu->AppendItem(static_cast<UINT>(kHostMenuCloseBoardBase + i), closeBoards[i].m_text.c_str());
        menu.AppendSeparator();
        menu.AppendSubMenu(kHostMenuCloseBoardRoot, TR(L"关闭白板"), boardMenu.get());
        subMenus.push_back(std::move(boardMenu));
    }
    //全屏时弹在「主持人 ▾」按钮下方，窗口模式时弹在光标处（光标此时就在刚点过的皇冠图标上）；越出工作区时由 DuiMenu 调整
    POINT pt = { 0, 0 };
    ::GetCursorPos(&pt);
    RECT button;
    if (IsFullscreen() && m_toolbar.GetExtraButtonScreenRect(hostExtraIndex(), button))
    {
        pt.x = button.left;
        pt.y = button.bottom;
    }
    m_captionPopup.BeginMenu(&menu, subMenus);
    const UINT chosen = menu.TrackPopup(pt.x, pt.y, m_hWnd);
    m_captionPopup.End();
    //菜单期间控制器关闭了观看窗：现在才销毁窗口（本条消息是投递来的，返回后 ATL 即清空 m_hWnd）
    if (m_captionPopup.TakePendingDestroy())
    {
        LOG_INFO("screenshare viewer: window destroyed after its host menu closed.");
        DestroyWindow();
        return;
    }
    //菜单期间观看窗被关闭过或通知窗口没了：丢弃这次选择
    if (!ShareBarShouldDeliverChoice(chosen, m_captionPopup.WasInterrupted(),
                                     m_notifyWnd != NULL && ::IsWindow(m_notifyWnd)))
        return;
    //「关闭白板 ▸」里的一块：把建立者交给控制器，由它先确认再关闭
    if (chosen >= kHostMenuCloseBoardBase && chosen < kHostMenuCloseBoardBase + closeBoards.size())
    {
        ::PostMessage(m_notifyWnd, kMsgShareViewerCommand, kCmdCloseBoard,
                      static_cast<LPARAM>(closeBoards[chosen - kHostMenuCloseBoardBase].m_ownerId));
        return;
    }
    ::PostMessage(m_notifyWnd, kMsgShareViewerCommand, kCmdHostMenu, static_cast<LPARAM>(chosen));
}

LRESULT CShareViewerWindow::OnHostMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    showHostMenu();
    return 0;
}

void CShareViewerWindow::SetCameraOn(bool on)
{
    m_cameraOn = on;
    m_toolbar.SetExtraButtons(toolbarExtraTexts());
    refreshMicIcon();
}

void CShareViewerWindow::SetCameraTiles(const std::vector<ShareCameraTile>& tiles, int overflowCount)
{
    if (m_cameraPanel == NULL)
        return;
    m_cameraPanel->SetTiles(tiles, overflowCount);
    const bool show = m_cameraPanel->HasContent();
    if (show == m_cameraPanel->IsVisible())
        return;
    m_cameraPanel->SetVisible(show);
    //显隐变化后对父容器重排（开发约定文档「UI 约定」第 5 条）
    balloonwjui::DuiControl* parent = m_cameraPanel->GetParent();
    if (parent != NULL)
        parent->ForceLayout(parent->GetRect());
    Invalidate();
}

void CShareViewerWindow::SetCameraSource(IShareCameraFrames* source)
{
    m_cameraSource = source;
    if (m_cameraPanel != NULL)
        m_cameraPanel->SetSource(source);
}

void CShareViewerWindow::SetCameraEndpoints(ShareCameraSender* sender, ShareCameraReceiver* receiver)
{
    m_receiver.SetCameraEndpoints(sender, receiver);
}

void CShareViewerWindow::requestMicToggle()
{
    if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
        ::PostMessage(m_notifyWnd, kMsgShareViewerCommand, m_micOn ? kCmdMicOff : kCmdMicOn, 0);
}

void CShareViewerWindow::SetMicOn(bool on)
{
    m_micOn = on;
    m_toolbar.SetExtraButtons(toolbarExtraTexts());
    refreshMicIcon();
}

void CShareViewerWindow::SetSpeakingText(const std::wstring& text)
{
    if (text == m_speaking)
        return;
    m_speaking = text;
    if (IsWindow())
        SetTitle(titleText().c_str());
    refreshStatus();
}

ShareAudioTransport* CShareViewerWindow::AudioTransport()
{
    return &m_receiver;
}

void CShareViewerWindow::SetAudioSession(ShareAudioSession* audio)
{
    m_receiver.SetAudioSession(audio);
}

LRESULT CShareViewerWindow::OnCaptionMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    showCaptionMenu();
    return 0;
}

LRESULT CShareViewerWindow::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    //标题栏图标的通知不带有意义的 ctrlId，只能靠 extra 里的图标 id 区分；图标还没放上去（id 为 0）时不认
    //（开发约定文档「UI 约定」第 4 条）
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DuiFrameWindow::DUIFW_CAPTION_ICON_CLICK)
        && m_micIconId != 0 && static_cast<int>(n->extra) == m_micIconId)
    {
        requestMicToggle();
        return 0;
    }
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DuiFrameWindow::DUIFW_CAPTION_ICON_CLICK)
        && m_camIconId != 0 && static_cast<int>(n->extra) == m_camIconId)
    {
        requestCameraToggle();
        return 0;
    }
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DuiFrameWindow::DUIFW_CAPTION_ICON_CLICK)
        && m_penIconId != 0 && static_cast<int>(n->extra) == m_penIconId)
    {
        requestAnnotateToggle();
        return 0;
    }
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DuiFrameWindow::DUIFW_CAPTION_ICON_CLICK)
        && m_recIconId != 0 && static_cast<int>(n->extra) == m_recIconId)
    {
        requestRecordToggle();
        return 0;
    }
    //标题栏「控制」图标（第 18 期）
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DuiFrameWindow::DUIFW_CAPTION_ICON_CLICK)
        && m_ctrlIconId != 0 && static_cast<int>(n->extra) == m_ctrlIconId)
    {
        requestControlToggle();
        return 0;
    }
    //标题栏「共享我的屏幕」图标（第 22 期）
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DuiFrameWindow::DUIFW_CAPTION_ICON_CLICK)
        && m_mineIconId != 0 && static_cast<int>(n->extra) == m_mineIconId)
    {
        requestShareMine();
        return 0;
    }
    //标题栏「新建白板」图标（第 23 期）
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DuiFrameWindow::DUIFW_CAPTION_ICON_CLICK)
        && m_boardIconId != 0 && static_cast<int>(n->extra) == m_boardIconId)
    {
        requestBoard();
        return 0;
    }
    //标题栏「观看者」图标（第 13 期）
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DuiFrameWindow::DUIFW_CAPTION_ICON_CLICK)
        && m_peopleIconId != 0 && static_cast<int>(n->extra) == m_peopleIconId)
    {
        requestViewersToggle();
        return 0;
    }
    //标题栏「共享说明」图标（第 15 期）
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DuiFrameWindow::DUIFW_CAPTION_ICON_CLICK)
        && m_infoIconId != 0 && static_cast<int>(n->extra) == m_infoIconId)
    {
        toggleInfoPopup();
        return 0;
    }
    //标题栏「字幕」图标（语音字幕）
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DuiFrameWindow::DUIFW_CAPTION_ICON_CLICK)
        && m_captionIconId != 0 && static_cast<int>(n->extra) == m_captionIconId)
    {
        PostMessage(WM_SHARE_VIEWER_CAPTION_MENU, 0, 0);
        return 0;
    }
    //标题栏皇冠图标（2026-10-07，经移交成为主持人时才有）：等点击通知处理完再弹主持人菜单
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DuiFrameWindow::DUIFW_CAPTION_ICON_CLICK)
        && m_hostIconId != 0 && static_cast<int>(n->extra) == m_hostIconId)
    {
        PostMessage(WM_SHARE_VIEWER_HOST_MENU, 0, 0);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

bool CShareViewerWindow::Open(const ShareRelayInfo& relay, const unsigned char* serverPubKey,
                              const std::wstring& hostName, HWND notifyWnd, HWND monitorRef,
                              const std::shared_ptr<balloonp2p::P2PLinkSlot>& p2pSlot)
{
    m_notifyWnd = notifyWnd;
    m_roomId = relay.m_roomId;
    m_hostName = hostName;
    m_lastSerial = 0;
    m_framesSinceTick = 0;
    m_lastFps = 0;
    m_hostOnline = true;
    m_sourceState = screenshare_source_sharing;
    m_relayBroken = false;
    //第 13 期：观看人数、时长、强制模式从头开始；强制模式等 1725 告知后再生效
    m_viewerCount = 0;
    m_duration.clear();
    m_forced = false;
    m_pullBackFailed = false;
    m_toolbar.SetFixedButtonsVisible(true, true);
    EnsureWindow();
    if (!IsWindow())
        return false;

    //有共享标题时用标题（第 15 期），否则「某某 的屏幕共享」；时长与帧率等定时器第一次触发时再接上
    SetTitle(MakeShareViewerCaption(m_shareTitle, hostName).c_str());

    //先摆到参照窗口所在的显示器上，再切全屏，全屏就铺满那块显示器
    HMONITOR mon = ::MonitorFromWindow(monitorRef != NULL ? monitorRef : m_hWnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfo(mon, &mi))
    {
        const int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - kWindowW) / 2;
        const int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - kWindowH) / 2;
        SetWindowPos(NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    refreshStatus();
    //默认全屏（docs/屏幕共享与录制方案.md 5.2 第 7 步）
    if (!IsFullscreen())
        toggleFullscreen();
    UpdateWindow();

    if (!m_receiver.Start(relay, serverPubKey, this, p2pSlot))
    {
        m_relayBroken = true;
        refreshStatus();
        return false;
    }
    SetTimer(kFpsTimerId, kFpsTimerMs, NULL);
    SetTimer(kCameraTimerId, kCameraTimerMs, NULL);
    return true;
}

bool CShareViewerWindow::Reconnect(const ShareRelayInfo& relay, const unsigned char* serverPubKey)
{
    if (!IsWindow() || relay.m_roomId != m_roomId)
        return false;
    m_relayBroken = false;
    //Start 内部先停掉旧的工作线程与连接，再按新令牌连接并报到
    const bool ok = m_receiver.Start(relay, serverPubKey, this);
    if (!ok)
        m_relayBroken = true;
    LOG_INFO("screenshare viewer: reconnected to relay, ok=%d, room=%s.", ok ? 1 : 0, m_roomId.c_str());
    refreshStatus();
    return ok;
}

void CShareViewerWindow::CloseViewer()
{
    m_receiver.Stop();
    m_toolbar.Hide();
    m_infoPopup.Hide();
    m_canvas = NULL;
    //标题栏图标存放在窗口对象里、不随窗口销毁清掉，先移除，下次建窗时重新放
    if (m_peopleIconId != 0)
        RemoveCaptionIcon(m_peopleIconId);
    m_peopleIconId = 0;
    m_forced = false;
    if (IsWindow())
        KillTimer(kPullBackTimerId);
    if (m_micIconId != 0)
        RemoveCaptionIcon(m_micIconId);
    m_micIconId = 0;
    if (m_camIconId != 0)
        RemoveCaptionIcon(m_camIconId);
    m_camIconId = 0;
    if (m_penIconId != 0)
        RemoveCaptionIcon(m_penIconId);
    m_penIconId = 0;
    if (m_recIconId != 0)
        RemoveCaptionIcon(m_recIconId);
    m_recIconId = 0;
    if (m_infoIconId != 0)
        RemoveCaptionIcon(m_infoIconId);
    m_infoIconId = 0;
    if (m_ctrlIconId != 0)
        RemoveCaptionIcon(m_ctrlIconId);
    m_ctrlIconId = 0;
    if (m_mineIconId != 0)
        RemoveCaptionIcon(m_mineIconId);
    m_mineIconId = 0;
    if (m_boardIconId != 0)
        RemoveCaptionIcon(m_boardIconId);
    m_boardIconId = 0;
    if (m_hostIconId != 0)
        RemoveCaptionIcon(m_hostIconId);
    m_hostIconId = 0;
    //主持人入口属于这一次观看（2026-10-07），下次由控制器按房间状态重新打开
    m_hostTools = false;
    //「共享我的屏幕」「新建白板」的状态、画面清单与被控制的画面都属于这一次观看（第 22、23 期）
    m_mineState = share_viewer_mine_none;
    m_boardState = share_viewer_mine_none;
    m_screenSeats.clear();
    m_screenNames.clear();
    m_boardSeats.clear();
    m_boardOwnerSeats.clear();
    m_lastSerials.clear();
    m_controlScreen = -1;
    //控制状态属于这一次观看（第 18 期）：不再补发抬起，房间已经离开
    m_controlState = share_viewer_control_none;
    m_forwarder.Reset();
    //共享标题与描述属于这一次共享，下次打开前由控制器重新设置
    m_shareTitle.clear();
    m_shareDesc.clear();
    m_annotating = false;
    m_annotPointer = false;
    m_recording = false;
    m_cameraPanel = NULL;
    //先退出全屏再销毁：DuiFrameWindow 的全屏标志不随窗口销毁复位，不退出的话下次打开时「切换全屏」会变成退出全屏，
    //观看窗就以窗口模式打开了（2h 联调发现）
    if (IsWindow() && IsFullscreen())
        ToggleFullscreen();
    //字幕菜单正在弹出：菜单的嵌套消息循环还在本窗口的消息处理里，此时销毁窗口，ATL 要等那条消息返回才清 m_hWnd。
    //改为先关菜单、只隐藏窗口，等 TrackPopup 返回后由 showCaptionMenu 销毁
    if (IsWindow() && ShareBarShouldDeferDestroy(m_captionPopup.IsActive()))
    {
        m_captionPopup.Close();
        m_captionPopup.SetPendingDestroy();
        ShowWindow(SW_HIDE);
        LOG_INFO("screenshare viewer: closed while its caption menu is open, destroy deferred.");
    }
    else if (IsWindow())
    {
        DestroyWindow();
    }
}

void CShareViewerWindow::SetHostOnline(bool online)
{
    m_hostOnline = online;
    refreshStatus();
}

void CShareViewerWindow::SetCloudRecordState(bool recording, bool paused)
{
    const bool pausedNow = recording && paused;
    if (recording == m_cloudRecording && pausedNow == m_cloudRecordPaused)
        return;
    m_cloudRecording = recording;
    m_cloudRecordPaused = pausedNow;
    LOG_INFO("screenshare viewer: cloud record %d, paused %d, room=%s.", recording ? 1 : 0, pausedNow ? 1 : 0,
             m_roomId.c_str());
    if (!IsWindow())
        return;
    SetTitle(titleText().c_str());
    refreshStatus();
}

const std::string& CShareViewerWindow::RoomId() const
{
    return m_roomId;
}

void CShareViewerWindow::OnViewerFrame()
{
    if (IsWindow())
        ::PostMessage(m_hWnd, WM_SHARE_VIEWER_FRAME, 0, 0);
}

void CShareViewerWindow::OnViewerEvent(int event, int value)
{
    if (IsWindow())
        ::PostMessage(m_hWnd, WM_SHARE_VIEWER_EVENT, static_cast<WPARAM>(event), static_cast<LPARAM>(value));
}

LRESULT CShareViewerWindow::OnFrameMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    int w = 0;
    int h = 0;
    uint64_t serial = 0;
    if (m_canvas == NULL)
        return 0;
    //多人共享（第 22 期）：按画面清单逐块取最新一帧；多个帧通知可能排在队列里，各块只在帧序号变了时换
    const std::vector<int> seats = m_canvas->ScreenSeats();
    if (!seats.empty())
    {
        const bool first = !m_canvas->HasFrame();
        bool any = false;
        for (size_t i = 0; i < seats.size(); ++i)
        {
            if (!m_receiver.GetScreenFrame(seats[i], m_scratch, w, h, serial))
                continue;
            uint64_t& last = m_lastSerials[seats[i]];
            if (serial == last)
                continue;
            last = serial;
            m_canvas->SwapScreenFrame(seats[i], m_scratch, w, h);
            any = true;
        }
        if (any)
            ++m_framesSinceTick;
        if (first && m_canvas->HasFrame())
            refreshStatus();
        return 0;
    }
    //还不知道画面清单（老服务端或房间状态还没到）：只有一块画面，取座位最小的一路
    if (!m_receiver.GetLatestFrame(m_scratch, w, h, serial) || serial == m_lastSerial)
        return 0;
    const bool first = !m_canvas->HasFrame();
    m_lastSerial = serial;
    m_canvas->SwapFrame(m_scratch, w, h);
    ++m_framesSinceTick;
    if (first)
        refreshStatus();
    return 0;
}

LRESULT CShareViewerWindow::OnEventMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    const int event = static_cast<int>(wParam);
    if (event == share_viewer_source_state)
        m_sourceState = static_cast<int>(lParam);
    else if (event == share_viewer_relay_failed || event == share_viewer_relay_lost)
        m_relayBroken = true;
    refreshStatus();
    //中转断开要让控制器知道（它决定是否离开房间）
    if ((event == share_viewer_relay_failed || event == share_viewer_relay_lost) && m_notifyWnd != NULL &&
        ::IsWindow(m_notifyWnd))
        ::PostMessage(m_notifyWnd, kMsgShareViewerEvent, wParam, lParam);
    return 0;
}

void CShareViewerWindow::refreshStatus()
{
    if (m_canvas == NULL)
        return;
    std::wstring status;
    if (m_relayBroken)
        status = TR(L"与屏幕共享服务的连接已断开");
    else if (!m_hostOnline)
        status = TR(L"对方网络中断，正在等待重连");
    else if (m_sourceState == screenshare_source_paused)
        status = TR(L"对方已暂停共享");
    else if (m_sourceState == screenshare_source_minimized)
        status = TR(L"对方共享的窗口已最小化");
    else if (m_sourceState == screenshare_source_closed)
        status = TR(L"对方共享的窗口已关闭，正在重新选择共享内容");
    //画面清单从有到无：典型是经移交成为主持人的人、原主持人离开后又没有别人共享；画面停在最后一帧，要说明原因
    else if (m_canvas->ScreensGone())
        status = TR(L"目前没有其他人在共享屏幕");
    else if (!m_canvas->HasFrame())
        status = TR(L"正在加载画面……");
    m_canvas->SetStatusText(status);
    //全屏工具条的状态行：有异常时写原因；有说话人或临时提示时只写提示（状态行只有约 220 像素宽，
    //与发起人、帧率写在一起会被截断）；其余时候写发起人与帧率（工具条标题已经是「屏幕共享」）
    if (IsFullscreen())
    {
        if (!status.empty())
            m_toolbar.SetStatusText(status);
        else if (!m_speaking.empty())
            m_toolbar.SetStatusText(m_speaking);
        else
            m_toolbar.SetStatusText(titleText());
    }
}

void CShareViewerWindow::SetShareInfo(const std::wstring& title, const std::wstring& desc)
{
    if (title == m_shareTitle && desc == m_shareDesc)
        return;
    const bool hadDesc = !m_shareDesc.empty();
    m_shareTitle = title;
    m_shareDesc = desc;
    if (!IsWindow())
        return;
    SetTitle(titleText().c_str());
    refreshStatus();
    //描述由无变有或由有变无：「共享说明」图标与工具条按钮随之出现或消失。工具条已显示时按钮个数变了不会自动重建
    //（见 CRdViewerToolbar::SetExtraButtons），全屏时重新显示一次工具条
    if (hadDesc != !m_shareDesc.empty())
    {
        m_toolbar.SetExtraButtons(toolbarExtraTexts());
        if (IsFullscreen())
        {
            m_toolbar.ShowOnMonitor(::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), m_hWnd,
                                    WM_SHARE_VIEWER_TOOLBAR);
            refreshStatus();
        }
        refreshMicIcon();
    }
    if (m_shareDesc.empty())
        m_infoPopup.Hide();
    else
        m_infoPopup.SetContent(MakeShareViewerCaption(m_shareTitle, m_hostName), m_shareDesc);
}

void CShareViewerWindow::toggleInfoPopup()
{
    if (m_infoPopup.IsShown())
    {
        m_infoPopup.Hide();
        return;
    }
    if (m_shareDesc.empty())
        return;
    RECT anchor;
    if (!popupAnchor(kInfoExtraIndex, anchor))
        return;
    m_infoPopup.Show(anchor, MakeShareViewerCaption(m_shareTitle, m_hostName), m_shareDesc);
}

std::wstring CShareViewerWindow::titleText() const
{
    CString title(MakeShareViewerCaption(m_shareTitle, m_hostName).c_str());
    //共享时长（第 13 期）放在帧率前面：全屏工具条的状态行只有约 220 像素宽，放不下时截掉的是帧率
    if (!m_duration.empty())
    {
        title += _T(" · ");
        title += m_duration.c_str();
    }
    //本人录下这场共享（2026-10-07）：录制时长写在共享时长之后，圆点交替表示正在录制，不再另弹录制条
    if (m_recording && !m_recordDuration.empty())
    {
        title += _T(" · ");
        title += MakeShareRecordingText(m_recordDuration, true, m_recordDotOn).c_str();
    }
    //云端录制（第 27b 期）：写在时长之后，让观看者知道这场共享正在被录下
    if (m_cloudRecording)
    {
        title += _T(" · ");
        title += m_cloudRecordPaused ? TR(L"云端录制已暂停") : TR(L"云端录制中");
    }
    CString fps;
    fps.Format(_T("   %d fps"), m_lastFps);
    title += fps;
    if (!m_speaking.empty())
    {
        title += _T(" · ");
        title += m_speaking.c_str();
    }
    return std::wstring(title.GetString());
}

void CShareViewerWindow::toggleFullscreen()
{
    ToggleFullscreen();
    if (IsFullscreen())
    {
        //「离开」在别处是在线状态（Away），这里是离开共享的按钮，用上下文区分两种译法
        m_toolbar.SetLabels(TR(L"屏幕共享"), TRC(L"离开屏幕共享的按钮", L"离开"));
        m_toolbar.SetExtraButtons(toolbarExtraTexts());
        m_toolbar.ShowOnMonitor(::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), m_hWnd,
                                WM_SHARE_VIEWER_TOOLBAR);
        refreshStatus();
    }
    else
    {
        m_toolbar.Hide();
    }
}

void CShareViewerWindow::requestLeave()
{
    if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
        ::PostMessage(m_notifyWnd, kMsgShareViewerCommand, kCmdLeave, 0);
    else
        CloseViewer();
}

LRESULT CShareViewerWindow::OnToolbarCmd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    (void)bHandled;
    //工具条「退出全屏」
    if (wParam == CRdViewerToolbar::kCmdExitFullscreen)
    {
        if (IsFullscreen())
            toggleFullscreen();
    }
    //工具条「离开」
    else if (wParam == CRdViewerToolbar::kCmdDisconnect)
    {
        requestLeave();
    }
    //工具条「打开 / 关闭麦克风」（第一个额外按钮）
    else if (wParam == CRdViewerToolbar::kCmdExtra)
    {
        requestMicToggle();
    }
    //工具条「打开 / 关闭摄像头」（第二个额外按钮）
    else if (wParam == CRdViewerToolbar::kCmdExtra + 1)
    {
        requestCameraToggle();
    }
    //工具条「标注 / 退出标注」（第三个额外按钮）
    else if (wParam == CRdViewerToolbar::kCmdExtra + 2)
    {
        requestAnnotateToggle();
    }
    //工具条「录制 / 停止录制」（第四个额外按钮）
    else if (wParam == CRdViewerToolbar::kCmdExtra + 3)
    {
        requestRecordToggle();
    }
    //工具条「申请控制 / 取消申请 / 结束控制」（第五个额外按钮，第 18 期）
    else if (wParam == CRdViewerToolbar::kCmdExtra + kControlExtraIndex)
    {
        requestControlToggle();
    }
    //工具条「共享我的屏幕」（第六个额外按钮，第 22 期）
    else if (wParam == CRdViewerToolbar::kCmdExtra + kShareMineExtraIndex)
    {
        requestShareMine();
    }
    //工具条「新建白板」（第七个额外按钮，第 23 期）
    else if (wParam == CRdViewerToolbar::kCmdExtra + kBoardExtraIndex)
    {
        requestBoard();
    }
    //工具条「N 人观看 ▾」（第八个额外按钮，第 13 期）
    else if (wParam == CRdViewerToolbar::kCmdExtra + kViewersExtraIndex)
    {
        requestViewersToggle();
    }
    //工具条「主持人 ▾」（最后一个额外按钮，2026-10-07，只在经移交成为主持人时出现）：没有共享说明时它与「共享说明」
    //的下标相同，所以排在前面判断
    else if (m_hostTools && wParam == CRdViewerToolbar::kCmdExtra + hostExtraIndex())
    {
        PostMessage(WM_SHARE_VIEWER_HOST_MENU, 0, 0);
    }
    //工具条「共享说明」（第九个额外按钮，第 15 期，只在共享有描述时出现）
    else if (!m_shareDesc.empty() && wParam == CRdViewerToolbar::kCmdExtra + kInfoExtraIndex)
    {
        toggleInfoPopup();
    }
    return 0;
}

LRESULT CShareViewerWindow::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    //强制模式：关窗（含 Alt+F4）不起作用，要等发起人结束（第 13 期）
    if (m_forced)
    {
        LOG_INFO("screenshare viewer: close ignored in forced mode, room=%s.", m_roomId.c_str());
        return 0;
    }
    requestLeave();
    return 0;
}

LRESULT CShareViewerWindow::OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    //标注模式：Delete 删除选中的对象、Esc 取消选择（第 20 期）；没有选中对象时照常处理
    if (m_annotating && m_annotInput != NULL && m_annotInput->OnAnnotKey(static_cast<UINT>(wParam)))
        return 0;
    //强制模式：只能全屏，F11 与 Esc 都不起作用（第 13 期）
    if (m_forced && (wParam == VK_F11 || wParam == VK_ESCAPE))
        return 0;
    //F11：切换全屏
    if (wParam == VK_F11)
    {
        toggleFullscreen();
        return 0;
    }
    //ESC：只在全屏时退出全屏
    if (wParam == VK_ESCAPE && IsFullscreen())
    {
        toggleFullscreen();
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CShareViewerWindow::OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    //强制模式：切到别的程序后拉回前台（第 13 期）
    if (wParam == kPullBackTimerId)
    {
        pullBackToFront();
        return 0;
    }
    //摄像头小窗：取各路最新画面
    if (wParam == kCameraTimerId)
    {
        if (m_cameraPanel != NULL && m_cameraPanel->IsVisible())
            m_cameraPanel->Refresh();
        return 0;
    }
    //录制期间刷新给录制用的白板图（2026-10-07）
    if (wParam == kRecordBoardTimerId)
    {
        if (m_recording)
            RefreshRecordBoardFrame();
        else
            KillTimer(kRecordBoardTimerId);
        return 0;
    }
    if (wParam != kFpsTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    m_lastFps = m_framesSinceTick;
    m_framesSinceTick = 0;
    SetTitle(titleText().c_str());
    refreshStatus();
    //链路实际路径：直连时画面上显示「P2P」徽章
    if (m_canvas != NULL)
        m_canvas->SetLinkPath(m_receiver.LinkPath());
    return 0;
}

} // namespace screenshare
