/**
 *  图片无法直接显示时的占位符：排版计算与绘制，ImagePlaceholder.h
 *
 *  聊天窗的图片气泡与消息记录窗的缩略图共用同一套占位符，包括：
 *    · 破图：发送失败、下载失败、本地文件丢失或无法解码时显示；
 *    · 可下载：消息记录窗里本地没有原图、但可以单击下载的图片；
 *    · 下载中：消息记录窗里已发起下载、尚未完成的图片。
 *  设计稿见仓库根目录 snapshots/broken_image_placeholder_design.png 与
 *  snapshots/history_image_download_placeholder_design.png。
 *
 *  本模块分三部分：
 *    · 纯计算 ComputeImagePlaceholderLayout：按占位块尺寸与说明文字尺寸，算出图标的
 *      大小与位置、说明文字是否显示以及显示在哪里。不访问 GDI，由 FlamingoUnit 直接
 *      断言（Tests/ImagePlaceholderTests.cpp）。
 *    · 纯判断 ResolveChatImageFailure / ResolveHistoryThumb：聊天气泡里的图片该按哪种
 *      失败显示，消息记录窗的缩略图该显示哪种占位、能否单击下载。同样由 FlamingoUnit
 *      直接断言。
 *    · GDI+ 绘制：破图图标、完整的照片图标、下载标记、下载中的加载圆环，以及把底色与
 *      破图图标组合起来的 PaintImagePlaceholderShapes。说明文字不在这里画 —— 调用方
 *      各有自己的 GDI 文字绘制函数，按 m_captionRect 画即可；量文字尺寸请用
 *      MeasureImagePlaceholderCaption，它建字体的参数与聊天气泡的 DrawGdiText 一致。
 *
 *  本文件不 include stdafx.h（PCH 设 NotUsing），与 FlamingoUnit 测试工程共用。
 *
 *  典型用法（聊天气泡，破图 + 说明文字）：
 *      const SIZE cap = MeasureImagePlaceholderCaption(hdc, L"图片加载失败", kImagePlaceholderCaptionPx);
 *      const ImagePlaceholderLayout lay = ComputeImagePlaceholderLayout(rcImage, cap.cx, cap.cy);
 *      PaintImagePlaceholderShapes(g, rcImage, lay, kImagePlaceholderBg);   // g 为 Gdiplus::Graphics
 *      if (lay.m_showCaption) { 在 lay.m_captionRect 里用 GDI 画文字; }
 *
 *  balloonwj@qq.com   2026-09-30
 */

#pragma once

#include <windows.h>   // RECT / POINT / SIZE / COLORREF / HDC

// 仅用作引用类型，前置声明，避免在头文件引入 <gdiplus.h>。
namespace Gdiplus { class Graphics; }

namespace balloonchatwnd {

// =======================================================================
// 排版常量（像素）
// =======================================================================

// 图标宽度占占位块短边的百分比。
const int kImagePlaceholderGlyphPercent = 30;

// 图标宽度的常规下限：再小裂缝就看不出来。占位块本身放得下时不会小于它。
const int kImagePlaceholderGlyphMin = 20;

// 图标宽度的上限：大图的占位块里图标也不再放大。
const int kImagePlaceholderGlyphMax = 44;

// 图标高度占宽度的百分比（照片图标是横向的矩形）。绘制与排版共用这一比例。
const int kImagePlaceholderGlyphHeightPercent = 80;

// 占位块很小、图标按下限放不下时，图标与占位块边缘至少保留的距离。
const int kImagePlaceholderTightMargin = 2;

// 图标宽度的绝对下限：占位块小到连这个尺寸都放不下时不画图标。
const int kImagePlaceholderGlyphAbsMin = 8;

// 图标与说明文字之间的竖直间距。
const int kImagePlaceholderCaptionGap = 8;

// 说明文字与占位块边缘至少保留的距离；四周放不下这圈留白时不显示文字。
const int kImagePlaceholderCaptionMargin = 8;

// 说明文字的字号（像素高度），与原先"发送失败"覆盖文字一致。
const int kImagePlaceholderCaptionPx = 12;

// =======================================================================
// 颜色
// =======================================================================

// 占位块底色，与原先加载中占位块的底色同一色系。
const COLORREF kImagePlaceholderBg = RGB(0xEE, 0xEC, 0xE8);

// 可点击的占位块在鼠标悬停时的底色（比常态深一档）。
const COLORREF kImagePlaceholderHoverBg = RGB(0xE3, 0xE0, 0xDA);

// 图标描边色。
const COLORREF kImagePlaceholderGlyphStroke = RGB(0xAB, 0xA5, 0x9C);

// 下载中时图标描边色（比常态浅，突出前面的加载圆环）。
const COLORREF kImagePlaceholderGlyphFaded = RGB(0xC4, 0xBF, 0xB8);

// 说明文字颜色：加载失败（对方发来的图片、本地文件丢失等）。
const COLORREF kImagePlaceholderCaptionGray = RGB(0x9A, 0x97, 0x92);

// 说明文字颜色：发送失败（自己发出的图片），保留警示意味。
const COLORREF kImagePlaceholderCaptionRed = RGB(0xD9, 0x53, 0x4F);

// 下载标记底色。
const COLORREF kImagePlaceholderBadgeBlue = RGB(0x2F, 0x6F, 0xD0);

// 鼠标悬停时下载标记底色（比常态深一档）。
const COLORREF kImagePlaceholderBadgeBlueHover = RGB(0x24, 0x58, 0xAA);

// =======================================================================
// 排版计算
// =======================================================================

/**
 *  占位块内图标与说明文字的排版结果。
 */
struct ImagePlaceholderLayout
{
    // 是否画图标。占位块小到连 kImagePlaceholderGlyphAbsMin 都放不下时为 false，
    // 此时 m_showCaption 也一定为 false。
    bool  m_drawGlyph;

    // 图标宽度（像素）；高度为它的 kImagePlaceholderGlyphHeightPercent%。
    int   m_glyphSize;

    // 图标中心点（与传入的占位块矩形同一坐标系）。
    POINT m_glyphCenter;

    // 是否显示说明文字。
    bool  m_showCaption;

    // 说明文字的矩形（与传入的占位块矩形同一坐标系），仅 m_showCaption 为 true 时有效。
    // 宽高即调用方传入的文字尺寸，水平居中，位于图标下方。
    RECT  m_captionRect;
};

/**
 *  计算占位块内图标与说明文字的排版。
 *
 *    rc：       占位块矩形（任意坐标系）。宽或高 <= 0 时什么都不画。
 *    captionW / captionH：说明文字的尺寸（像素，用 MeasureImagePlaceholderCaption 量得）。
 *               任一 <= 0 表示没有说明文字，只画图标。
 *
 *  规则：
 *    · 图标宽度取占位块短边的 kImagePlaceholderGlyphPercent%，夹在
 *      [kImagePlaceholderGlyphMin, kImagePlaceholderGlyphMax] 之间；
 *    · 占位块太小、按上面的宽度放不下时，把图标缩到四周各留
 *      kImagePlaceholderTightMargin 恰好放下；缩到 kImagePlaceholderGlyphAbsMin
 *      以下则不画图标；
 *    · 说明文字只有在四周都能留出 kImagePlaceholderCaptionMargin、且图标与文字之间
 *      能留出 kImagePlaceholderCaptionGap 时才显示；
 *    · 图标（与文字）作为一个整体在占位块内水平、垂直居中。
 *
 *  返回：排版结果。纯函数，无副作用。
 */
ImagePlaceholderLayout ComputeImagePlaceholderLayout(const RECT& rc, int captionW, int captionH);

/**
 *  量说明文字的尺寸。建字体的参数（微软雅黑、常规字重、ClearType、按像素高度）与
 *  聊天气泡的 DrawGdiText 一致，调用方必须用同样参数的字体画文字，否则量出的宽度
 *  与画出来的对不上。
 *
 *    hdc：        用于测量的设备上下文（只借用，函数返回前恢复原字体）。
 *    text：       说明文字；NULL 或空串时返回 {0, 0}。
 *    pixelHeight：字号（像素高度），通常传 kImagePlaceholderCaptionPx。
 *
 *  返回：文字的宽高（像素）；字体创建失败时返回 {0, 0}。
 */
SIZE MeasureImagePlaceholderCaption(HDC hdc, LPCWSTR text, int pixelHeight);

// =======================================================================
// 聊天气泡：失败判断
// =======================================================================

/**
 *  聊天气泡里一张图片应当按哪种失败来显示。
 */
enum ChatImageFailure
{
    // 不失败：图片正常显示，或保持上传中 / 下载中的占位样子不变。
    kChatImageNotFailed  = 0,

    // 发送失败：本端发出的图片没有上传成功。破图 + 红色"图片发送失败"。
    kChatImageSendFailed = 1,

    // 加载失败：收到的图片下载失败，或本地文件丢失、内容损坏无法解码。
    // 破图 + 灰色"图片加载失败"。
    kChatImageLoadFailed = 2,
};

/**
 *  判断聊天气泡里的图片该按哪种失败显示。
 *
 *    imageState：  ChatMessage::imageState，取值见 ChatMessageMenu.h 的 kChatImageState*。
 *    hasPath：     这条消息是否带有本地图片路径。没有路径的"正常"态图片是调试入口里
 *                  只画占位与标签的示例图，不算失败。
 *    decodeFailed：ChatImageCache::IsFailed 的结果（本地文件丢失或无法解码）。只在
 *                  "正常"态下参与判断。
 *
 *  返回：失败类型。纯函数，无副作用。上传中、下载中一律不算失败。
 */
ChatImageFailure ResolveChatImageFailure(int imageState, bool hasPath, bool decodeFailed);

/**
 *  取某种失败要显示的说明文字。
 *    failure：失败类型。
 *  返回：说明文字（静态字符串，调用方不要释放）；kChatImageNotFailed 返回空串。
 */
LPCWSTR ChatImageFailureCaption(ChatImageFailure failure);

/**
 *  取某种失败的说明文字颜色：发送失败为 kImagePlaceholderCaptionRed，其余为
 *  kImagePlaceholderCaptionGray。
 */
COLORREF ChatImageFailureCaptionColor(ChatImageFailure failure);

// =======================================================================
// 消息记录窗：图片缩略图的状态判断
// =======================================================================

/**
 *  消息记录窗里一张图片缩略图的显示状态。
 */
enum HistoryThumbState
{
    // 显示真实缩略图：本地有原图。解码完成前暂时只铺底色；若解码失败（文件损坏），
    // 绘制时会按 ChatImageCache::IsFailed 改画破图。
    kHistoryThumbImage        = 0,

    // 需要下载：本地没有原图、但消息带有下载所需的信息。灰色照片图标 + 蓝色下载标记，
    // 可单击下载。
    kHistoryThumbNeedDownload = 1,

    // 下载中：本窗口已发起下载、尚未完成。淡色照片图标 + 加载圆环，不可点击。
    kHistoryThumbDownloading  = 2,

    // 破图：下载失败、本地文件损坏，或消息缺少下载所需的信息。
    kHistoryThumbBroken       = 3,
};

/**
 *  缩略图状态判断的结果。
 */
struct HistoryThumbDecision
{
    // 显示状态。
    HistoryThumbState m_state;

    // 单击缩略图是否发起下载（需要下载，或下载失败后重试）。
    bool m_clickable;

    // 鼠标悬停时的提示文字（静态字符串，调用方不要释放）；空串表示不挂提示。
    LPCWSTR m_tooltip;
};

/**
 *  判断消息记录窗里一张图片缩略图的显示状态、能否单击下载，以及悬停提示。
 *
 *    downloadable：  消息是否带有下载所需的信息（图片的文件 ID 与内容 MD5 都不为空）。
 *    fileExists：    本地缓存里是否已有原图文件。
 *    downloading：   本窗口是否已为这张图发起下载、尚未完成。
 *    downloadFailed：本窗口为这张图发起的下载是否失败过（且之后没有再成功）。
 *    decodeFailed：  本地原图是否解码失败（ChatImageCache::IsFailed），只在文件存在时参与判断。
 *
 *  返回：判断结果。纯函数，无副作用。判断顺序：下载中优先；其次本地有文件则看能否解码；
 *        本地没有文件时，下载失败过的按破图（仍可单击重试），能下载的按"需要下载"，
 *        不能下载的按破图（不可点击）。
 */
HistoryThumbDecision ResolveHistoryThumb(bool downloadable, bool fileExists, bool downloading,
                                         bool downloadFailed, bool decodeFailed);

// =======================================================================
// GDI+ 绘制
// =======================================================================

/**
 *  画破图图标：完整的照片图标沿一条折线裂成左右两半，两半各自错开、略微倾斜。
 *
 *    g：      GDI+ 绘图对象（只借用；函数内会 Save / Restore 其变换与裁剪）。
 *    cx / cy：图标中心点。
 *    size：   图标宽度（像素）；高度为其 kImagePlaceholderGlyphHeightPercent%。
 *    stroke： 描边颜色，通常传 kImagePlaceholderGlyphStroke。
 */
void DrawBrokenImageGlyph(Gdiplus::Graphics& g, float cx, float cy, float size, COLORREF stroke);

/**
 *  画完整的照片图标（圆角边框、太阳、山脊）。参数含义同 DrawBrokenImageGlyph。
 */
void DrawPhotoGlyph(Gdiplus::Graphics& g, float cx, float cy, float size, COLORREF stroke);

/**
 *  画下载标记：带白色描边的实心圆，中间一个白色向下箭头。
 *
 *    g：       GDI+ 绘图对象（只借用）。
 *    cx / cy： 圆心。
 *    diameter：圆的直径（像素，不含外圈白色描边）。
 *    fill：    圆的底色，常态传 kImagePlaceholderBadgeBlue，悬停传 ...BadgeBlueHover。
 */
void DrawDownloadBadge(Gdiplus::Graphics& g, float cx, float cy, float diameter, COLORREF fill);

/**
 *  画下载中的加载圆环：白色圆底上一段 270 度的蓝色圆弧（静止，不转动）。
 *  参数含义同 DrawDownloadBadge（没有 fill）。
 */
void DrawDownloadingBadge(Gdiplus::Graphics& g, float cx, float cy, float diameter);

/**
 *  画破图占位块的图形部分：铺底色，再按排版结果画破图图标。说明文字不在这里画。
 *
 *    g：     GDI+ 绘图对象（只借用）。
 *    rc：    占位块矩形（与 g 同一坐标系）。
 *    layout：ComputeImagePlaceholderLayout 对同一个 rc 的计算结果。
 *    bg：    底色，通常传 kImagePlaceholderBg。
 */
void PaintImagePlaceholderShapes(Gdiplus::Graphics& g, const RECT& rc,
                                 const ImagePlaceholderLayout& layout, COLORREF bg);

} // namespace balloonchatwnd
