/**
 *  SubtitleLayout.h
 *
 *  字幕窗口的几何：按字号、行数与显示器 DPI 算出各部分尺寸，工具按钮的位置，命中测试，以及进入字幕
 *  模式时窗口放在哪里。全部是纯函数，屏幕信息（各显示器工作区、其它字幕已占的位置）由调用方传入，
 *  可以完全用单元测试覆盖（见 Tests/SubtitleTests.cpp 的 SUBL、SUBP 组）。
 *
 *  窗口自上而下分三段：工具条（解锁且悬停时显示会话名与四个按钮，其它时候透明）、文字区（最多
 *  maxLines 行，每行居中）、底部留白。窗口高度只取决于字号与行数，不随消息长短变化，所以消息换来
 *  换去时字幕的位置不会跳动。工具条里「会话名 + 设置、锁定、回到聊天窗、退出」整组水平居中，
 *  锁定后浮出的小锁按钮与工具条里的锁定按钮在同一位置，用户总能在同一处找到它。
 *
 *  放置规则（ResolveSubtitleRect）：
 *    · 这个会话保存过位置，且保存的矩形与某个工作区重叠得足够多（显示器没有拔掉）：用保存的位置与宽度，
 *      平移到整个窗口都落在重叠最多的那个工作区里；
 *    · 否则放在主显示器工作区底部居中、离底边留出工作区高度的 kDefaultBottomMarginPercent；与其它字幕
 *      重叠时逐个往上挪，直到不重叠或到了工作区顶部。
 *
 *  典型用法：
 *      subtitle::SubtitleMetrics m = subtitle::ComputeSubtitleMetrics(appearance.m_fontSize, dpi);
 *      int h = subtitle::SubtitleWindowHeight(m, appearance.m_maxLines);
 *      subtitle::SubtitleToolbar tb = subtitle::ComputeSubtitleToolbar(m, width, titleWidth);
 *      int part = subtitle::HitTestSubtitle(m, tb, width, h, pt, locked, buttonsActive, lockVisible);
 *
 *  balloonwj@qq.com   2026-10-05
 */

#pragma once

#include <windows.h>
#include <vector>

#include "../../Session/SubtitlePrefs.h"

namespace subtitle {

//---- 以下尺寸都是 96 DPI 下的像素，使用时按显示器 DPI 缩放 ----
//工具条高度
const int kToolbarHeightDip = 34;
//工具按钮边长
const int kButtonSizeDip = 26;
//相邻两个工具按钮的间距
const int kButtonGapDip = 4;
//会话名与第一个按钮之间的间距
const int kTitleGapDip = 10;
//会话名的字号
const int kTitleFontDip = 13;
//文字区左右内边距（也是左右边缘调宽命中带之外的留白）
const int kPaddingXDip = 18;
//工具条与第一行文字之间的间距
const int kTextTopGapDip = 2;
//最后一行文字与窗口底边之间的留白
const int kPaddingBottomDip = 12;
//左右边缘调宽的命中带宽度
const int kEdgeGripDip = 8;
//字幕窗口的最小宽度
const int kMinWidthDip = 320;
//默认宽度的上限
const int kDefaultMaxWidthDip = 1100;
//底色的圆角半径
const int kCornerRadiusDip = 10;
//字幕之间上下错开时留的间距
const int kStackGapDip = 8;

//行高是字号的百分之多少：中文字形上下要留出描边与阴影的位置
const int kLineHeightPercent = 140;
//会话名最多占窗口宽度的百分比，超出时截短
const int kTitleMaxWidthPercent = 40;
//默认宽度占工作区宽度的百分比
const int kDefaultWidthPercent = 50;
//默认位置离工作区底边的距离占工作区高度的百分比
const int kDefaultBottomMarginPercent = 10;
//工具按钮的个数（设置、锁定、回到聊天窗、退出）
const int kToolbarButtonCount = 4;

//按字号与 DPI 算出的各部分尺寸，单位都是物理像素
struct SubtitleMetrics
{
    int m_dpi;            //所在显示器的 DPI
    int m_fontPx;         //字幕文字字号
    int m_lineHeight;     //行高
    int m_toolbarHeight;  //工具条高度
    int m_buttonSize;     //工具按钮边长
    int m_buttonGap;      //工具按钮间距
    int m_titleGap;       //会话名与第一个按钮的间距
    int m_titleFontPx;    //会话名字号
    int m_paddingX;       //文字区左右内边距
    int m_textTopGap;     //工具条与第一行的间距
    int m_paddingBottom;  //底部留白
    int m_edgeGrip;       //左右边缘调宽命中带宽度
    int m_minWidth;       //窗口最小宽度
    int m_cornerRadius;   //底色圆角半径

    SubtitleMetrics()
        : m_dpi(0)
        , m_fontPx(0)
        , m_lineHeight(0)
        , m_toolbarHeight(0)
        , m_buttonSize(0)
        , m_buttonGap(0)
        , m_titleGap(0)
        , m_titleFontPx(0)
        , m_paddingX(0)
        , m_textTopGap(0)
        , m_paddingBottom(0)
        , m_edgeGrip(0)
        , m_minWidth(0)
        , m_cornerRadius(0)
    {
    }
};

//工具条各部分的位置（窗口客户区坐标，物理像素）
struct SubtitleToolbar
{
    RECT m_title;     //会话名；没有会话名时宽度为 0
    RECT m_settings;  //设置按钮
    RECT m_lock;      //锁定 / 解锁按钮，锁定后浮出的小锁按钮也在这里
    RECT m_back;      //回到聊天窗按钮
    RECT m_close;     //退出字幕模式按钮
};

/**
 *  按字号与 DPI 算出各部分尺寸。
 *    fontSizeDip：字号，96 DPI 下的像素（SubtitleAppearance::m_fontSize）。
 *    dpi：        字幕所在显示器的 DPI；不大于 0 时按 96 处理。
 *  返回：各部分尺寸，物理像素。
 */
SubtitleMetrics ComputeSubtitleMetrics(int fontSizeDip, int dpi);

/**
 *  字幕窗口的高度：工具条 + 间距 + maxLines 行 + 底部留白。
 *    m：       各部分尺寸。
 *    maxLines：最多行数；超出 [kMinMaxLines, kMaxMaxLines] 时按边界处理。
 *  返回：窗口高度，物理像素。
 */
int SubtitleWindowHeight(const SubtitleMetrics& m, int maxLines);

/**
 *  文字区矩形（窗口客户区坐标）：左右各让出内边距，自工具条下方起、高 maxLines 行。
 *    m / width / maxLines：各部分尺寸、窗口宽度（物理像素）、最多行数。
 */
RECT SubtitleTextRect(const SubtitleMetrics& m, int width, int maxLines);

/**
 *  默认宽度：工作区宽度的 kDefaultWidthPercent，限制在 [最小宽度, min(默认上限, 工作区宽度)] 之内。
 *    workArea：字幕所在显示器的工作区。
 *    m：       各部分尺寸（用其中的 DPI 与最小宽度）。
 *  返回：宽度，物理像素。
 */
int DefaultSubtitleWidth(const RECT& workArea, const SubtitleMetrics& m);

/**
 *  工具条各部分的位置：「会话名 + 四个按钮」整组水平居中，竖直方向在工具条里居中。
 *    m：         各部分尺寸。
 *    width：     窗口宽度，物理像素。
 *    titleWidth：会话名文字的实际宽度，像素；超过窗口宽度的 kTitleMaxWidthPercent 时按该上限摆放
 *                （文字由绘制方截短）；不大于 0 表示没有会话名。
 *  返回：各部分的位置。
 */
SubtitleToolbar ComputeSubtitleToolbar(const SubtitleMetrics& m, int width, int titleWidth);

/**
 *  命中测试。
 *    m / tb：            各部分尺寸与工具条位置。
 *    width / height：    窗口宽高，物理像素。
 *    pt：                光标位置（窗口客户区坐标）。
 *    locked：            是否锁定。
 *    buttonsActive：     解锁时工具按钮能否点击（底色完全显示时为 true）；为 false 时按钮所在处算作 kPartBody。
 *    lockButtonVisible： 锁定时小锁按钮是否已经浮出；为 false 时那里也算作 kPartBody。
 *  返回：取值见 SubtitlePart。锁定时只可能是 kPartNone、kPartBody、kPartLock。
 */
int HitTestSubtitle(const SubtitleMetrics& m, const SubtitleToolbar& tb, int width, int height, POINT pt,
                    bool locked, bool buttonsActive, bool lockButtonVisible);

//进入字幕模式时决定窗口位置所需的输入
struct SubtitlePlaceInput
{
    bool              m_hasSaved;     //这个会话是否保存过位置
    SubtitlePlacement m_saved;        //保存的位置；m_hasSaved 为 false 时无意义
    int               m_height;       //窗口高度（SubtitleWindowHeight），物理像素
    int               m_defaultWidth; //没有保存过位置时用的宽度（DefaultSubtitleWidth），物理像素
    int               m_minWidth;     //最小宽度，物理像素
    int               m_stackGap;     //与其它字幕上下错开时的间距，物理像素
    std::vector<RECT> m_workAreas;    //各显示器的工作区（屏幕坐标），可以为空
    RECT              m_primaryWork;  //主显示器的工作区，没有保存过位置时放在这里
    std::vector<RECT> m_occupied;     //其它会话的字幕此刻所在的矩形（屏幕坐标），新字幕尽量避开

    SubtitlePlaceInput()
        : m_hasSaved(false)
        , m_height(0)
        , m_defaultWidth(0)
        , m_minWidth(0)
        , m_stackGap(0)
    {
        ::SetRectEmpty(&m_primaryWork);
    }
};

/**
 *  进入字幕模式时窗口放在哪里，规则见文件头。
 *    in：输入。
 *  返回：窗口矩形，屏幕坐标、物理像素。本函数无副作用。
 */
RECT ResolveSubtitleRect(const SubtitlePlaceInput& in);

/**
 *  把窗口矩形平移到整个落在一个工作区里：选与它重叠最多的工作区（都不重叠时选 fallback）；
 *  宽高超过该工作区时先缩到工作区大小。拖动、调宽结束后与显示器布局变化后调用。
 *    rc：       窗口矩形，屏幕坐标。
 *    workAreas：各显示器的工作区，可以为空。
 *    fallback： 都不重叠时使用的工作区，通常是主显示器的工作区。
 *  返回：平移后的矩形。
 */
RECT ClampSubtitleRect(const RECT& rc, const std::vector<RECT>& workAreas, const RECT& fallback);

} // namespace subtitle
